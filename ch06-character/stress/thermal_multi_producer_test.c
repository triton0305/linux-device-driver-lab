// SPDX-License-Identifier: GPL-2.0-only
// Chapter 06E / E02: four concurrent state writers and one event consumer.
// Build: gcc -std=c11 -O2 -Wall -Wextra -Werror -pthread -I../include thermal_multi_producer_test.c -o thermal_multi_producer_test
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

#define PRODUCERS 4U
#define WRITES_EACH 10000U
#define POLL_TIMEOUT_MS 100

struct worker {
    unsigned int id;
    int fd;
    atomic_int *start;
    atomic_uint *finished;
    unsigned int writes_ok;
    int error;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int write_state(int fd, char value)
{
    ssize_t n = write(fd, &value, 1);
    if (n == 1)
        return 0;
    if (n < 0)
        perror("write /dev/thermal_core");
    else
        fprintf(stderr, "short write: %zd\n", n);
    return -1;
}

/* 1: event, 0: EAGAIN, -1: failure */
static int read_event(int fd, struct thermal_core_event *ev)
{
    ssize_t n = read(fd, ev, sizeof(*ev));
    if (n == (ssize_t)sizeof(*ev))
        return 1;
    if (n < 0 && errno == EAGAIN)
        return 0;
    if (n < 0)
        perror("read /dev/thermal_events");
    else
        fprintf(stderr, "short event read: %zd\n", n);
    return -1;
}

static int take_event(int fd, struct thermal_core_event *ev)
{
    int rc = read_event(fd, ev);
    if (rc != 1)
        fprintf(stderr, "expected exactly one event; got %d\n", rc);
    return rc == 1 ? 0 : -1;
}

static void *producer_main(void *arg)
{
    struct worker *w = arg;
    while (!atomic_load_explicit(w->start, memory_order_acquire))
        sched_yield();

    for (unsigned int i = 0; i < WRITES_EACH; ++i) {
        /* Each producer alternates, but competing writes may be idempotent. */
        char value = ((i + w->id) & 1U) ? '0' : '1';
        if (write_state(w->fd, value) != 0) {
            w->error = 1;
            break;
        }
        ++w->writes_ok;
    }
    atomic_fetch_add_explicit(w->finished, 1U, memory_order_release);
    return NULL;
}

int main(void)
{
    int control = -1, events = -1, result = EXIT_FAILURE;
    struct worker workers[PRODUCERS] = {0};
    pthread_t threads[PRODUCERS];
    unsigned int started = 0, successful_writes = 0;
    atomic_int start;
    atomic_uint finished;
    struct thermal_core_event ev = {0};
    struct thermal_core_state state = {0};
    uint64_t baseline_seq = 0, expected_seq = 0, prior_ts = 0;
    uint64_t read_count = 0, internal_gaps = 0, transitions = 0, dropped = 0;
    uint64_t latency_sum = 0, latency_max = 0, t0 = 0, elapsed = 0;
    int invalid = 0;

    atomic_init(&start, 0);
    atomic_init(&finished, 0);
    for (unsigned int i = 0; i < PRODUCERS; ++i)
        workers[i].fd = -1;

    if (sizeof(ev) != 24 || sizeof(state) != 16) {
        fprintf(stderr, "unexpected UAPI structure layout\n");
        return EXIT_FAILURE;
    }
    control = open("/dev/thermal_core", O_RDWR | O_CLOEXEC);
    events = open("/dev/thermal_events", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (control < 0 || events < 0) {
        perror("open thermal devices");
        goto cleanup;
    }
    if (ioctl(control, THERMAL_IOC_RESET) < 0) {
        perror("ioctl RESET");
        goto cleanup;
    }
    /* Exclusive-device assumption: no other process changes the state. */
    for (;;) {
        int rc = read_event(events, &ev);
        if (rc < 0) goto cleanup;
        if (rc == 0) break;
    }

    /* Explicit sequence baseline; state must end at 0. */
    if (write_state(control, '1') || take_event(events, &ev) ||
        ev.abi_version != THERMAL_UAPI_VERSION || ev.state != 1) {
        fprintf(stderr, "baseline state=1 failed\n");
        goto cleanup;
    }
    if (write_state(control, '0') || take_event(events, &ev) ||
        ev.abi_version != THERMAL_UAPI_VERSION || ev.state != 0) {
        fprintf(stderr, "baseline state=0 failed\n");
        goto cleanup;
    }
    baseline_seq = ev.sequence;
    expected_seq = baseline_seq + 1;
    prior_ts = ev.timestamp_ns;
    for (unsigned int i = 0; i < PRODUCERS; ++i) {
        workers[i].id = i;
        workers[i].fd = open("/dev/thermal_core", O_WRONLY | O_CLOEXEC);
        workers[i].start = &start;
        workers[i].finished = &finished;
        if (workers[i].fd < 0) {
            perror("open producer control");
            goto cleanup;
        }
        int rc = pthread_create(&threads[i], NULL, producer_main, &workers[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            goto cleanup;
        }
        ++started;
    }

    t0 = now_ns();
    atomic_store_explicit(&start, 1, memory_order_release);
    for (;;) {
        int rc = read_event(events, &ev);
        if (rc < 0) { invalid = 1; break; }
        if (rc == 0) {
            if (atomic_load_explicit(&finished, memory_order_acquire) == PRODUCERS)
                break; /* All writers done; FIFO observed empty. */
            struct pollfd pfd = { .fd = events, .events = POLLIN };
            int ready = poll(&pfd, 1, POLL_TIMEOUT_MS);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) { perror("poll"); invalid = 1; break; }
            if (ready > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                fprintf(stderr, "unexpected poll flags: %#x\n", pfd.revents);
                invalid = 1; break;
            }
            continue;
        }
        /* Any actual binary state transition toggles state regardless of writer. */
        if (ev.abi_version != THERMAL_UAPI_VERSION || ev.state > 1 ||
            ev.sequence < expected_seq ||
            ev.sequence > baseline_seq + PRODUCERS * WRITES_EACH ||
            ev.state != ((ev.sequence - baseline_seq) & UINT64_C(1)) ||
            ev.timestamp_ns < prior_ts) {
            fprintf(stderr, "invalid event: seq=%" PRIu64 " state=%" PRIu32
                    " expected_seq=%" PRIu64 "\n",
                    (uint64_t)ev.sequence, ev.state, expected_seq);
            invalid = 1;
            break;
        }
        internal_gaps += ev.sequence - expected_seq;
        expected_seq = ev.sequence + 1;
        prior_ts = ev.timestamp_ns;
        ++read_count;
        uint64_t observed = now_ns();
        if (observed < ev.timestamp_ns) { invalid = 1; break; }
        uint64_t delay = observed - ev.timestamp_ns;
        latency_sum += delay;
        if (delay > latency_max) latency_max = delay;
    }

    for (unsigned int i = 0; i < started; ++i) {
        int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(rc));
            invalid = 1;
        }
        successful_writes += workers[i].writes_ok;
        if (workers[i].error) invalid = 1;
    }
    started = 0;
    elapsed = now_ns() - t0;

    /* Final exclusive marker resolves how many writes actually transitioned.
     * Only writes from this test are allowed while it runs. */
    state.abi_version = THERMAL_UAPI_VERSION;
    if (ioctl(control, THERMAL_IOC_GET_STATE, &state) < 0 || state.state > 1) {
        perror("ioctl GET_STATE");
        invalid = 1;
        goto summary;
    }
    if (write_state(control, state.state ? '0' : '1') != 0 ||
        take_event(events, &ev) != 0) {
        invalid = 1;
        goto summary;
    }
    if (ev.abi_version != THERMAL_UAPI_VERSION ||
        ev.state != (state.state ^ 1U) || ev.sequence <= baseline_seq ||
        ev.timestamp_ns < prior_ts) {
        fprintf(stderr, "invalid final marker\n");
        invalid = 1;
        goto summary;
    }
    transitions = ev.sequence - baseline_seq - 1; /* Marker is not a producer write. */
    if (transitions > successful_writes || expected_seq > ev.sequence) invalid = 1;
    dropped = transitions >= read_count ? transitions - read_count : 0;
    if (transitions < read_count || dropped < internal_gaps ||
        read_count + dropped != transitions ||
        successful_writes != PRODUCERS * WRITES_EACH) invalid = 1;

summary:
    printf("E02 writers=%u writes=%u actual_transitions=%" PRIu64
           " consumed=%" PRIu64 " dropped=%" PRIu64 " errors=%d\n",
           PRODUCERS, successful_writes, transitions, read_count, dropped, invalid);
    printf("E02 idempotent_writes=%" PRIu64 " elapsed_ms=%.3f writes_per_sec=%.1f\n",
           successful_writes >= transitions ? (uint64_t)successful_writes - transitions : 0,
           elapsed / 1e6,
           elapsed ? (double)successful_writes * 1e9 / (double)elapsed : 0.0);
    printf("E02 observed_latency_us_avg=%.3f max=%.3f\n",
           read_count ? (double)latency_sum / (double)read_count / 1e3 : 0.0,
           (double)latency_max / 1e3);
    if (!invalid && transitions > 0 && read_count + dropped == transitions) {
        puts("E02 PASS (concurrent writers; transitions distinguished from writes)");
        result = EXIT_SUCCESS;
    } else {
        puts("E02 FAIL");
    }

cleanup:
    /* Handle early setup failures without leaving waiting workers behind. */
    atomic_store_explicit(&start, 1, memory_order_release);
    for (unsigned int i = 0; i < started; ++i)
        pthread_join(threads[i], NULL);
    for (unsigned int i = 0; i < PRODUCERS; ++i)
        if (workers[i].fd >= 0) close(workers[i].fd);
    if (events >= 0) close(events);
    if (control >= 0) close(control);
    return result;
}
