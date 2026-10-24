
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
  int fd = open("sample.txt", O_RDONLY);

  if (fd == -1) {
    perror("open");
    return 1;
  }

  const char data[] = "XYZ";

  // TODO: write()를 호출해 반환값을 n에 저장
  ssize_t n = 0;
  n = write(fd, data, sizeof(data));

  if (n == -1) {
    int error = errno;
    perror("write");
    printf("errno = %d, EBADF = %d\n", error, EBADF);
  } else {
    printf("write returned %zd\n", n);
  }

  if (close(fd) == -1) {
    perror("close");
    return 1;
  }

  return n == -1 ? 0 : 1;
}

