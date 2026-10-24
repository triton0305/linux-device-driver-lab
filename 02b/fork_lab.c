#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
  pid_t pid;

  // TODO 1: fork() 호출
  pid = fork();
  // TODO 2: fork() 실패 처리
  if(pid == -1) {
    perror("fork");
    return 1;
  }
  // TODO 3: 자식 프로세스
  // PID, PPID, fork() 반환값 출력
  if(pid == 0) {
    printf("pid : %d, ppid : %d, fork() : %d\n",getpid(), getppid(), pid);
  }

  // TODO 4: 부모 프로세스
  // PID, PPID, fork() 반환값 출력
  else{
    printf("pid : %d, ppid : %d, fork() : %d\n", getpid(), getppid(), pid);
  // TODO 5: waitpid()로 자식 종료 회수 
    pid_t ret = waitpid(pid, NULL, 0);
    
    if(ret == -1){
      perror("waitpid");
      return 1;
    }
  }
  return 0;
}
