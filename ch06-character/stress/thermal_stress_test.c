// SPDX-License-Identifier: GPL-2.0-only
// Chapter 06E / E01: one producer, one consumer, 10,000 transitions.
// Build: cc -O2 -Wall -Wextra -Werror -pthread -I../include thermal_stress_test.c -o thermal_stress_test
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "thermal_uapi.h"

#define TRANSITIONS 10000U
#define POLL_TIMEOUT_MS 100

struct producer_context {
    int fd;
    atomic_int done;
    unsigned int produced;
    int error;
};

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

/* Return 1 for one event, 0 for EAGAIN, -1 for an unexpected failure. */
static int consume_one(int fd, struct thermal_core_event *ev)
{
    ssize_t n = read(fd, ev, sizeof(*ev));
    if (n == (ssize_t)sizeof(*ev))
        return 1;
    if (n < 0 && errno == EAGAIN)
        return 0;
    if (n < 0)
        perror("read /dev/thermal_events");
    else
        fprintf(stderr, "invalid event read length: %zd\n", n);
    return -1;
}

static int write_state(int fd, char state)
{
    ssize_t n = write(fd, &state, 1);
    if (n == 1)
        return 0;
    if (n < 0)
        perror("write /dev/thermal_core");
    else
        fprintf(stderr, "short state write: %zd\n", n);
    return -1;
}

static void *producer_main(void *arg)
{
    struct producer_context *p = arg;
    for (unsigned int i = 0; i < TRANSITIONS; ++i) {
        /* Baseline ends in state 0. Every write now changes the state. */
        if (write_state(p->fd, (i & 1U) ? '0' : '1') != 0) {
            p->error = 1;
            break;
        }
        ++p->produced;
    }
    atomic_store_explicit(&p->done, 1, memory_order_release);
    return NULL;
}

int main(void)
{
    int control = -1, events = -1, result = EXIT_FAILURE;
    pthread_t producer;
    int thread_started = 0;
    struct producer_context p = { .fd = -1 };
    struct thermal_core_event ev = {0};
    uint64_t baseline = 0, expected = 0, previous_timestamp = 0;
    uint64_t consumed = 0, gaps = 0, latency_total = 0, latency_max = 0;
    uint64_t start_ns = 0, elapsed_ns = 0;
    int invalid = 0;

    if (sizeof(ev) != 24) {
        fprintf(stderr, "unexpected ABI event size: %zu\n", sizeof(ev));
        return EXIT_FAILURE;
    }
    control = open("/dev/thermal_core", O_WRONLY | O_CLOEXEC);
    events = open("/dev/thermal_events", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (control < 0 || events < 0) {
        perror("open thermal device");
        goto out;
    }

    /* E01 assumes exclusive use of this virtual device during the test. */
    if (ioctl(control, THERMAL_IOC_RESET) < 0) {
        perror("ioctl RESET");
        goto out;
    }
    /* Discard events left over from 06D / regression exercises. */
    for (;;) {
        int rc = consume_one(events, &ev);
        if (rc == 0) break;
        if (rc < 0) goto out;
    }

    /* Get an exact sequence baseline, ending with state == 0. */
    if (write_state(control, '1') != 0 || consume_one(events, &ev) != 1 ||
        ev.abi_version != THERMAL_UAPI_VERSION || ev.state != 1) {
        fprintf(stderr, "baseline state=1 event failed\n");
        goto out;
    }
    if (write_state(control, '0') != 0 || consume_one(events, &ev) != 1 ||
        ev.abi_version != THERMAL_UAPI_VERSION || ev.state != 0) {
        fprintf(stderr, "baseline state=0 event failed\n");
        goto out;
    }
    baseline = ev.sequence;
    expected = baseline + 1;
    previous_timestamp = ev.timestamp_ns;
    p.fd = control;
    atomic_init(&p.done, 0);

    start_ns = monotonic_ns();
    int rc = pthread_create(&producer, NULL, producer_main, &p);
    if (rc != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(rc));
        goto out;
    }
    thread_started = 1;

    for (;;) {
        int status = consume_one(events, &ev);
        if (status < 0) { invalid = 1; break; }
        if (status == 0) {
            if (atomic_load_explicit(&p.done, memory_order_acquire))
                break; /* FIFO empty and producer has finished. */
            struct pollfd pollfd = { .fd = events, .events = POLLIN };
            int ready = poll(&pollfd, 1, POLL_TIMEOUT_MS);
            if (ready < 0 && errno == EINTR)
                continue;
            if (ready < 0) { perror("poll"); invalid = 1; break; }
            if (ready > 0 && (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                fprintf(stderr, "unexpected poll revents: %#x\n", pollfd.revents);
                invalid = 1; break;
            }
            continue;
        }

        if (ev.abi_version != THERMAL_UAPI_VERSION ||
            ev.state > 1 || ev.sequence < expected ||
            ev.sequence > baseline + TRANSITIONS ||
            ev.state != ((ev.sequence - baseline) & UINT64_C(1)) ||
            ev.timestamp_ns < previous_timestamp) {
            fprintf(stderr, "invalid event: ver=%" PRIu32 " state=%" PRIu32
                    " seq=%" PRIu64 " expected=%" PRIu64 " ts=%" PRIu64 "\n",
                    ev.abi_version, ev.state, (uint64_t)ev.sequence,
                    expected, (uint64_t)ev.timestamp_ns);
            invalid = 1; break;
        }
        gaps += ev.sequence - expected; /* Allowed drop-oldest overflow. */
        expected = ev.sequence + 1;
        previous_timestamp = ev.timestamp_ns;
        ++consumed;
        uint64_t now_ns = monotonic_ns();
        if (now_ns < ev.timestamp_ns) { invalid = 1; break; }
        uint64_t delay_ns = now_ns - ev.timestamp_ns;
        latency_total += delay_ns;
        if (delay_ns > latency_max) latency_max = delay_ns;
    }

    if (thread_started) {
        int join_rc = pthread_join(producer, NULL);
        thread_started = 0;
        if (join_rc != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(join_rc));
            invalid = 1;
        }
    }
    elapsed_ns = monotonic_ns() - start_ns;
    if (p.error || p.produced != TRANSITIONS || invalid)
        goto summary;

    /* Account for events dropped after the final observed event. */
    gaps += (baseline + p.produced + 1) - expected;
    if (consumed + gaps != p.produced) invalid = 1;

summary:
    printf("E01 produced=%u consumed=%" PRIu64 " dropped=%" PRIu64 " errors=%d\n",
           p.produced, consumed, gaps, invalid || p.error);
    printf("E01 elapsed_ms=%.3f throughput_writes_per_sec=%.1f\n",
           elapsed_ns / 1e6,
           elapsed_ns ? (double)p.produced * 1e9 / (double)elapsed_ns : 0.0);
    printf("E01 userspace_observed_latency_us_avg=%.3f max=%.3f\n",
           consumed ? (double)latency_total / (double)consumed / 1e3 : 0.0,
           (double)latency_max / 1e3);
    if (!invalid && !p.error && p.produced == TRANSITIONS &&
        consumed + gaps == p.produced) {
        puts("E01 PASS (ordering/integrity; overflow drops are permitted)");
        result = EXIT_SUCCESS;
    } else {
        puts("E01 FAIL");
    }

out:
    if (thread_started) pthread_join(producer, NULL);
    if (events >= 0) close(events);
    if (control >= 0) close(control);
    return result;
}
