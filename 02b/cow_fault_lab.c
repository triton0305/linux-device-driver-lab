#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
  const size_t page_count = 16;
  long page_size = sysconf(_SC_PAGESIZE);

  if (page_size <= 0) {
    perror("sysconf");
    return 1;
  }

  size_t buffer_size = (size_t)page_size * page_count;
  unsigned char *buffer = malloc(buffer_size);

  if (buffer == NULL) {
    perror("malloc");
    return 1;
  }

  // 부모: fork 이전에 각 페이지에 값 기록
  for (size_t i = 0; i < page_count; i++) {
    buffer[i * (size_t)page_size] = 10;
  }

  pid_t pid = fork();

  if (pid < 0) {
    perror("fork");
    free(buffer);
    return 1;
  }

  if (pid == 0) {
    struct rusage before;
    struct rusage after;

    if (getrusage(RUSAGE_SELF, &before) == -1) {
      perror("getrusage");
      _exit(1);
    }

    // 자식: 각 페이지에 값 변경
    for (size_t i = 0; i < page_count; i++) {
      buffer[i * (size_t)page_size] = 20;
    }

    if (getrusage(RUSAGE_SELF, &after) == -1) {
      perror("getrusage");
      _exit(1);
    }

    long minor_faults = after.ru_minflt - before.ru_minflt;

    printf("Page size: %ld bytes\n", page_size);
    printf("Page count: %zu\n", page_count);
    printf("Minor faults: %ld\n", minor_faults);

    fflush(stdout);
    free(buffer);
    _exit(0);
  }

  if (waitpid(pid, NULL, 0) == -1) {
    perror("waitpid");
    free(buffer);
    return 1;
  }

  free(buffer);
  return 0;
}
