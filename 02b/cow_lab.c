#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
  int value = 10;

  pid_t pid = fork();

  printf("===<&value : %p>===\n", (void*)&value);
  if(pid == 0) {
    value = 20;
    printf("===<Child => value : %d, &value : %p>===\n", value, (void*)&value);
  }
  else {
    waitpid(pid, NULL, 0);
    printf("===<Parent => value : %d, &value : %p>===\n", value, (void*)&value);
  }
  return 0;
}
