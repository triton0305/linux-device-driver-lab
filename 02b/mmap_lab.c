#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
  long page_size = sysconf(_SC_PAGESIZE);

  if (page_size <= 0) {
    perror("sysconf");
    return 1;
  }

  size_t length = (size_t)page_size;

  void *region = mmap(NULL, length,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1, 0);

  if (region == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  unsigned char *buffer = region;

  printf("Page size: %ld\n", page_size);
  printf("Mapped address: %p\n", region);

  printf("Before: %u\n", (unsigned int)buffer[0]);

  buffer[0] = 42;

  printf("After: %u\n", (unsigned int)buffer[0]);

  if (munmap(region, length) == -1) {
    perror("munmap");
    return 1;
  }

  return 0;
}
