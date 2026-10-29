// SPDX-License-Identifier: GPL-2.0-only
// Chapter 06E / E03: one producer, four competing event consumers.
// Host check: gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I../include ...
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "thermal_uapi.h"

#define CONSUMERS 4U
#define PRODUCER_WRITES 10000U
#define POLL_MS 50
#define DEADLINE_NS UINT64_C(5000000000)

struct shared {
    atomic_int start;
    atomic_int producer_done;
    atomic_uint errors;
    uint64_t baseline_seq;
    atomic_uchar seen[PRODUCER_WRITES + 1U];
    uint64_t stamps[PRODUCER_WRITES + 1U];
};

struct consumer {
    struct shared *shared;
    int fd;
    unsigned int id;
    uint64_t received;
    uint64_t latency_sum_ns;
    uint64_t latency_max_ns;
    uint64_t last_seq;
};

struct producer {
    struct shared *shared;
    int fd;
    unsigned int writes_ok;
};

static uint64_t mono_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static int write_value(int fd, char value)
{
    ssize_t n = write(fd, &value, 1);
    if (n == 1) return 0;
    if (n < 0) perror("write thermal_core");
    else fprintf(stderr, "short state write: %zd\n", n);
    return -1;
}

static int read_record(int fd, struct thermal_core_event *ev)
{
    ssize_t n = read(fd, ev, sizeof(*ev));
    if (n == (ssize_t)sizeof(*ev)) return 1;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    if (n < 0) perror("read thermal_events");
    else fprintf(stderr, "short event read: %zd\n", n);
    return -1;
}

static void fail(struct shared *s, const char *what)
{
    if (atomic_fetch_add_explicit(&s->errors, 1U, memory_order_relaxed) < 8U)
        fprintf(stderr, "[ERROR] %s\n", what);
}

static void *producer_main(void *arg)
{
    struct producer *p = arg;
    while (!atomic_load_explicit(&p->shared->start, memory_order_acquire))
        sched_yield();
    for (unsigned int i = 0; i < PRODUCER_WRITES; ++i) {
        char value = (i & 1U) ? '0' : '1';
        if (write_value(p->fd, value) != 0) {
            fail(p->shared, "producer write failed");
            break;
        }
        ++p->writes_ok;
    }
    atomic_store_explicit(&p->shared->producer_done, 1, memory_order_release);
    return NULL;
}

static void *consumer_main(void *arg)
{
    struct consumer *c = arg;
    struct shared *s = c->shared;
    while (!atomic_load_explicit(&s->start, memory_order_acquire))
        sched_yield();
    uint64_t end_by = mono_ns() + DEADLINE_NS;
    for (;;) {
        struct thermal_core_event ev;
        int rc = read_record(c->fd, &ev);
        if (rc < 0) { fail(s, "event read failed"); break; }
        if (rc == 0) {
            if (atomic_load_explicit(&s->producer_done, memory_order_acquire))
                break; /* Producer finished, and the shared FIFO is empty. */
            if (mono_ns() >= end_by) { fail(s, "consumer deadline exceeded"); break; }
            struct pollfd pfd = {.fd = c->fd, .events = POLLIN};
            int ready = poll(&pfd, 1, POLL_MS);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) { fail(s, "poll failed"); break; }
            if (ready && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                fail(s, "unexpected poll flags"); break;
            }
            continue;
        }
        if (ev.abi_version != THERMAL_UAPI_VERSION ||
            ev.sequence <= s->baseline_seq ||
            ev.sequence > s->baseline_seq + PRODUCER_WRITES ||
            (c->received && ev.sequence <= c->last_seq)) {
            fail(s, "invalid UAPI, sequence range or per-reader order");
            continue;
        }
        uint64_t index = ev.sequence - s->baseline_seq;
        if (ev.state != (uint32_t)(index & UINT64_C(1))) {
            fail(s, "unexpected state for sequence parity");
            continue;
        }
        if (atomic_exchange_explicit(&s->seen[index], 1U, memory_order_relaxed)) {
            fail(s, "duplicate event across readers");
            continue;
        }
        s->stamps[index] = ev.timestamp_ns;
        c->last_seq = ev.sequence;
        ++c->received;
        uint64_t observed = mono_ns();
        if (observed < ev.timestamp_ns) {
            fail(s, "event timestamp is in the future");
            continue;
        }
        uint64_t latency = observed - ev.timestamp_ns;
        c->latency_sum_ns += latency;
        if (latency > c->latency_max_ns) c->latency_max_ns = latency;
    }
    return NULL;
}

int main(void)
{
    int control = -1, result = EXIT_FAILURE;
    struct shared *s = NULL;
    struct producer p = {.fd = -1};
    struct consumer c[CONSUMERS] = {0};
    pthread_t writer, readers[CONSUMERS];
    unsigned int readers_started = 0;
    int writer_started = 0;
    uint64_t elapsed = 0, t0 = 0, total = 0, latency_sum = 0, latency_max = 0;

    if (sizeof(struct thermal_core_event) != 24U) {
        fputs("wrong thermal_core_event ABI size\n", stderr);
        return EXIT_FAILURE;
    }
    s = calloc(1, sizeof(*s));
    if (!s) { perror("calloc"); return EXIT_FAILURE; }
    atomic_init(&s->start, 0);
    atomic_init(&s->producer_done, 0);
    atomic_init(&s->errors, 0);
    for (unsigned int i = 0; i <= PRODUCER_WRITES; ++i)
        atomic_init(&s->seen[i], 0U);
    for (unsigned int i = 0; i < CONSUMERS; ++i) c[i].fd = -1;

    control = open("/dev/thermal_core", O_RDWR | O_CLOEXEC);
    int setup_events = open("/dev/thermal_events", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (control < 0 || setup_events < 0) {
        perror("open thermal devices");
        if (setup_events >= 0) close(setup_events);
        goto cleanup;
    }
    if (ioctl(control, THERMAL_IOC_RESET) < 0) {
        perror("RESET"); close(setup_events); goto cleanup;
    }
    /* Assumes no other process touches the control/event devices. */
    struct thermal_core_event ev = {0};
    for (;;) {
        int rc = read_record(setup_events, &ev);
        if (rc < 0) { close(setup_events); goto cleanup; }
        if (rc == 0) break;
    }
    /* Establish known baseline: state 0, and last event sequence known. */
    if (write_value(control, '1') || read_record(setup_events, &ev) != 1 ||
        ev.abi_version != THERMAL_UAPI_VERSION || ev.state != 1 ||
        write_value(control, '0') || read_record(setup_events, &ev) != 1 ||
        ev.abi_version != THERMAL_UAPI_VERSION || ev.state != 0) {
        fputs("failed to establish event baseline\n", stderr);
        close(setup_events); goto cleanup;
    }
    s->baseline_seq = ev.sequence;
    close(setup_events);

    p.shared = s;
    p.fd = open("/dev/thermal_core", O_WRONLY | O_CLOEXEC);
    if (p.fd < 0) { perror("open producer"); goto cleanup; }
    for (unsigned int i = 0; i < CONSUMERS; ++i) {
        c[i].shared = s;
        c[i].id = i;
        c[i].fd = open("/dev/thermal_events", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (c[i].fd < 0) { perror("open consumer"); goto cleanup; }
        int rc = pthread_create(&readers[i], NULL, consumer_main, &c[i]);
        if (rc) { fprintf(stderr, "pthread_create reader: %s\n", strerror(rc)); goto cleanup; }
        ++readers_started;
    }
    int rc = pthread_create(&writer, NULL, producer_main, &p);
    if (rc) { fprintf(stderr, "pthread_create writer: %s\n", strerror(rc)); goto cleanup; }
    writer_started = 1;
    t0 = mono_ns();
    atomic_store_explicit(&s->start, 1, memory_order_release);
    pthread_join(writer, NULL);
    writer_started = 0;
    for (unsigned int i = 0; i < readers_started; ++i)
        pthread_join(readers[i], NULL);
    readers_started = 0;
    elapsed = mono_ns() - t0;

    for (unsigned int i = 0; i < CONSUMERS; ++i) {
        total += c[i].received;
        latency_sum += c[i].latency_sum_ns;
        if (c[i].latency_max_ns > latency_max) latency_max = c[i].latency_max_ns;
    }
    unsigned int errors = atomic_load_explicit(&s->errors, memory_order_relaxed);
    if (p.writes_ok != PRODUCER_WRITES || total > p.writes_ok) ++errors;
    uint64_t unique_seen = 0, prev_stamp = 0;
    for (unsigned int i = 1; i <= PRODUCER_WRITES; ++i) {
        if (!atomic_load_explicit(&s->seen[i], memory_order_relaxed)) continue;
        ++unique_seen;
        if (prev_stamp && s->stamps[i] < prev_stamp) ++errors;
        prev_stamp = s->stamps[i];
    }
    if (unique_seen != total) ++errors;
    printf("E03 consumers=%u writes=%u received=%" PRIu64
           " dropped=%" PRIu64 " errors=%u\n",
           CONSUMERS, p.writes_ok, total, (uint64_t)p.writes_ok - total, errors);
    printf("E03 reader_counts=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
           " elapsed_ms=%.3f writes_per_sec=%.1f\n",
           c[0].received, c[1].received, c[2].received, c[3].received,
           (double)elapsed / 1e6,
           elapsed ? (double)p.writes_ok * 1e9 / (double)elapsed : 0.0);
    printf("E03 userspace_observed_latency_us_avg=%.3f max=%.3f\n",
           total ? (double)latency_sum / (double)total / 1e3 : 0.0,
           (double)latency_max / 1e3);
    if (!errors && p.writes_ok == PRODUCER_WRITES && unique_seen == total) {
        puts("E03 PASS (competing consumers: no duplicates, ordering intact)");
        result = EXIT_SUCCESS;
    } else {
        puts("E03 FAIL");
    }

cleanup:
    atomic_store_explicit(&s->producer_done, 1, memory_order_release);
    atomic_store_explicit(&s->start, 1, memory_order_release);
    if (writer_started) pthread_join(writer, NULL);
    for (unsigned int i = 0; i < readers_started; ++i) pthread_join(readers[i], NULL);
    for (unsigned int i = 0; i < CONSUMERS; ++i)
        if (c[i].fd >= 0) close(c[i].fd);
    if (p.fd >= 0) close(p.fd);
    if (control >= 0) close(control);
    free(s);
    return result;
}
