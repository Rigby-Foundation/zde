/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Rigby Foundation */
/* zde: the session. Starts the window server, then the panel, and waits;
 * when the server ends (Ctrl+Alt+Q) everything else is taken down and the
 * console comes back. `zde` from the shell is all it takes; `zde prog args`
 * also starts prog once the desktop is up (its output stays on the
 * console, handy for demos and tests). */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <zwm.h>

static pid_t spawn(const char *path, const char *arg)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl(path, path, arg, (char *)NULL);
        perror(path);
        _exit(127);
    }
    return pid;
}

static pid_t spawnv(char **argv)
{
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(127);
    }
    return pid;
}

int main(int argc, char **argv)
{
    pid_t wm = spawn("/bin/zwm", NULL);
    if (wm < 0) { perror("zde: fork"); return 1; }
    /* the server is up once it accepts a connection */
    zwm *c = NULL;
    for (int i = 0; i < 50 && !c; i++) {
        usleep(100000);
        int st;
        if (waitpid(wm, &st, WNOHANG) == wm) { fprintf(stderr, "zde: zwm exited (%d)\n", WEXITSTATUS(st)); return 1; }
        c = zwm_connect();
    }
    if (!c) { fprintf(stderr, "zde: zwm did not come up\n"); kill(wm, SIGTERM); return 1; }
    zwm_disconnect(c);
    pid_t panel = spawn("/bin/zpanel", NULL);
    if (argc > 1) spawnv(argv + 1);
    for (;;) {
        int st;
        pid_t p = wait(&st);
        if (p == wm) break;
        if (p == panel) panel = spawn("/bin/zpanel", NULL);     /* keep the panel around */
        if (p < 0) break;
    }
    if (panel > 0) kill(panel, SIGTERM);
    /* programs the panel started notice the server is gone and exit themselves */
    printf("zde: session ended\n");
    return 0;
}
