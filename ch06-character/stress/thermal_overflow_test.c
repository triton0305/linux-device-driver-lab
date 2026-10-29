// SPDX-License-Identifier: GPL-2.0-only
// Chapter 06E / E04: deterministic drop-oldest and sequence-gap verification.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "thermal_uapi.h"

#define CORE "/dev/thermal_core"
#define EVENTS "/dev/thermal_events"
#define CAPACITY 8U

static int core = -1, events = -1;
static unsigned int passed, failed;

static int write_state(unsigned int state)
{
    char c = state ? '1' : '0';
    ssize_t n = write(core, &c, 1);
    if (n != 1) {
        if (n < 0) perror("write core");
        else fprintf(stderr, "write returned %zd instead of 1\n", n);
        return -1;
    }
    return 0;
}

/* Nonblocking: 1 = event, 0 = empty, -1 = unexpected failure. */
static int pop(struct thermal_core_event *ev)
{
    ssize_t n = read(events, ev, sizeof(*ev));
    if (n == (ssize_t)sizeof(*ev)) return 1;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    if (n < 0) perror("read events");
    else fprintf(stderr, "read returned %zd instead of %zu\n", n, sizeof(*ev));
    return -1;
}

static int drain(void)
{
    struct thermal_core_event ev;
    int rc;
    unsigned int limit = 0;
    while ((rc = pop(&ev)) == 1) {
        if (++limit > 100000U) {
            fprintf(stderr, "drain exceeded safety limit\n");
            return -1;
        }
    }
    return rc == 0 ? 0 : -1;
}

/* Reset state and drain leftovers, then seed a known first transition.
 * The seed gives an observed baseline sequence even if module was used before.
 * The seed itself is consumed and is NOT part of measured test events.
 */
static int setup(uint64_t *base)
{
    struct thermal_core_event seed;
    if (ioctl(core, THERMAL_IOC_RESET) < 0) {
        perror("ioctl RESET");
        return -1;
    }
    if (drain() < 0 || write_state(1) < 0) return -1;
    if (pop(&seed) != 1 || seed.abi_version != THERMAL_UAPI_VERSION || seed.state != 1) {
        fprintf(stderr, "failed to obtain clean seed event\n");
        return -1;
    }
    if (pop(&seed) != 0) {
        fprintf(stderr, "queue not empty after seed consumption\n");
        return -1;
    }
    *base = seed.sequence;
    return 0;
}

/* After the seed (state=1), transition i=1 -> state 0, i=2 -> state 1. */
static int produce(uint32_t first, uint32_t last)
{
    for (uint32_t i = first; i <= last; ++i) {
        if (write_state((i & 1U) == 0U) < 0) return -1;
    }
    return 0;
}

static int verify_range(uint64_t base, uint32_t first, uint32_t last,
                        uint64_t *last_stamp)
{
    for (uint32_t i = first; i <= last; ++i) {
        struct thermal_core_event ev;
        int rc = pop(&ev);
        if (rc != 1) {
            fprintf(stderr, "missing event index=%" PRIu32 " (rc=%d)\n", i, rc);
            return -1;
        }
        if (ev.abi_version != THERMAL_UAPI_VERSION ||
            ev.sequence != base + i ||
            ev.state != ((i & 1U) == 0U) ||
            ev.timestamp_ns == 0 ||
            (last_stamp && *last_stamp && ev.timestamp_ns < *last_stamp)) {
            fprintf(stderr, "bad event index=%" PRIu32 " sequence=%" PRIu64
                    " expected=%" PRIu64 " state=%u ts=%" PRIu64 "\n",
                    i, (uint64_t)ev.sequence, base + i,
                    (unsigned)ev.state, (uint64_t)ev.timestamp_ns);
            return -1;
        }
        if (last_stamp) *last_stamp = ev.timestamp_ns;
    }
    return 0;
}

static int expect_empty(void)
{
    struct thermal_core_event ev;
    int rc = pop(&ev);
    if (rc != 0) {
        fprintf(stderr, "expected EAGAIN (empty queue), rc=%d\n", rc);
        return -1;
    }
    return 0;
}

static int test_suffix(uint32_t produced)
{
    uint64_t base = 0, stamp = 0;
    uint32_t first = produced > CAPACITY ? produced - CAPACITY + 1U : 1U;
    if (setup(&base) < 0 || produce(1, produced) < 0 ||
        verify_range(base, first, produced, &stamp) < 0 || expect_empty() < 0)
        return -1;
    printf("E04 events=%" PRIu32 " kept=%" PRIu32 " dropped=%" PRIu32
           " retained_index=%" PRIu32 "..%" PRIu32 " baseline_seq=%" PRIu64 "\n",
           produced, produced - first + 1U, first - 1U, first, produced, base);
    return 0;
}

/* Read part of retained suffix, then append events: preserved unread tail
 * plus the new events must remain in FIFO order without unexpected loss. */
static int test_partial_drain(void)
{
    uint64_t base = 0, stamp = 0;
    if (setup(&base) < 0 || produce(1, 20) < 0 ||
        verify_range(base, 13, 16, &stamp) < 0 ||
        produce(21, 24) < 0 ||
        verify_range(base, 17, 24, &stamp) < 0 || expect_empty() < 0)
        return -1;
    printf("E04 partial-drain: consumed=4 appended=4 retained_index=17..24\n");
    return 0;
}

static void run(const char *name, int (*test)(void))
{
    int rc = test();
    printf("[%s] %s\n", rc == 0 ? "PASS" : "FAIL", name);
    if (rc == 0) ++passed;
    else ++failed;
    fflush(stdout);
}

static int case8(void) { return test_suffix(8); }
static int case9(void) { return test_suffix(9); }
static int case20(void) { return test_suffix(20); }
static int case1000(void) { return test_suffix(1000); }

int main(void)
{
    if (sizeof(struct thermal_core_event) != 24) {
        fprintf(stderr, "unexpected event ABI size %zu\n", sizeof(struct thermal_core_event));
        return 2;
    }
    core = open(CORE, O_RDWR);
    events = open(EVENTS, O_RDONLY | O_NONBLOCK);
    if (core < 0 || events < 0) {
        perror("open thermal devices");
        if (events >= 0) close(events);
        if (core >= 0) close(core);
        return 2;
    }
    puts("E04 requires exclusive use of /dev/thermal_core and /dev/thermal_events");
    run("E04-1 exact capacity (8 events)", case8);
    run("E04-2 boundary overflow (9 events)", case9);
    run("E04-3 drop-oldest suffix (20 events)", case20);
    run("E04-4 sustained overwrite (1000 events)", case1000);
    run("E04-5 partial drain and refill", test_partial_drain);
    printf("E04 SUMMARY PASS=%u FAIL=%u TOTAL=%u/5\n", passed, failed, passed + failed);
    close(events);
    close(core);
    return passed == 5 && failed == 0 ? 0 : 1;
}
