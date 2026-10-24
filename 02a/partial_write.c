
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
  int p[2];

  if (pipe2(p, O_NONBLOCK) == -1) {
    perror("pipe2");
    return 1;
  }

  int capacity = fcntl(p[1], F_GETPIPE_SZ);

  if (capacity == -1) {
    perror("fcntl");
    close(p[0]);
    close(p[1]);
    return 1;
  }

  size_t requested = (size_t)capacity + 4096;
  char *data = calloc(requested, 1);

  if (!data) {
    perror("calloc");
    close(p[0]);
    close(p[1]);
    return 1;
  }

  ssize_t n = 0;
  n = write(p[1], data, requested);
  printf("Capacity  = %d\n", capacity);
  printf("Requested = %zu\n", requested);
  printf("Written   = %zd\n", n);

  if (n == -1)
    perror("write");
  
  if(n > 0 && (size_t)n < requested) {

  char drained[4096];
  ssize_t consumed = 0;
  consumed = read(p[0], drained, sizeof(drained));
  
  if(consumed == -1) {
    perror("pipe read");
  } else {
    printf("Consumed = %zd\n", consumed);
  }

  ssize_t retry = 0;

  retry = write(p[1], data + n, requested - (size_t)n);

  if(retry == -1)
    perror("retry write");
  else
    printf("Retry = %zd\n", retry);

  }
  free(data);
  close(p[0]);
  close(p[1]);

  return (n > 0 && (size_t)n < requested) ? 0 : 1;
}

