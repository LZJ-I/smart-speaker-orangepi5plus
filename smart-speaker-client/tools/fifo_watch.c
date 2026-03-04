#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <fifo_path>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }

    char buf[1024];
    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read failed: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            usleep(10000);
            continue;
        }
        buf[n] = '\0';
        printf("%s", buf);
        fflush(stdout);
    }

    close(fd);
    return 0;
}
