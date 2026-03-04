#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *path;
    pid_t pid;
} ChildProc;

static volatile sig_atomic_t g_running = 1;

static void on_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

static int spawn_child(ChildProc *c) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execl(c->path, c->path, (char *)NULL);
        _exit(127);
    }
    c->pid = pid;
    return 0;
}

int main(void) {
    ChildProc children[] = {
        {"asr_kws", "./asr_kws_process", -1},
        {"tts", "./tts_process", -1},
        {"player", "./player/run", -1},
    };
    const int n = (int)(sizeof(children) / sizeof(children[0]));

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    for (int i = 0; i < n; ++i) {
        if (spawn_child(&children[i]) != 0) {
            fprintf(stderr, "spawn %s failed: %s\n", children[i].name, strerror(errno));
        }
    }

    while (g_running) {
        int status = 0;
        pid_t dead = waitpid(-1, &status, 0);
        if (dead < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < n; ++i) {
            if (children[i].pid == dead) {
                children[i].pid = -1;
                if (g_running) {
                    usleep(300000);
                    spawn_child(&children[i]);
                }
                break;
            }
        }
    }

    for (int i = 0; i < n; ++i) {
        if (children[i].pid > 0) {
            kill(children[i].pid, SIGTERM);
        }
    }
    while (waitpid(-1, NULL, 0) > 0) {
    }
    return 0;
}
