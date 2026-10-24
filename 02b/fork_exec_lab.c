#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
  char *argv[] = {"ls", "-a", "-l", NULL};
  char *envp[] = {
    "PATH=/usr/local/bin:/usr/bin:/bin",
    NULL
  };

  pid_t pid;

  // TODO 1: fork() 호출
  pid = fork();

  // TODO 2: fork() 실패 처리
  if(pid == -1) {
    perror("fork");
    return 1;
  }

  // TODO 3: 자식 프로세스
  if(pid == 0) {
  // 자신의 PID 출력
    printf("Child PID: %ld, PPID: %ld\n", (long)getpid(), (long)getppid());
  // execve()로 /usr/bin/ls 실행
    execve("/usr/bin/ls", argv, envp);
  // execve() 실패 처리
    perror("execve");
    return 1;
  }

  // TODO 4: 부모 프로세스
  else {
  // 자신의 PID와 자식 PID 출력
    printf("PID : %d, PPID : %d\n", getpid(), getppid());
  // waitpid() 호출 및 실패 처리
    pid_t ret;
    ret = waitpid(pid,NULL,0);
    if(ret == -1)
    {
      perror("waitpid");
      return 1;
    }
  // TODO 5: 자식 회수 완료 출력
    printf("Parent complete\n");
  }
  return 0;
}
