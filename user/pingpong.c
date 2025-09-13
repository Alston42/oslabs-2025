#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]) {
    int f2c[2];
    pipe(f2c);
    int c2f[2];
    pipe(c2f);
    char buf[512];
    char pid[512];
    
    if (fork() == 0) {
        /* 子进程代码块 */
        // ping
        close(f2c[1]);
        read(f2c[0], buf, sizeof(buf));
        printf("%d: received ping from pid %s\n", getpid(), buf);
        close(f2c[0]);

        // pong
        close(c2f[0]);
        itoa(getpid(), pid);
        write(c2f[1], pid, sizeof(pid));
        close(c2f[1]);
        exit(0);
    } else {
        /* 父进程代码块 */
        // ping
        close(f2c[0]);
        itoa(getpid(), pid);
        write(f2c[1], pid, sizeof(pid));
        close(f2c[1]);
        
        // pong
        close(c2f[1]);
        read(c2f[0], buf, sizeof(buf));
        printf("%d: received pong from pid %s\n", getpid(), buf);
        close(c2f[0]);
        exit(0);
    }

    exit(0);
}