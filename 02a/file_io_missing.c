
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    const char *path = "missing_ch02.txt";

    int fd = open(path, O_RDONLY);

    if (fd == -1) {
        perror("open");
        return 1;
    }

    char buf[4];

    for (int i = 0; i < 3; i++) {

        // TODO: read()를 호출하고 반환값을 n에 저장
        ssize_t n = 0;
        n = read(fd, buf, sizeof(buf));

        if (n == -1) {
            perror("read");
            close(fd);
            return 1;
        }

        printf("read #%d = %zd\n", i + 1, n);
    }

    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    return 0;
}

