#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define DEV "/dev/thermal_core"

static unsigned int tests;
static unsigned int failures;

static void check(const char *name, int ok, const char *detail)
{
    ++tests;
    printf("[%s] %-30s %s\n", ok ? "PASS" : "FAIL", name,
           detail ? detail : "");
    if (!ok)
        ++failures;
}

static void check_io(const char *name, ssize_t actual, int actual_errno,
                     ssize_t expected, int expected_errno)
{
    char detail[128];
    int ok = actual == expected &&
             (expected >= 0 || actual_errno == expected_errno);

    snprintf(detail, sizeof(detail), "got=%zd errno=%d; expected=%zd errno=%d",
             actual, actual_errno, expected, expected_errno);
    check(name, ok, detail);
}

static void check_state(const char *name, char expected)
{
    char buf[4] = {0};
    int fd = open(DEV, O_RDONLY | O_CLOEXEC);
    ssize_t n = -1;
    int err = 0;
    char detail[128];

    if (fd >= 0) {
        errno = 0;
        n = read(fd, buf, sizeof(buf));
        err = errno;
        close(fd);
    } else {
        err = errno;
    }
    snprintf(detail, sizeof(detail), "read=%zd errno=%d state=%c",
             n, err, n > 0 ? buf[0] : '?');
    check(name, fd >= 0 && n == 2 && buf[0] == expected &&
          buf[1] == '\n', detail);
}

static void check_partial_reads(void)
{
    char b[8] = {0};
    int fd = open(DEV, O_RDONLY | O_CLOEXEC);
    ssize_t n;
    int err;

    if (fd < 0) {
        check("open for partial reads", 0, strerror(errno));
        return;
    }

    errno = 0;
    n = read(fd, b, 0);
    err = errno;
    check_io("read count=0", n, err, 0, 0);

    errno = 0;
    n = read(fd, b, 1);
    err = errno;
    check("read first byte", n == 1 && b[0] == '0',
          n == 1 ? "got='0' expected='0'" : "unexpected count or data");

    errno = 0;
    n = read(fd, b, 1);
    err = errno;
    check("read second byte", n == 1 && b[0] == '\n',
          n == 1 ? "got=LF expected=LF" : "unexpected count or data");

    errno = 0;
    n = read(fd, b, 1);
    err = errno;
    check_io("read EOF", n, err, 0, 0);
    close(fd);

    fd = open(DEV, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        check("reopen for large read", 0, strerror(errno));
        return;
    }
    errno = 0;
    n = read(fd, b, sizeof(b));
    check("read count>data", n == 2 && b[0] == '0' && b[1] == '\n',
          "expect 2 bytes: 0 + LF");
    close(fd);
}

static void check_faults(int wr_fd)
{
    long page_size = sysconf(_SC_PAGESIZE);
    void *bad;
    ssize_t n;
    int err;
    int rd_fd;

    if (page_size <= 0) {
        check("EFAULT setup", 0, "sysconf(_SC_PAGESIZE) failed");
        return;
    }

    /* Mapped page with no user read/write permissions. The app never
       dereferences it; only the kernel's safe uaccess copy sees it. */
    bad = mmap(NULL, (size_t)page_size, PROT_NONE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bad == MAP_FAILED) {
        check("EFAULT setup", 0, strerror(errno));
        return;
    }

    errno = 0;
    n = write(wr_fd, bad, 1);
    err = errno;
    check_io("write protected page", n, err, -1, EFAULT);
    check_state("state after EFAULT", '0');

    rd_fd = open(DEV, O_RDONLY | O_CLOEXEC);
    if (rd_fd < 0) {
        check("open for read EFAULT", 0, strerror(errno));
    } else {
        errno = 0;
        n = read(rd_fd, bad, 1);
        err = errno;
        check_io("read protected page", n, err, -1, EFAULT);
        close(rd_fd);
    }

    munmap(bad, (size_t)page_size);
}

int main(void)
{
    int fd = open(DEV, O_WRONLY | O_CLOEXEC);
    ssize_t n;
    int err;

    if (fd < 0) {
        fprintf(stderr, "[FATAL] open %s: %s\n", DEV, strerror(errno));
        return 2;
    }

    errno = 0;
    n = write(fd, "0", 1);
    err = errno;
    check_io("write '0'", n, err, 1, 0);
    check_state("state after '0'", '0');

    errno = 0;
    n = write(fd, "1", 1);
    err = errno;
    check_io("write '1'", n, err, 1, 0);
    check_state("state after '1'", '1');

    errno = 0;
    n = write(fd, "12", 2);
    err = errno;
    check_io("write length=2", n, err, -1, EINVAL);
    check_state("state after length error", '1');

    errno = 0;
    n = write(fd, "x", 1);
    err = errno;
    check_io("write invalid 'x'", n, err, -1, EINVAL);
    check_state("state after invalid cmd", '1');

    errno = 0;
    n = write(fd, "", 0);
    err = errno;
    check_io("write count=0", n, err, 0, 0);
    check_state("state after empty write", '1');

    errno = 0;
    n = write(fd, "0", 1);
    err = errno;
    check_io("restore state '0'", n, err, 1, 0);
    check_partial_reads();
    check_faults(fd);
    close(fd);

    printf("\nSUMMARY: %u/%u PASS, %u FAIL\n", tests - failures,
           tests, failures);
    return failures ? 1 : 0;
}
