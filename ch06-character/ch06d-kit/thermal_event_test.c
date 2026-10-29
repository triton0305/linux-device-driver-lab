#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "thermal_uapi.h"

#define CORE "/dev/thermal_core"
#define EVENTS "/dev/thermal_events"
#define CAPACITY 8
#define TESTS 16

_Static_assert(sizeof(struct thermal_core_state) == 16, "thermal_core_state ABI mismatch");
_Static_assert(sizeof(struct thermal_core_event) == 24, "thermal_core_event ABI mismatch");
_Static_assert(offsetof(struct thermal_core_event, sequence) == 8, "event sequence offset mismatch");
_Static_assert(offsetof(struct thermal_core_event, timestamp_ns) == 16, "event timestamp offset mismatch");

static int core = -1, events = -1;
static unsigned int passed, failed;
static const char *reason;

#define CHECK(expr) do { if (!(expr)) { reason = #expr; return -1; } } while (0)

static void nap_ms(long ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) { }
}

static int set_state(unsigned int state)
{
    struct thermal_core_state request = { .abi_version = THERMAL_UAPI_VERSION, .state = state };
    return ioctl(core, THERMAL_IOC_SET_STATE, &request);
}

static int state_is(unsigned int want)
{
    struct thermal_core_state state = { 0 };
    return ioctl(core, THERMAL_IOC_GET_STATE, &state) == 0 &&
           state.abi_version == THERMAL_UAPI_VERSION && state.state == want &&
           state.reserved[0] == 0 && state.reserved[1] == 0;
}

static int pop(struct thermal_core_event *event)
{
    return read(events, event, sizeof(*event)) == (ssize_t)sizeof(*event) ? 0 : -1;
}

static int empty(void)
{
    struct thermal_core_event event;
    errno = 0;
    return read(events, &event, sizeof(event)) == -1 && errno == EAGAIN;
}

static int prepare(void)
{
    struct thermal_core_event event;
    int n = 0;
    if (ioctl(core, THERMAL_IOC_RESET) < 0) return -1;
    while (read(events, &event, sizeof(event)) == (ssize_t)sizeof(event)) {
        if (++n > 1000) return -1;
    }
    return errno == EAGAIN && state_is(0) ? 0 : -1;
}

static int valid(const struct thermal_core_event *e, unsigned int state)
{
    return e->abi_version == THERMAL_UAPI_VERSION && e->state == state &&
           e->sequence > 0 && e->timestamp_ns > 0;
}

static int produce(unsigned int state)
{
    return set_state(state) == 0 ? 0 : -1;
}

static int test01(void)
{
    CHECK(sizeof(struct thermal_core_state) == 16);
    CHECK(sizeof(struct thermal_core_event) == 24);
    CHECK(offsetof(struct thermal_core_event, abi_version) == 0);
    CHECK(offsetof(struct thermal_core_event, state) == 4);
    CHECK(offsetof(struct thermal_core_event, sequence) == 8);
    CHECK(offsetof(struct thermal_core_event, timestamp_ns) == 16);
    CHECK(THERMAL_UAPI_VERSION == 1);
    return 0;
}
static int test02(void)
{
    int fd = open(EVENTS, O_RDONLY | O_NONBLOCK);
    CHECK(fd >= 0);
    CHECK(close(fd) == 0);
    return 0;
}
static int test03(void)
{
    struct pollfd p = { .fd = events, .events = POLLIN };
    CHECK(prepare() == 0);
    CHECK(empty());
    CHECK(poll(&p, 1, 100) == 0);
    return 0;
}
static int test04(void)
{
    struct thermal_core_event e;
    CHECK(prepare() == 0);
    CHECK(produce(1) == 0);
    CHECK(pop(&e) == 0 && valid(&e, 1));
    CHECK(state_is(1));
    CHECK(empty());
    return 0;
}
static int test05(void)
{
    struct thermal_core_event a, b;
    CHECK(prepare() == 0);
    CHECK(produce(1) == 0);
    CHECK(pop(&a) == 0 && valid(&a, 1));
    CHECK(produce(1) == 0);
    CHECK(empty());
    CHECK(produce(0) == 0);
    CHECK(pop(&b) == 0 && valid(&b, 0));
    CHECK(b.sequence == a.sequence + 1);
    CHECK(empty());
    return 0;
}
static int test06(void)
{
    struct thermal_core_event a, b;
    CHECK(prepare() == 0);
    CHECK(write(core, "1", 1) == 1);
    CHECK(write(core, "0", 1) == 1);
    CHECK(pop(&a) == 0 && valid(&a, 1));
    CHECK(pop(&b) == 0 && valid(&b, 0));
    CHECK(b.sequence == a.sequence + 1 && empty());
    return 0;
}
static int test07(void)
{
    struct thermal_core_event a, b;
    CHECK(prepare() == 0);
    CHECK(produce(1) == 0);
    CHECK(ioctl(core, THERMAL_IOC_RESET) == 0);
    CHECK(ioctl(core, THERMAL_IOC_RESET) == 0);
    CHECK(pop(&a) == 0 && valid(&a, 1));
    CHECK(pop(&b) == 0 && valid(&b, 0));
    CHECK(b.sequence == a.sequence + 1 && empty());
    return 0;
}
static int test08(void)
{
    struct thermal_core_event a, b;
    long pagesize = sysconf(_SC_PAGESIZE);
    void *invalid;
    struct thermal_core_state bad = { .abi_version = THERMAL_UAPI_VERSION, .state = 2 };
    struct thermal_core_state badver = { .abi_version = 999, .state = 1 };
    struct thermal_core_state reserved = { .abi_version = THERMAL_UAPI_VERSION, .state = 1, .reserved = {1, 0} };
    CHECK(prepare() == 0);
    CHECK(pagesize > 0);
    invalid = mmap(NULL, (size_t)pagesize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(invalid != MAP_FAILED);
    CHECK(produce(1) == 0 && pop(&a) == 0);
    errno = 0; CHECK(write(core, "x", 1) == -1 && errno == EINVAL);
    errno = 0; CHECK(write(core, "11", 2) == -1 && errno == EINVAL);
    errno = 0; CHECK(ioctl(core, THERMAL_IOC_SET_STATE, &bad) == -1 && errno == EINVAL);
    errno = 0; CHECK(ioctl(core, THERMAL_IOC_SET_STATE, &badver) == -1 && errno == EINVAL);
    errno = 0; CHECK(ioctl(core, THERMAL_IOC_SET_STATE, &reserved) == -1 && errno == EINVAL);
    errno = 0; CHECK(ioctl(core, THERMAL_IOC_SET_STATE, invalid) == -1 && errno == EFAULT);
    errno = 0; CHECK(write(core, invalid, 1) == -1 && errno == EFAULT);
    CHECK(munmap(invalid, (size_t)pagesize) == 0);
    CHECK(state_is(1) && empty());
    CHECK(produce(0) == 0 && pop(&b) == 0);
    CHECK(b.sequence == a.sequence + 1 && valid(&b, 0));
    return 0;
}
static int test09(void)
{
    struct thermal_core_event a, b;
    unsigned char buf[64] = { 0 };
    CHECK(prepare() == 0);
    CHECK(produce(1) == 0);
    CHECK(read(events, buf, 0) == 0);
    errno = 0; CHECK(read(events, buf, 23) == -1 && errno == EINVAL);
    CHECK(read(events, &a, 24) == 24 && valid(&a, 1));
    CHECK(produce(0) == 0);
    CHECK(read(events, buf, sizeof(buf)) == 24);
    memcpy(&b, buf, sizeof(b));
    CHECK(valid(&b, 0) && b.sequence == a.sequence + 1 && empty());
    return 0;
}
static int test10(void)
{
    struct thermal_core_event e;
    void *bad;
    long pagesize = sysconf(_SC_PAGESIZE);
    CHECK(prepare() == 0);
    CHECK(pagesize > 0);
    bad = mmap(NULL, (size_t)pagesize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(bad != MAP_FAILED);
    CHECK(produce(1) == 0);
    errno = 0;
    ssize_t n = read(events, bad, sizeof(e));
    int saved = errno;
    CHECK(munmap(bad, (size_t)pagesize) == 0);
    CHECK(n == -1 && saved == EFAULT);
    CHECK(pop(&e) == 0 && valid(&e, 1) && empty());
    return 0;
}
static int test11(void)
{
    struct thermal_core_event e, prev = { 0 };
    CHECK(prepare() == 0);
    for (unsigned int i = 0; i < 6; i++) CHECK(produce((i + 1) % 2) == 0);
    for (unsigned int i = 0; i < 6; i++) {
        CHECK(pop(&e) == 0 && valid(&e, (i + 1) % 2));
        if (i > 0) CHECK(e.sequence == prev.sequence + 1 && e.timestamp_ns >= prev.timestamp_ns);
        prev = e;
    }
    CHECK(empty());
    return 0;
}
static int test12(void)
{
    struct thermal_core_event e, prev = { 0 }, first = { 0 };
    CHECK(prepare() == 0);
    for (unsigned int i = 0; i < 10; i++) CHECK(produce((i + 1) % 2) == 0);
    for (unsigned int i = 0; i < CAPACITY; i++) {
        CHECK(pop(&e) == 0 && valid(&e, (i + 3) % 2));
        if (i == 0) first = e;
        else CHECK(e.sequence == prev.sequence + 1);
        prev = e;
    }
    CHECK(prev.sequence == first.sequence + 7 && empty());
    return 0;
}

struct child_result { ssize_t n; int err; struct thermal_core_event event; };
static void noop(int signum) { (void)signum; }
static int wait_child(pid_t pid, int *status, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid) return 0;
        if (r == -1) return -1;
        nap_ms(10);
    }
    kill(pid, SIGKILL);
    (void)waitpid(pid, status, 0);
    return -1;
}
static int read_exact(int fd, void *buffer, size_t len)
{
    size_t n = 0;
    while (n < len) {
        ssize_t x = read(fd, (char *)buffer + n, len - n);
        if (x > 0) n += (size_t)x;
        else if (x < 0 && errno == EINTR) continue;
        else return -1;
    }
    return 0;
}
static int run_wait_child(int signal_test)
{
    int ready[2] = {-1, -1}, data[2] = {-1, -1}, status = 0, rc = -1;
    pid_t pid = -1;
    int reaped = 0;
    struct child_result result = { 0 };
    char marker;
    struct pollfd pfd;

    if (prepare() != 0 || pipe(ready) != 0 || pipe(data) != 0)
        goto cleanup;
    pid = fork();
    if (pid < 0) goto cleanup;
    if (pid == 0) {
        int fd;
        struct sigaction sa = { 0 };
        close(ready[0]); close(data[0]);
        fd = open(EVENTS, O_RDONLY); /* blocking FD, not the parent's O_NONBLOCK FD */
        if (fd < 0) _exit(90);
        if (signal_test) {
            sa.sa_handler = noop;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0; /* NO SA_RESTART */
            if (sigaction(SIGUSR1, &sa, NULL) < 0) _exit(91);
        }
        if (write(ready[1], "R", 1) != 1) _exit(92);
        errno = 0;
        result.n = read(fd, &result.event, sizeof(result.event));
        result.err = errno;
        if (write(data[1], &result, sizeof(result)) != (ssize_t)sizeof(result)) _exit(93);
        close(fd);
        close(ready[1]); close(data[1]);
        _exit(0);
    }
    close(ready[1]); ready[1] = -1;
    close(data[1]); data[1] = -1;
    pfd.fd = ready[0]; pfd.events = POLLIN;
    if (poll(&pfd, 1, 1000) != 1 || read_exact(ready[0], &marker, 1) != 0)
        goto cleanup;
    close(ready[0]); ready[0] = -1;
    nap_ms(100);
    if (waitpid(pid, &status, WNOHANG) != 0) goto cleanup;
    if (signal_test) {
        if (kill(pid, SIGUSR1) != 0) goto cleanup;
    } else if (produce(1) != 0) goto cleanup;
    if (wait_child(pid, &status, 2000) != 0) { reaped = 1; goto cleanup; }
    reaped = 1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) goto cleanup;
    if (read_exact(data[0], &result, sizeof(result)) != 0) goto cleanup;
    if (signal_test) {
        if (result.n != -1 || result.err != EINTR || !empty()) goto cleanup;
    } else if (result.n != sizeof(result.event) || !valid(&result.event, 1) || !empty())
        goto cleanup;
    rc = 0;
cleanup:
    if (pid > 0 && !reaped) {
        kill(pid, SIGKILL);
        (void)waitpid(pid, &status, 0);
    }
    for (int i = 0; i < 2; i++) {
        if (ready[i] >= 0) close(ready[i]);
        if (data[i] >= 0) close(data[i]);
    }
    if (rc != 0) reason = signal_test ? "blocking signal test failed" : "blocking wake test failed";
    return rc;
}
static int test13(void) { return run_wait_child(0); }
static int test14(void) { return run_wait_child(1); }
static int test15(void)
{
    struct pollfd p = { .fd = events, .events = POLLIN | POLLRDNORM };
    struct thermal_core_event e;
    CHECK(prepare() == 0);
    CHECK(poll(&p, 1, 80) == 0);
    CHECK(produce(1) == 0);
    CHECK(poll(&p, 1, 500) == 1);
    CHECK((p.revents & (POLLIN | POLLRDNORM)) != 0);
    CHECK(poll(&p, 1, 0) == 1); /* poll does not consume */
    CHECK(pop(&e) == 0 && valid(&e, 1));
    CHECK(poll(&p, 1, 80) == 0);
    return 0;
}
struct report { int error; unsigned int count; struct thermal_core_event events[CAPACITY]; };
static int test16(void)
{
    int go[2] = {-1, -1}, out[2][2] = {{-1, -1}, {-1, -1}};
    int status, rc = -1;
    pid_t children[2] = {-1, -1};
    int reaped[2] = {0, 0};
    struct report reports[2];
    struct thermal_core_event all[CAPACITY];
    unsigned int found = 0;

    if (prepare() != 0) goto cleanup;
    for (unsigned int i = 0; i < CAPACITY; i++)
        if (produce((i + 1) % 2) != 0) goto cleanup;
    if (pipe(go) != 0) goto cleanup;
    for (int i = 0; i < 2; i++) {
        if (pipe(out[i]) != 0) goto cleanup;
        children[i] = fork();
        if (children[i] < 0) goto cleanup;
        if (children[i] == 0) {
            struct report r = { 0 };
            char marker;
            close(go[1]); close(out[i][0]);
            if (read_exact(go[0], &marker, 1) != 0) _exit(70);
            for (;;) {
                struct thermal_core_event e;
                ssize_t n = read(events, &e, sizeof(e));
                if (n == -1 && errno == EAGAIN) break;
                if (n != sizeof(e) || r.count >= CAPACITY) { r.error = 1; break; }
                r.events[r.count++] = e;
            }
            if (write(out[i][1], &r, sizeof(r)) != (ssize_t)sizeof(r)) _exit(71);
            close(out[i][1]); close(go[0]); _exit(0);
        }
        close(out[i][1]); out[i][1] = -1;
    }
    close(go[0]); go[0] = -1;
    if (write(go[1], "AB", 2) != 2) goto cleanup;
    close(go[1]); go[1] = -1;
    for (int i = 0; i < 2; i++) {
        if (wait_child(children[i], &status, 2000) != 0) { reaped[i] = 1; goto cleanup; }
        reaped[i] = 1;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) goto cleanup;
        if (read_exact(out[i][0], &reports[i], sizeof(reports[i])) != 0) goto cleanup;
        close(out[i][0]); out[i][0] = -1;
        if (reports[i].error || reports[i].count > CAPACITY) goto cleanup;
        for (unsigned int j = 0; j < reports[i].count; j++) {
            if (found >= CAPACITY) goto cleanup;
            all[found++] = reports[i].events[j];
        }
    }
    if (found != CAPACITY || !empty()) goto cleanup;
    for (unsigned int i = 0; i < found; i++) {
        for (unsigned int j = i + 1; j < found; j++) {
            if (all[j].sequence < all[i].sequence) {
                struct thermal_core_event tmp = all[i]; all[i] = all[j]; all[j] = tmp;
            }
        }
    }
    for (unsigned int i = 0; i < CAPACITY; i++) {
        if (!valid(&all[i], (i + 1) % 2)) goto cleanup;
        if (i && all[i].sequence != all[i - 1].sequence + 1) goto cleanup;
    }
    rc = 0;
cleanup:
    for (int i = 0; i < 2; i++) {
        if (children[i] > 0 && !reaped[i]) {
            kill(children[i], SIGKILL);
            (void)waitpid(children[i], &status, 0);
        }
        for (int j = 0; j < 2; j++) if (out[i][j] >= 0) close(out[i][j]);
        if (go[i] >= 0) close(go[i]);
    }
    if (rc != 0) reason = "competing reader event integrity failed";
    return rc;
}

struct testcase { const char *name; int (*fn)(void); };
static const struct testcase tests[] = {
    { "D01 ABI layout", test01 }, { "D02 event device open", test02 },
    { "D03 empty queue", test03 }, { "D04 transition event", test04 },
    { "D05 idempotent no-event", test05 }, { "D06 legacy write", test06 },
    { "D07 ioctl SET/RESET", test07 }, { "D08 rejected inputs", test08 },
    { "D09 read sizes", test09 }, { "D10 EFAULT non-consumption", test10 },
    { "D11 FIFO", test11 }, { "D12 overflow drop-oldest", test12 },
    { "D13 blocking wakeup", test13 }, { "D14 signal interrupt", test14 },
    { "D15 poll readiness", test15 }, { "D16 competing readers", test16 },
};

int main(void)
{
    core = open(CORE, O_RDWR);
    events = open(EVENTS, O_RDONLY | O_NONBLOCK);
    if (core < 0 || events < 0) {
        perror("open thermal devices");
        fprintf(stderr, "Load the 06D module; no other process may consume events.\n");
        if (events >= 0) close(events);
        if (core >= 0) close(core);
        return 2;
    }
    for (unsigned int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        reason = "unspecified failure";
        if (tests[i].fn() == 0) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s: %s (errno=%d: %s)\n", tests[i].name,
                   reason, errno, strerror(errno));
            failed++;
            /* state cleanup before next case */
            if (prepare() != 0) {
                fprintf(stderr, "[FATAL] unable to reset test environment\n");
                break;
            }
        }
        fflush(stdout);
    }
    printf("SUMMARY PASS=%u FAIL=%u TOTAL=%u/%d\n", passed, failed, passed + failed, TESTS);
    close(events); close(core);
    return failed == 0 && passed == TESTS ? 0 : 1;
}
