#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
    int p1[2];
    int p2[2];
    char buf[1];

    if (argc != 1) {
        fprintf(2, "Usage: pingpong\n");
        exit(1);
    }

    if (pipe(p1) < 0 || pipe(p2) < 0) {
        fprintf(2, "pipe error\n");
        exit(1);
    }

    if (fork() == 0) {  // child
        close(p1[1]);
        close(p2[0]);  // no read, write need

        if (read(p1[0], buf, 1) != 1) {
            fprintf(2, "read error(child)\n");
            exit(1);
        }

        printf("%d: received ping\n", getpid());

        write(p2[1], "p", 1);
        close(p1[0]);
        close(p2[1]);  // succeed
        exit(0);
    }

    close(p1[0]);
    close(p2[1]);

    write(p1[1], "p", 1);

    if (read(p2[0], buf, 1) != 1) {
        fprintf(2, "read error(parent)\n");
        exit(1);
    }

    close(p1[1]);
    close(p2[0]);

    wait(0);  // reap
    printf("%d: received pong\n", getpid());

    exit(0);
}