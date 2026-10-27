#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NUM_THREADS 2
#define LOOPS 5

static long counter = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *arg)
{
  int id = *(int *)arg;

  for(int i = 0; i < LOOPS; i++) {
    pthread_mutex_lock(&mutex);

    counter++;
    printf("Thread %d -> counter %ld\n", id, counter);

    pthread_mutex_unlock(&mutex);
  }

  return NULL;
}

int main(void)
{
  pthread_t threads[NUM_THREADS];
  int created = 0;

  for (int i = 0; i < NUM_THREADS; i++) {
    int ids[NUM_THREADS] = {0, 1};

    int ret = pthread_create(&threads[i], NULL, worker, ids+i);

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
