#include <errno.h>
#include <signal.h>

extern "C" int _getpid(void) {
    return 1;
}

extern "C" int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}