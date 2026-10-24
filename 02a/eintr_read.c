#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static void on_signal(int signo)
{
  (void)signo;
}

int main(void)
{
  int p[2];
  char buf[16];
  struct sigaction sa = {0};

  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGUSR1, &sa, NULL) == -1) {
    perror("sigaction");
    return 1;
  }

  if (pipe(p) == -1) {
    perror("pipe");
    return 1;
  }

  printf("PID = %ld\n", (long)getpid());
  fflush(stdout);

  // TODO: p[0]에서 최대 sizeof(buf) 바이트 읽기
  ssize_t n = 0;
  n = read(p[0], buf, sizeof(buf));

  int saved_errno = errno;

  if (n == -1 && saved_errno == EINTR) {
    printf("read interrupted: EINTR\n");
  } else if (n == -1) {
    errno = saved_errno;
    perror("read");
  } else {
    printf("read = %zd\n", n);
  }

  close(p[0]);
  close(p[1]);

  return (n == -1 && saved_errno == EINTR) ? 0 : 1;
}
