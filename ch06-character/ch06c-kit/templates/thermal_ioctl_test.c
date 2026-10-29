#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "thermal_uapi.h"

#define DEVICE "/dev/thermal_core"

static unsigned int total, failed;

static void verify(const char *label, int pass, const char *details)
{
    ++total;
    if (!pass) ++failed;
    printf("[%s] %-29s %s\n", pass ? "PASS" : "FAIL", label, details);
}

static void check_result(const char *label, int result, int err,
                         int expected, int expected_errno)
{
    char message[128];
    int pass = result == expected &&
               (expected >= 0 || err == expected_errno);
    snprintf(message, sizeof(message), "got=%d errno=%d; expected=%d errno=%d",
             result, err, expected, expected_errno);
    verify(label, pass, message);
}

static int do_ioctl(int fd, unsigned long cmd, void *arg, int *err)
{
    int ret;
    errno = 0;
    ret = ioctl(fd, cmd, arg);
    *err = errno;
    return ret;
}

static int check_state_ioctl(int fd, unsigned int expected, const char *name)
{
    struct thermal_core_state st = {0};
    char detail[128];
    int err, r = do_ioctl(fd, THERMAL_IOC_GET_STATE, &st, &err);
    int ok = r == 0 && err == 0 &&
             st.abi_version == THERMAL_UAPI_VERSION &&
             st.state == expected && !st.reserved[0] && !st.reserved[1];
    snprintf(detail, sizeof(detail), "ret=%d errno=%d ver=%u state=%u reserved=%u,%u",
             r, err, st.abi_version, st.state,
             st.reserved[0], st.reserved[1]);
    verify(name, ok, detail);
    return ok;
}

static void check_legacy_read(int fd, char expected, const char *name)
{
    int rd = open(DEVICE, O_RDONLY | O_CLOEXEC);
    char buff[4] = {0};
    ssize_t n = -1;
    char detail[100];
    int err = 0;

    (void)fd;
    if (rd >= 0) {
        errno = 0;
        n = read(rd, buff, sizeof(buff));
        err = errno;
        close(rd);
    } else {
        err = errno;
    }
    snprintf(detail, sizeof(detail), "read=%zd errno=%d bytes=%02x,%02x",
             n, err, (unsigned char)buff[0], (unsigned char)buff[1]);
    verify(name, rd >= 0 && n == 2 && buff[0] == expected &&
           buff[1] == '\n', detail);
}

int main(void)
{
    int fd = open(DEVICE, O_RDWR | O_CLOEXEC);
    struct thermal_core_state req = { .abi_version = THERMAL_UAPI_VERSION };
    int r, err;
    long pagesz;
    void *protected_page;

    if (fd < 0) {
        fprintf(stderr, "[FATAL] open %s: %s\n", DEVICE, strerror(errno));
        return 2;
    }

    verify("fixed UAPI layout", sizeof(req) == 16,
           "struct thermal_core_state size must be 16");

    r = do_ioctl(fd, THERMAL_IOC_RESET, NULL, &err);
    check_result("RESET", r, err, 0, 0);
    check_state_ioctl(fd, 0, "GET after RESET");
    check_legacy_read(fd, '0', "legacy read after RESET");

    req.state = 1;
    r = do_ioctl(fd, THERMAL_IOC_SET_STATE, &req, &err);
    check_result("SET state=1", r, err, 0, 0);
    check_state_ioctl(fd, 1, "GET state=1");
    check_legacy_read(fd, '1', "legacy read state=1");

    req.state = 2;
    r = do_ioctl(fd, THERMAL_IOC_SET_STATE, &req, &err);
    check_result("SET invalid state=2", r, err, -1, EINVAL);
    check_state_ioctl(fd, 1, "state after invalid value");

    req.state = 0;
    req.abi_version = THERMAL_UAPI_VERSION + 1;
    r = do_ioctl(fd, THERMAL_IOC_SET_STATE, &req, &err);
    check_result("SET wrong ABI version", r, err, -1, EINVAL);
    check_state_ioctl(fd, 1, "state after wrong version");

    req.abi_version = THERMAL_UAPI_VERSION;
    req.reserved[0] = 1;
    r = do_ioctl(fd, THERMAL_IOC_SET_STATE, &req, &err);
    check_result("SET nonzero reserved", r, err, -1, EINVAL);
    check_state_ioctl(fd, 1, "state after reserved error");
    req.reserved[0] = 0;

    r = do_ioctl(fd, _IO('X', 0x7e), NULL, &err);
    check_result("unknown ioctl", r, err, -1, ENOTTY);

    r = do_ioctl(fd, _IOR(THERMAL_IOC_MAGIC, 0x01, __u32), &req, &err);
    check_result("wrong ioctl size", r, err, -1, ENOTTY);
    check_state_ioctl(fd, 1, "state after unknown ioctl");

    pagesz = sysconf(_SC_PAGESIZE);
    if (pagesz <= 0) {
        verify("protected page setup", 0, "sysconf(_SC_PAGESIZE) failed");
    } else {
        protected_page = mmap(NULL, (size_t)pagesz, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (protected_page == MAP_FAILED) {
            verify("protected page setup", 0, strerror(errno));
        } else {
            r = do_ioctl(fd, THERMAL_IOC_SET_STATE, protected_page, &err);
            check_result("SET protected page", r, err, -1, EFAULT);
            check_state_ioctl(fd, 1, "state after SET EFAULT");

            r = do_ioctl(fd, THERMAL_IOC_GET_STATE, protected_page, &err);
            check_result("GET protected page", r, err, -1, EFAULT);
            munmap(protected_page, (size_t)pagesz);
        }
    }

    /* Both ioctl and legacy write must observe the same atomic state. */
    errno = 0;
    {
        ssize_t n = write(fd, "0", 1);
        err = errno;
        verify("legacy write '0'", n == 1 && err == 0,
               "expect write() == 1");
    }
    check_state_ioctl(fd, 0, "GET after legacy write");

    r = do_ioctl(fd, THERMAL_IOC_SET_STATE, &req, &err);
    check_result("SET state=0", r, err, 0, 0);
    check_state_ioctl(fd, 0, "final state=0");

    close(fd);
    printf("\nSUMMARY: %u/%u PASS, %u FAIL\n",
           total - failed, total, failed);
    return failed ? 1 : 0;
}
