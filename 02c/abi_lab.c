#include <stdio.h>
#include <unistd.h>

static long add3(long a, long b, long c)
{
  return a + b + c;
}

int main(void)
{
  const char message[] = "Hello from write\n";

  long result = add3(10,20,30);

  if (write(STDOUT_FILENO, message, sizeof(message) - 1) == -1) {
    perror("write");
    return 1;
  }

  printf("add3 result = %ld\n", result);

  return 0;
}
