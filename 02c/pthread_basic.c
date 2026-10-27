#include <pthread.h>
#include <stdio.h>
#include <string.h>

static void *worker(void *arg)
{
  int *shared = (int *)arg;
  int local = 100;

  printf("[Worker] local address = %p\n", (void *)&local);
  printf("[Worker] shared address = %p\n", (void *)shared);

  *shared = 20;

  return NULL;
}

int main(void)
{
  pthread_t thread;
  int shared = 10;
  int local = 200;

  printf("[Main] local address  = %p\n", (void *)&local);
  printf("[Main] shared address = %p\n", (void *)&shared);
  printf("[Main] shared before  = %d\n", shared);

  int ret = pthread_create(&thread, NULL, worker, &shared);

  if(ret != 0) {
    fprintf(stderr, "pthread_create: %s\n", strerror(ret));
    return 1;
  }

  ret = pthread_join(thread, NULL);

  if(ret != 0) {
    fprintf(stderr, "pthread_join: %s\n", strerror(ret));
    return 1;
  }

  printf("[Main] shared after     = %d\n", shared);

  return 0;
}
