#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <unistd.h>

int main(void)
{
  // TODO 1: argv 배열 선언
  char *argv[] = {"ls","-a","-l",NULL};
  // TODO 2: envp 배열 선언
  char *envp[] = {"PATH=/usr/local/bin:/usr/bin:/bin",NULL};

  printf("Before execve\n");

  // TODO 3: execve() 호출
  execve("/usr/bin/ls",argv,envp);
  // TODO 4: execve() 실패 처리
  perror("execve");
  return 1;
}
