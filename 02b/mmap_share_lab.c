#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_test(int flags, const char *name)
{
  int *value = mmap(NULL, sizeof(int),
                    PROT_READ | PROT_WRITE,
                    flags | MAP_ANONYMOUS,
                    -1, 0);

  if (value == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  *value = 10;

  pid_t pid = fork();

  if (pid < 0) {
    perror("fork");
    munmap(value, sizeof(int));
    return 1;
  }

  if (pid == 0) {
    *value = 20;
    _exit(0);
  }

  if (waitpid(pid, NULL, 0) == -1) {
    perror("waitpid");
    munmap(value, sizeof(int));
    return 1;
  }

  printf("%s: Parent value = %d\n", name, *value);

  if (munmap(value, sizeof(int)) == -1) {
    perror("munmap");
    return 1;
  }

  return 0;
}

int main(void)
{
  if (run_test(MAP_PRIVATE, "MAP_PRIVATE") != 0)
    return 1;

  if (run_test(MAP_SHARED, "MAP_SHARED") != 0)
    return 1;

  return 0;
}
