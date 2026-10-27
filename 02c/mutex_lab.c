#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NUM_THREADS 2
#define LOOPS 1000000

static long counter = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
  (void)arg;

  for(int i = 0; i < LOOPS; i++) {
    pthread_mutex_lock(&mutex);

    counter++;

    pthread_mutex_unlock(&mutex);
  }

  return NULL;
}

int main(void)
{
  pthread_t threads[NUM_THREADS];
  int created = 0;

  for (int i = 0; i < NUM_THREADS; i++) {
    int ret = pthread_create(&threads[i], NULL, worker, NULL);

    if(ret != 0) {
      fprintf(stderr, "pthread_create: %s\n", strerror(ret));
      break;
    }

    created++;
  }

  for (int i = 0; i < created; i++) {
    int ret = pthread_join(threads[i], NULL);

    if(ret != 0) {
      fprintf(stderr, "pthread_join: %s\n", strerror(ret));
      return 1;
    }
  }

  if (created != NUM_THREADS)
    return 1;

  printf("Expected: %ld\n", (long)NUM_THREADS * LOOPS);
  printf("Actual:   %ld\n", counter);

  return 0;
}
