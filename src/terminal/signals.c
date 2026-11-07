#define _POSIX_C_SOURCE 200809L

#include "signals.h"
#include "terminal.h"

#include <signal.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t resize_pending = 0;
static volatile sig_atomic_t suspend_active = 0;

static void write_screen(const char *seq, size_t len)
{
    ssize_t ignored = write(STDOUT_FILENO, seq, len);
    (void)ignored;
}

static void handle_fatal(int signum)
{
    if (rawmode_enabled)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        write_screen(TERMINAL_LEAVE_SCREEN, sizeof TERMINAL_LEAVE_SCREEN - 1);
    }
    _exit(128 + signum);
}

static void handle_winch(int signum)
{
    (void)signum;
    resize_pending = 1;
}

static void handle_tstp(int signum)
{
    (void)signum;

    if (rawmode_enabled)
    {
        suspend_active = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        write_screen(TERMINAL_LEAVE_SCREEN, sizeof TERMINAL_LEAVE_SCREEN - 1);
    }

    struct sigaction action = {.sa_handler = SIG_DFL, .sa_flags = 0};
    sigemptyset(&action.sa_mask);
    sigaction(SIGTSTP, &action, NULL);

    /* SIGTSTP is blocked while this handler runs; unblock it before
     * re-raising so the default action actually stops the process. */
    sigset_t unblock;
    sigemptyset(&unblock);
    sigaddset(&unblock, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &unblock, NULL);
    raise(SIGTSTP);

    action.sa_handler = handle_tstp;
    sigaction(SIGTSTP, &action, NULL);
}

static void handle_cont(int signum)
{
    (void)signum;

    if (suspend_active && rawmode_enabled)
    {
        suspend_active = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios);
        write_screen(TERMINAL_ENTER_SCREEN, sizeof TERMINAL_ENTER_SCREEN - 1);
        resize_pending = 1;
    }
}

int signals_take_resize(void)
{
    if (!resize_pending)
    {
        return 0;
    }
    resize_pending = 0;
    return 1;
}

int signals_install(void)
{
    static const int fatal[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    struct sigaction action = {.sa_handler = NULL, .sa_flags = 0};

    sigemptyset(&action.sa_mask);

    action.sa_handler = handle_fatal;
    for (size_t i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
    {
        if (sigaction(fatal[i], &action, NULL) == -1)
        {
            perror("error: cannot install the signal handler");
            return -1;
        }
    }

    action.sa_handler = handle_winch;
    if (sigaction(SIGWINCH, &action, NULL) == -1)
    {
        perror("error: cannot install the resize handler");
        return -1;
    }

    action.sa_handler = handle_tstp;
    if (sigaction(SIGTSTP, &action, NULL) == -1)
    {
        perror("error: cannot install the suspend handler");
        return -1;
    }

    action.sa_handler = handle_cont;
    if (sigaction(SIGCONT, &action, NULL) == -1)
    {
        perror("error: cannot install the continue handler");
        return -1;
    }

    return 0;
}
