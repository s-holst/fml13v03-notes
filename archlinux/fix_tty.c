#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int isatty(int fd) {
    struct termios t;
    return ioctl(fd, TCGETS, &t) == 0;
}

int tcgetattr(int fd, struct termios *t) {
    return ioctl(fd, TCGETS, t);
}

int tcsetattr(int fd, int optional_actions, const struct termios *t) {
    int cmd;
    switch (optional_actions) {
    case TCSANOW:
        cmd = TCSETS;
        break;
    case TCSADRAIN:
        cmd = TCSETSW;
        break;
    case TCSAFLUSH:
        cmd = TCSETSF;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return ioctl(fd, cmd, t);
}

char *ttyname(int fd) {
    static char buf[256];
    if (!isatty(fd))
        return NULL;
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(proc_path, buf, sizeof(buf) - 1);
    if (len < 0)
        return NULL;
    buf[len] = '\0';
    return buf;
}

int ttyname_r(int fd, char *buf, size_t buflen) {
    if (!isatty(fd))
        return errno ? errno : ENOTTY;
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d", fd);
    ssize_t len = readlink(proc_path, buf, buflen - 1);
    if (len < 0)
        return errno;
    buf[len] = '\0';
    return 0;
}
