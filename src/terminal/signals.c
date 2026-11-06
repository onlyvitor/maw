#define _POSIX_C_SOURCE 200809L

#include "signals.h"

#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

extern struct termios saved_termios;
extern volatile sig_atomic_t rawmode_enabled;

static const char leave_screen[] = "\033[?1049l";

static void handle_signal(int signum)
{
    if (rawmode_enabled)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        write(STDOUT_FILENO, leave_screen, sizeof(leave_screen) - 1);
    }
    _exit(128 + signum);
}

int signals_install(void)
{
    static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    struct sigaction action = {
        .sa_handler = handle_signal,
        .sa_flags = 0,
    };

    sigemptyset(&action.sa_mask);

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
    {
        if (sigaction(signals[i], &action, NULL) == -1)
        {
            perror("error: cannot install the signal handler");
            return -1;
        }
    }
    return 0;
}
