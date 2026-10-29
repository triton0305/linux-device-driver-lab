// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include "lab_gpio_uapi.h"

_Static_assert(sizeof(struct lab_gpio_state) == 16, "GET ABI");
_Static_assert(sizeof(struct lab_gpio_led) == 16, "SET ABI");
static unsigned int failures;
#define CHECK(test) do { \
	if (!(test)) { fprintf(stderr, "FAIL line %d: %s (errno=%d)\n", \
		__LINE__, #test, errno); failures++; } \
} while (0)

static int get(int fd, struct lab_gpio_state *s)
{
	memset(s, 0, sizeof(*s));
	return ioctl(fd, LAB_GPIO_IOC_GET, s);
}
static int set(int fd, unsigned int value)
{
	struct lab_gpio_led req = { .abi_version = LAB_GPIO_ABI_VERSION, .led = value };
	return ioctl(fd, LAB_GPIO_IOC_SET, &req);
}
static void nap(void)
{
	struct timespec delay = { .tv_nsec = 100000000 };
	while (nanosleep(&delay, &delay) < 0 && errno == EINTR) { }
}
static int selftest(int fd, const char *path)
{
	struct lab_gpio_state state;
	struct lab_gpio_led bad = { .abi_version = LAB_GPIO_ABI_VERSION, .led = 2 };
	int second = open(path, O_RDWR | O_CLOEXEC);
	char data[3] = {0};

	CHECK(second >= 0);
	CHECK(ioctl(fd, LAB_GPIO_IOC_OFF) == 0);
	CHECK(set(fd, 1) == 0);
	CHECK(get(second, &state) == 0 && state.led == 1 && state.button <= 1 &&
	      state.abi_version == LAB_GPIO_ABI_VERSION && state.reserved == 0);
	CHECK(read(second, data, 1) == 1 && data[0] == '1');
	CHECK(read(second, data, 2) == 1 && data[0] == '\n');
	CHECK(read(second, data, 1) == 0);
	CHECK(write(fd, "", 0) == 0);
	errno = 0; CHECK(write(fd, "x", 1) == -1 && errno == EINVAL);
	errno = 0; CHECK(write(fd, "11", 2) == -1 && errno == EINVAL);
	errno = 0; CHECK(ioctl(fd, LAB_GPIO_IOC_SET, &bad) == -1 && errno == EINVAL);
	bad.led = 0; bad.abi_version++;
	errno = 0; CHECK(ioctl(fd, LAB_GPIO_IOC_SET, &bad) == -1 && errno == EINVAL);
	bad.abi_version = LAB_GPIO_ABI_VERSION; bad.reserved[1] = 1;
	errno = 0; CHECK(ioctl(fd, LAB_GPIO_IOC_SET, &bad) == -1 && errno == EINVAL);
	errno = 0; CHECK(ioctl(fd, _IO('X', 1)) == -1 && errno == ENOTTY);
	errno = 0; CHECK(ioctl(fd, _IOR('G', 1, __u32), &state) == -1 && errno == ENOTTY);
	errno = 0; CHECK(ioctl(fd, LAB_GPIO_IOC_SET, NULL) == -1 && errno == EFAULT);
	errno = 0; CHECK(ioctl(fd, LAB_GPIO_IOC_GET, NULL) == -1 && errno == EFAULT);
	CHECK(get(fd, &state) == 0 && state.led == 1);
	CHECK(write(second, "0", 1) == 1);
	CHECK(get(fd, &state) == 0 && state.led == 0);
	CHECK(ioctl(fd, LAB_GPIO_IOC_OFF) == 0);
	if (second >= 0) close(second);
	printf("API selftest: %u failures (does not verify electrical behavior)\n", failures);
	return failures ? 1 : 0;
}

/* Private runtime files are advisory coordination for this application only.
 * Never unlink lock files: replacing their inode would break flock exclusion.
 * No PID files or kill(pid) are used; stop talks to the live socket owner.
 */
#ifndef LAB_GPIO_RUNTIME_DIR
#define LAB_GPIO_RUNTIME_DIR "/run/lab-gpio"
#endif
struct control {
	int gate, owner;
	char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
};
#define MAX_CHANNELS 8
struct channel {
	struct control control;
	char *path;
	int fd, listener, bound;
	int candidate, consecutive, armed;
	unsigned int led;
};
static volatile sig_atomic_t stopping;

static void stop_signal(int signum)
{
	(void)signum;
	stopping = 1;
}

static void control_close(struct control *c)
{
	if (c->owner >= 0) close(c->owner);
	if (c->gate >= 0) close(c->gate);
	c->owner = c->gate = -1;
}

static int private_file(int dir, const char *name)
{
	struct stat st;
	int fd = openat(dir, name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) return -1;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 077) != 0) {
		close(fd); errno = EACCES; return -1;
	}
	return fd;
}

/* Resolve symlinks while present, and also allow stop/status after unbind has
 * removed the canonical device node. Use that canonical path for later stop.
 */
static char *device_path(const char *path)
{
	char *resolved = realpath(path, NULL), *copy, *slash, *parent, *result;
	const char *base;
	if (resolved || errno != ENOENT) return resolved;
	copy = strdup(path);
	if (!copy) return NULL;
	slash = strrchr(copy, '/');
	base = slash ? slash + 1 : copy;
	if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) {
		free(copy); errno = EINVAL; return NULL;
	}
	if (slash) {
		*slash = '\0';
		parent = realpath(*copy ? copy : "/", NULL);
	} else {
		parent = realpath(".", NULL);
	}
	if (!parent) { free(copy); return NULL; }
	result = malloc(strlen(parent) + strlen(base) + 2);
	if (result) sprintf(result, "%s%s%s", parent, strcmp(parent, "/") ? "/" : "", base);
	free(parent); free(copy);
	return result;
}

static int control_open(struct control *c, const char *path)
{
	static const char hex[] = "0123456789abcdef";
	struct stat st;
	char key[sizeof(c->socket_path)], name[sizeof(key) + 8];
	char *canonical = device_path(path);
	int dir = -1, saved;
	size_t len;
	c->gate = c->owner = -1;
	if (!canonical) return -1;
	len = strlen(canonical);
	if (len * 2 + strlen(LAB_GPIO_RUNTIME_DIR) + 7 >= sizeof(c->socket_path)) {
		free(canonical); errno = ENAMETOOLONG; return -1;
	}
	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)canonical[i];
		key[2 * i] = hex[ch >> 4]; key[2 * i + 1] = hex[ch & 15];
	}
	key[len * 2] = '\0';
	free(canonical);
	if (mkdir(LAB_GPIO_RUNTIME_DIR, 0700) < 0 && errno != EEXIST) return -1;
	dir = open(LAB_GPIO_RUNTIME_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (dir < 0) return -1;
	if (fstat(dir, &st) < 0 || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		errno = EACCES; goto fail;
	}
	snprintf(name, sizeof(name), "%s.gate", key);
	c->gate = private_file(dir, name);
	if (c->gate < 0) goto fail;
	/* Serialize start/stop/manual writers, not the daemon's polling loop. */
	if (flock(c->gate, LOCK_EX) < 0) goto fail;
	snprintf(name, sizeof(name), "%s.lock", key);
	c->owner = private_file(dir, name);
	if (c->owner < 0) goto fail;
	if (snprintf(c->socket_path, sizeof(c->socket_path), "%s/%s.sock",
		     LAB_GPIO_RUNTIME_DIR, key) >= (int)sizeof(c->socket_path)) {
		errno = ENAMETOOLONG; goto fail;
	}
	close(dir);
	return 0;
fail:
	saved = errno;
	close(dir); control_close(c); errno = saved;
	return -1;
}

static int wait_readable(int fd, int timeout)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };
	int ret;
	do { ret = poll(&p, 1, timeout); } while (ret < 0 && errno == EINTR && !stopping);
	if (ret == 0) errno = ETIMEDOUT;
	return ret > 0 ? 0 : -1;
}

static int receive_code(int fd, int *code)
{
	if (wait_readable(fd, 5000) < 0) return -1;
	if (recv(fd, code, sizeof(*code), 0) != sizeof(*code)) {
		errno = EPROTO; return -1;
	}
	return 0;
}

static int send_code(int fd, int code)
{
	return send(fd, &code, sizeof(code), MSG_NOSIGNAL) == sizeof(code) ? 0 : -1;
}

static int connect_control(struct control *c, char command)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	memcpy(address.sun_path, c->socket_path, strlen(c->socket_path) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    send(fd, &command, 1, MSG_NOSIGNAL) != 1) {
		int saved = errno;
		close(fd); errno = saved; return -1;
	}
	return fd;
}

static int checked_get(int fd, struct lab_gpio_state *s)
{
	if (get(fd, s) < 0) return -1;
	if (s->abi_version != LAB_GPIO_ABI_VERSION || s->led > 1 || s->button > 1 || s->reserved) {
		errno = EPROTO; return -1;
	}
	return 0;
}

static long long monotonic_ms(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int sample_channel(struct channel *ch)
{
	struct lab_gpio_state s;
	if (checked_get(ch->fd, &s) < 0) return -1;
	if (ch->candidate != (int)s.button) {
		ch->candidate = (int)s.button;
		ch->consecutive = 1;
	} else if (ch->consecutive < 3) {
		ch->consecutive++;
	}
	if (ch->consecutive == 3) {
		if (!ch->candidate) {
			ch->armed = 1;
		} else if (ch->armed) {
			if (set(ch->fd, !ch->led) < 0) return -1;
			ch->led = !ch->led;
			ch->armed = 0;
		}
	}
	return 0;
}

static int background_loop(struct channel *channels, size_t count, int *stop_client)
{
	struct pollfd pollfds[MAX_CHANNELS];
	long long next = monotonic_ms();
	if (next < 0) return -1;
	for (size_t i = 0; i < count; i++) {
		pollfds[i].fd = channels[i].listener;
		pollfds[i].events = POLLIN;
	}
	next += 20;
	while (!stopping) {
		long long now = monotonic_ms();
		int ret;
		if (now < 0) return -1;
		ret = poll(pollfds, count, now < next ? (int)(next - now) : 0);
		if (ret < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		for (size_t i = 0; i < count && !stopping; i++) {
			if (pollfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { errno = EIO; return -1; }
			if (pollfds[i].revents & POLLIN) {
				int client = accept4(channels[i].listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
				if (client >= 0) {
					char command;
					if (wait_readable(client, 100) == 0 && recv(client, &command, 1, 0) == 1) {
						/* Stopping through ANY member stops and cleans the entire group. */
						if (command == 'S') { *stop_client = client; return 0; }
						(void)send_code(client, command == 'Q' ? 0 : EINVAL);
					}
					close(client);
				} else if (errno != EAGAIN && errno != EINTR) {
					return -1;
				}
			}
		}
		if (stopping) break;
		now = monotonic_ms();
		if (now < 0) return -1;
		if (now >= next) {
			for (size_t i = 0; i < count && !stopping; i++) {
				if (sample_channel(&channels[i]) < 0) {
					int saved = errno;
					syslog(LOG_ERR, "%s I/O failure: %s", channels[i].path, strerror(saved));
					errno = saved;
					return -1;
				}
			}
			/* One scan per period, no catch-up burst after scheduling/I/O delay. */
			next = monotonic_ms();
			if (next < 0) return -1;
			next += 20;
		}
	}
	return 0;
}

static void daemon_run(struct channel *channels, size_t count, int ready)
{
	struct sigaction sa = { .sa_handler = stop_signal };
	int client = -1, code = 0, ack, nullfd;
	for (size_t i = 0; i < count; i++) {
		close(channels[i].control.gate); channels[i].control.gate = -1;
	}
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
		code = errno; goto cleanup;
	}
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGHUP, &sa, NULL) < 0 || setsid() < 0) { code = errno; goto cleanup; }
	umask(077);
	openlog("lab_gpio_toggle", LOG_PID, LOG_DAEMON);
	/* Visit EVERY device even after an earlier open/OFF/GET fails. A missing
	 * channel must not leave later, otherwise accessible LEDs unchanged.
	 */
	for (size_t i = 0; i < count; i++) {
		struct channel *ch = &channels[i];
		struct lab_gpio_state s;
		ch->fd = open(ch->path, O_RDWR | O_CLOEXEC | O_NOCTTY);
		if (ch->fd < 0 || ioctl(ch->fd, LAB_GPIO_IOC_OFF) < 0 || checked_get(ch->fd, &s) < 0) {
			int saved = errno;
			syslog(LOG_ERR, "%s initialization failed: %s", ch->path, strerror(saved));
			if (!code) code = saved;
		}
	}
	if (code || stopping) { if (!code) code = EINTR; goto cleanup; }
	for (size_t i = 0; i < count; i++) {
		struct channel *ch = &channels[i];
		struct sockaddr_un address = { .sun_family = AF_UNIX };
		ch->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
		if (ch->listener < 0) { code = errno; goto cleanup; }
		memcpy(address.sun_path, ch->control.socket_path, strlen(ch->control.socket_path) + 1);
		/* All lifetime locks are owned; any old socket is stale. */
		if (unlink(ch->control.socket_path) < 0 && errno != ENOENT) { code = errno; goto cleanup; }
		if (bind(ch->listener, (struct sockaddr *)&address, sizeof(address)) < 0) {
			code = errno; goto cleanup;
		}
		ch->bound = 1;
		if (listen(ch->listener, 8) < 0) { code = errno; goto cleanup; }
	}
	if (chdir("/") < 0) { code = errno; goto cleanup; }
	nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (nullfd < 0) { code = errno; goto cleanup; }
	for (int i = 0; i < 3; i++) {
		if (dup2(nullfd, i) < 0) { code = errno; close(nullfd); goto cleanup; }
	}
	if (nullfd > 2) close(nullfd);
	if (stopping) { code = EINTR; goto cleanup; }
	if (send_code(ready, 0) < 0 || receive_code(ready, &ack) < 0 || ack != 0) {
		code = ECANCELED; goto cleanup;
	}
	close(ready); ready = -1;
	syslog(LOG_INFO, "toggle controller ready: %zu channel(s)", count);
	if (background_loop(channels, count, &client) < 0) {
		code = errno;
		syslog(LOG_ERR, "GPIO/control failure: %s; stopping all channels", strerror(code));
	}
cleanup:
	/* Never stop cleanup on an OFF error. Hold ALL locks until ALL LEDs have
	 * been visited, so a new writer cannot interleave with group cleanup.
	 */
	for (size_t i = 0; i < count; i++) {
		struct channel *ch = &channels[i];
		if (ch->fd >= 0) {
			if (ioctl(ch->fd, LAB_GPIO_IOC_OFF) < 0) {
				int error = errno;
				syslog(LOG_ERR, "%s exit LED OFF failed: %s", ch->path, strerror(error));
				if (!code) code = error;
			}
			close(ch->fd);
		}
		if (ch->listener >= 0) close(ch->listener);
		if (ch->bound) unlink(ch->control.socket_path);
	}
	for (size_t i = 0; i < count; i++) close(channels[i].control.owner);
	if (client >= 0) { (void)send_code(client, code); close(client); }
	if (ready >= 0) { (void)send_code(ready, code ? code : ECANCELED); close(ready); }
	closelog();
	_exit(code ? 1 : 0);
}

static int start_controller(struct channel *channels, size_t count)
{
	int pair[2], code = 0;
	pid_t child;
	/* Claim every member before changing any GPIO. Rollback is just close. */
	for (size_t i = 0; i < count; i++) {
		if (flock(channels[i].control.owner, LOCK_EX | LOCK_NB) < 0) {
			fprintf(stderr, "%s: controller already running or output owned by another command.\n", channels[i].path);
			return 1;
		}
	}
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0) return -1;
	fflush(NULL);
	child = fork();
	if (child < 0) { int saved = errno; close(pair[0]); close(pair[1]); errno = saved; return -1; }
	if (!child) { close(pair[0]); daemon_run(channels, count, pair[1]); }
	close(pair[1]);
	if (receive_code(pair[0], &code) < 0 || code || send_code(pair[0], 0) < 0) {
		int saved = code ? code : errno;
		close(pair[0]); errno = saved; return -1;
	}
	close(pair[0]);
	printf("Toggle controller ready: %zu channel(s), 20 ms polling, 3-sample debounce. All LEDs initialized OFF.\n", count);
	return 0;
}

static int stop_controller(struct control *c, const char *path)
{
	int fd, reply = 0, result = 0;
	if (flock(c->owner, LOCK_EX | LOCK_NB) < 0) {
		int client;
		if (errno != EWOULDBLOCK) return -1;
		client = connect_control(c, 'S');
		if (client < 0) return -1; /* Never write OFF against an unconfirmed live owner. */
		if (receive_code(client, &reply) < 0) { int saved = errno; close(client); errno = saved; return -1; }
		close(client);
		if (flock(c->owner, LOCK_EX | LOCK_NB) < 0) return -1;
		if (reply) {
			fprintf(stderr, "Controller cleanup: %s\n", strerror(reply));
			result = 1;
		}
	}
	/* Covers an absent/crashed controller too; gate + owner exclude new starts. */
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) return -1;
	if (ioctl(fd, LAB_GPIO_IOC_OFF) < 0) { int saved = errno; close(fd); errno = saved; return -1; }
	close(fd);
	puts("Toggle controller stopped; LED OFF.");
	return result;
}

/* Sort canonical paths before acquiring gates to avoid reversed-list deadlocks. */
static int compare_channels(const void *a, const void *b)
{
	return strcmp(((const struct channel *)a)->path, ((const struct channel *)b)->path);
}

static int parse_channels(const char *spec, struct channel *channels, size_t *count)
{
	/* Device NODE names only: GPIO numbers and polarity remain in Device Tree. */
	const char *defaults = "/dev/lab_gpio-lab-gpio,/dev/lab_gpio-lab-gpio-d1,"
		"/dev/lab_gpio-lab-gpio-d2,/dev/lab_gpio-lab-gpio-d3,"
		"/dev/lab_gpio-lab-gpio-d4,/dev/lab_gpio-lab-gpio-d5,"
		"/dev/lab_gpio-lab-gpio-d6,/dev/lab_gpio-lab-gpio-d7";
	char *list = strdup(!strcmp(spec, "all") ? defaults : spec), *cursor, *item;
	*count = 0;
	if (!list) return -1;
	cursor = list;
	while ((item = strsep(&cursor, ","))) {
		struct channel *ch;
		if (!*item || *count == MAX_CHANNELS) { free(list); errno = EINVAL; return -1; }
		ch = &channels[*count];
		ch->control.gate = ch->control.owner = ch->fd = ch->listener = -1;
		ch->candidate = -1;
		ch->path = device_path(item);
		if (!ch->path) { free(list); return -1; }
		(*count)++;
	}
	free(list);
	if (*count != 1 && *count != MAX_CHANNELS) { errno = EINVAL; return -1; }
	qsort(channels, *count, sizeof(*channels), compare_channels);
	for (size_t i = 1; i < *count; i++) {
		if (!strcmp(channels[i-1].path, channels[i].path)) { errno = EINVAL; return -1; }
	}
	return 0;
}

static int status_controller(struct control *c)
{
	if (flock(c->owner, LOCK_EX | LOCK_NB) == 0) {
		puts("Toggle controller stopped."); return 0;
	}
	if (errno == EWOULDBLOCK) {
		int code = 0, client = connect_control(c, 'Q'), rc = -1;
		if (client < 0) return -1;
		if (receive_code(client, &code) == 0) {
			if (code) errno = code;
			else { puts("Toggle controller running."); rc = 0; }
		}
		int saved = errno;
		close(client); errno = saved;
		return rc;
	}
	return -1;
}

static int toggle_command(const char *spec, const char *command)
{
	struct channel channels[MAX_CHANNELS] = {0};
	size_t count = 0;
	int rc = 0;
	if (parse_channels(spec, channels, &count) < 0) {
		perror("device list (one path, all, or eight distinct comma-separated paths)");
		rc = 1; goto out;
	}
	for (size_t i = 0; i < count; i++) {
		if (control_open(&channels[i].control, channels[i].path) < 0) {
			perror("runtime control (requires access to /run/lab-gpio)");
			rc = 1; goto out;
		}
	}
	if (!strcmp(command, "start")) {
		rc = start_controller(channels, count);
		if (rc < 0) perror("toggle start");
	} else {
		for (size_t i = 0; i < count; i++) {
			int ret;
			if (count > 1) printf("%s: ", channels[i].path);
			if (!strcmp(command, "stop"))
				ret = stop_controller(&channels[i].control, channels[i].path);
			else
				ret = status_controller(&channels[i].control);
			if (ret < 0) perror(channels[i].path);
			if (ret) rc = 1; /* Still visit every later channel. */
		}
	}
out:
	for (size_t i = 0; i < count; i++) {
		control_close(&channels[i].control);
		free(channels[i].path);
	}
	return rc ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct lab_gpio_state s;
	int fd, rc = 0;
	struct control writer = { .gate = -1, .owner = -1 };
	if (argc == 4 && !strcmp(argv[2], "toggle") &&
	    (!strcmp(argv[3], "start") || !strcmp(argv[3], "stop") || !strcmp(argv[3], "status")))
		return toggle_command(argv[1], argv[3]);
	if (argc != 3 || (strcmp(argv[2], "get") && strcmp(argv[2], "on") &&
	    strcmp(argv[2], "off") && strcmp(argv[2], "watch") &&
	    strcmp(argv[2], "selftest") && strcmp(argv[2], "hold"))) {
		fprintf(stderr, "Usage: %s /dev/lab_gpio-DEVICE get|on|off|watch|selftest|hold\n"
			"       %s DEVICE|all|DEV0,...,DEV7 toggle start|status|stop\n"
			"       all off: stop the group and attempt OFF on all eight devices\n"
			"selftest drives LED; watch samples every 100 ms for 30 s;\n"
			"hold waits up to 30 s for manual unbind and checks old FD.\n", argv[0], argv[0]);
		return 2;
	}
	if (!strcmp(argv[2], "off")) return toggle_command(argv[1], "stop");
	if (!strcmp(argv[1], "all") || strchr(argv[1], ',')) {
		fprintf(stderr, "Multiple devices support only toggle start/status/stop and off.\n");
		return 2;
	}
	if (!strcmp(argv[2], "on") || !strcmp(argv[2], "selftest")) {
		if (control_open(&writer, argv[1]) < 0) { perror("runtime control"); return 1; }
		if (flock(writer.owner, LOCK_EX | LOCK_NB) < 0) {
			fprintf(stderr, "Output owned by toggle controller; stop it before on/selftest.\n");
			control_close(&writer); return 1;
		}
	}
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open"); control_close(&writer); return 1; }
	if (!strcmp(argv[2], "selftest")) {
		rc = selftest(fd, argv[1]);
	} else if (!strcmp(argv[2], "on")) {
		rc = set(fd, 1);
	} else if (!strcmp(argv[2], "hold")) {
		int removed = 0;
		puts("FD held: manually unbind now; no sysfs writes are made by this app.");
		fflush(stdout);
		for (int i = 0; i < 300; i++) {
			if (get(fd, &s) < 0) {
				if (errno == ENODEV) { removed = 1; break; }
				perror("GET while held"); rc = 1; break;
			}
			nap();
		}
		CHECK(removed);
		if (removed) {
			char c;
			errno = 0; CHECK(read(fd, &c, 1) == -1 && errno == ENODEV);
			errno = 0; CHECK(write(fd, "0", 1) == -1 && errno == ENODEV);
			errno = 0; CHECK(ioctl(fd, LAB_GPIO_IOC_OFF) == -1 && errno == ENODEV);
			puts("Old FD rejected after unbind; close now releases its reference.");
		}
		rc = rc || failures;
	} else {
		int samples = !strcmp(argv[2], "watch") ? 300 : 1;
		for (int i = 0; i < samples; i++) {
			if (get(fd, &s) < 0) { rc = -1; break; }
			if (samples > 1) nap();
			/* Watch samples every 100 ms, but logs only every ten samples. */
			if (samples == 1 || (i + 1) % 10 == 0) {
				printf("LED=%s button=%s\n", s.led ? "ON" : "OFF",
				       s.button ? "pressed" : "released");
				fflush(stdout);
			}
		}
	}
	if (rc < 0) perror("GPIO operation");
	close(fd);
	control_close(&writer);
	return rc ? 1 : 0;
}
