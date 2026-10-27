#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
  int fd = open("mmap_sample.txt", O_RDONLY);

  if (fd == -1) {
    perror("open");
    return 1;
  }

  struct stat st;

  if (fstat(fd, &st) == -1) {
    perror("fstat");
    close(fd);
    return 1;
  }

  if (st.st_size <= 0) {
    fprintf(stderr, "File is empty\n");
    close(fd);
    return 1;
  }

  size_t length = (size_t)st.st_size;

  void *region = mmap(NULL, length,
                      PROT_READ, MAP_PRIVATE,
                      fd, 0);

  if (region == MAP_FAILED) {
    perror("mmap");
    close(fd);
    return 1;
  }

  if (close(fd) == -1) {
    perror("close");
    munmap(region, length);
    return 1;
  }

  printf("Mapped address: %p\n", region);
  printf("File contents: ");
  fwrite(region, 1, length, stdout);

  if (munmap(region, length) == -1) {
    perror("munmap");
    return 1;
  }

  return 0;
}
