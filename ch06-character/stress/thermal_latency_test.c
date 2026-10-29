// SPDX-License-Identifier: GPL-2.0-only
// Chapter 06E / E05: paced, single-producer event-delivery latency comparison.
// Measures kernel event timestamp -> userspace completion of read().
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "thermal_uapi.h"

#define SAMPLES 1000U
#define READ_TIMEOUT_MS 2000
#define ARM_DELAY_NS 1000000L /* Allow reader to reach read()/poll(). Not a proof of sleep. */

struct trial {
    int control;
    int events;
    int use_poll;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned int armed;
    unsigned int received;
    unsigned int errors;
    uint64_t latency[SAMPLES];
    uint64_t last_seq;
    uint64_t last_timestamp;
};

static uint64_t monotonic_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static struct timespec deadline_in_seconds(time_t seconds)
{
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t) != 0) { perror("clock_gettime realtime"); exit(2); }
    t.tv_sec += seconds;
    return t;
}

static void announce_failure(struct trial *t, const char *message, unsigned int i)
{
    fprintf(stderr, "[FAIL] %s sample=%u errno=%d (%s)\n", message, i, errno, strerror(errno));
    pthread_mutex_lock(&t->lock);
    t->errors++;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

static void *reader_main(void *arg)
{
    struct trial *t = arg;
    for (unsigned int i = 0; i < SAMPLES; i++) {
        pthread_mutex_lock(&t->lock);
        t->armed = i + 1;
        pthread_cond_broadcast(&t->cond);
        pthread_mutex_unlock(&t->lock);

        if (t->use_poll) {
            struct pollfd p = { .fd = t->events, .events = POLLIN };
            int rc;
            do { rc = poll(&p, 1, READ_TIMEOUT_MS); } while (rc < 0 && errno == EINTR);
            if (rc != 1 || !(p.revents & POLLIN) || (p.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                announce_failure(t, "poll readiness/timeout", i);
                return NULL;
            }
        }
        struct thermal_core_event e = {0};
        ssize_t n;
        do { n = read(t->events, &e, sizeof(e)); } while (n < 0 && errno == EINTR);
        uint64_t now = monotonic_ns();
        if (n != (ssize_t)sizeof(e)) {
            announce_failure(t, "read returned wrong size", i);
            return NULL;
        }
        uint32_t expected_state = (i & 1U) ? 0U : 1U;
        if (e.abi_version != THERMAL_UAPI_VERSION || e.state != expected_state ||
            (i > 0 && e.sequence != t->last_seq + 1) ||
            e.timestamp_ns < t->last_timestamp || now < e.timestamp_ns) {
            announce_failure(t, "event order/state/timestamp", i);
            return NULL;
        }
        t->last_seq = e.sequence;
        t->last_timestamp = e.timestamp_ns;
        t->latency[i] = now - e.timestamp_ns;

        pthread_mutex_lock(&t->lock);
        t->received = i + 1;
        pthread_cond_broadcast(&t->cond);
        pthread_mutex_unlock(&t->lock);
    }
    return NULL;
}

static int wait_for_counter(struct trial *t, const unsigned int *counter, unsigned int target)
{
    struct timespec deadline = deadline_in_seconds(3);
    int rc = 0;
    pthread_mutex_lock(&t->lock);
    while (*counter < target && t->errors == 0) {
        rc = pthread_cond_timedwait(&t->cond, &t->lock, &deadline);
        if (rc != 0) break;
    }
    int ok = (*counter >= target && t->errors == 0 && rc == 0);
    pthread_mutex_unlock(&t->lock);
    if (!ok) fprintf(stderr, "[FAIL] waiting for sample=%u rc=%d\n", target, rc);
    return ok ? 0 : -1;
}

static int compare_uint64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static void report(const struct trial *t)
{
    uint64_t sorted[SAMPLES], total = 0;
    unsigned int over100 = 0, over1000 = 0;
    for (unsigned int i = 0; i < SAMPLES; i++) {
        sorted[i] = t->latency[i];
        total += sorted[i];
        if (sorted[i] > UINT64_C(100000)) over100++;
        if (sorted[i] > UINT64_C(1000000)) over1000++;
    }
    qsort(sorted, SAMPLES, sizeof(sorted[0]), compare_uint64);
    printf("E05 mode=%s samples=%u errors=0 latency_us_avg=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f over100us=%u over1ms=%u\n",
           t->use_poll ? "poll+read" : "blocking-read", SAMPLES,
           (double)total / SAMPLES / 1000.0,
           (double)sorted[499] / 1000.0,
           (double)sorted[949] / 1000.0,
           (double)sorted[989] / 1000.0,
           (double)sorted[999] / 1000.0, over100, over1000);
}

static int prepare_devices(int control)
{
    if (ioctl(control, THERMAL_IOC_RESET) != 0) { perror("RESET"); return -1; }
    int fd = open("/dev/thermal_events", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror("open for drain"); return -1; }
    for (;;) {
        struct thermal_core_event e;
        ssize_t n = read(fd, &e, sizeof(e));
        if (n == (ssize_t)sizeof(e)) continue;
        if (n < 0 && errno == EAGAIN) break;
        fprintf(stderr, "drain error: read=%zd errno=%d\n", n, errno);
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int run_trial(int control, int use_poll)
{
    struct trial t = { .control=control, .events=-1, .use_poll=use_poll };
    pthread_t reader;
    int started = 0, ok = 1;
    if (prepare_devices(control) != 0) return -1;
    t.events = open("/dev/thermal_events", O_RDONLY | O_CLOEXEC);
    if (t.events < 0) { perror("open blocking events"); return -1; }
    if (pthread_mutex_init(&t.lock, NULL) != 0) { close(t.events); return -1; }
    if (pthread_cond_init(&t.cond, NULL) != 0) {
        pthread_mutex_destroy(&t.lock); close(t.events); return -1;
    }
    int rc = pthread_create(&reader, NULL, reader_main, &t);
    if (rc != 0) { fprintf(stderr, "pthread_create: %s\n", strerror(rc)); ok=0; goto cleanup; }
    started = 1;
    for (unsigned int i = 0; i < SAMPLES; i++) {
        if (wait_for_counter(&t, &t.armed, i + 1) != 0) { ok = 0; break; }
        const struct timespec delay = { .tv_sec=0, .tv_nsec=ARM_DELAY_NS };
        struct timespec remaining = delay;
        while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) { }
        char value = (i & 1U) ? '0' : '1';
        ssize_t n = write(control, &value, 1);
        if (n != 1) { perror("write state"); ok = 0; break; }
        if (wait_for_counter(&t, &t.received, i + 1) != 0) { ok = 0; break; }
    }
cleanup:
    if (started) {
        if (!ok) pthread_cancel(reader);
        rc = pthread_join(reader, NULL);
        if (rc != 0) { fprintf(stderr, "pthread_join: %s\n", strerror(rc)); ok = 0; }
    }
    if (ok && t.errors == 0 && t.received == SAMPLES)
        report(&t);
    else {
        fprintf(stderr, "E05 mode=%s FAIL (received=%u errors=%u)\n",
                use_poll ? "poll+read" : "blocking-read", t.received, t.errors);
        ok = 0;
    }
    pthread_cond_destroy(&t.cond);
    pthread_mutex_destroy(&t.lock);
    close(t.events);
    return ok ? 0 : -1;
}

int main(void)
{
    if (sizeof(struct thermal_core_event) != 24) { fputs("Unexpected event ABI size\n", stderr); return 1; }
    int control = open("/dev/thermal_core", O_WRONLY | O_CLOEXEC);
    if (control < 0) { perror("open thermal_core"); return 1; }
    puts("E05 requires exclusive device access. Each mode uses 1000 paced events (~1 ms apart).");
    int passed = 0;
    if (run_trial(control, 0) == 0) passed++;
    if (run_trial(control, 1) == 0) passed++;
    close(control);
    printf("E05 SUMMARY PASS=%d FAIL=%d TOTAL=%d/2\n", passed, 2-passed, passed);
    return passed == 2 ? 0 : 1;
}
