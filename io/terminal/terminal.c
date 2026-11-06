/* Feature-test macro: must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include "terminal.h"

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved_termios;
static volatile sig_atomic_t rawmode_enabled = 0;

/* Enter the alternate screen and clear it; leave it on the way out so
 * the shell history stays untouched. */
static const char enter_screen[] = "\033[?1049h\033[H\033[2J";
static const char leave_screen[] = "\033[?1049l";

/* Write one of the screen switches above (and its flush) to stdout. */
static int write_screen(const char *sequence)
{
    return fputs(sequence, stdout) == EOF || fflush(stdout) == EOF ? -1 : 0;
}

static void handle_signal(int signum)
{
    if (rawmode_enabled)
    {
        /* tcsetattr, write and _exit are async-signal-safe */
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);

        /* Leave the alternate screen even on a fatal signal, so the
         * shell is usable afterwards. */
        (void)write(STDOUT_FILENO, leave_screen, sizeof(leave_screen) - 1);
    }

    _exit(128 + signum);
}

static void install_signal_handlers(void)
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
        }
    }
}

// enter raw mode for editing the input
int rawmode_on(void)
{
    if (rawmode_enabled)
    {
        return 0;
    }

    if (!isatty(STDIN_FILENO))
    {
        return 0;
    }

    if (tcgetattr(STDIN_FILENO, &saved_termios) == -1)
    {
        perror("error: cannot get the terminal attributes");
        return -1;
    }

    install_signal_handlers();

    struct termios raw = saved_termios;

    // disable line buffering and automatic echo
    raw.c_lflag &= ~(ICANON | ECHO);

    // disable software flow control so ^S reaches the editor instead of
    // stopping the terminal output
    raw.c_iflag &= ~(IXON | IXOFF | IXANY);

    // read one byte at a time, without a timeout
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    // apply the new settings
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
    {
        perror("error: cannot enable the raw mode");
        return -1;
    }

    rawmode_enabled = 1;

    /* Do not leave the terminal raw if the screen switch fails. */
    if (write_screen(enter_screen) != 0)
    {
        perror("error: cannot switch the screen");
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        rawmode_enabled = 0;

        return -1;
    }

    return 0;
}

// restore the default terminal configuration and the normal screen
int rawmode_off(void)
{
    if (!rawmode_enabled)
    {
        return 0;
    }

    if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) == -1)
    {
        rawmode_enabled = 0;
        perror("error: cannot restore the terminal attributes");

        /* Leave the alternate screen even when the attributes could
         * not be restored, so the shell is usable either way. */
        if (write_screen(leave_screen) != 0)
        {
            perror("error: cannot switch the screen");
        }

        return -1;
    }

    rawmode_enabled = 0;

    if (write_screen(leave_screen) != 0)
    {
        perror("error: cannot switch the screen");

        return -1;
    }

    return 0;
}

// ask the kernel for the current window geometry
Size terminal_size(void)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0 || ws.ws_col == 0)
    {
        return size_fallback();
    }

    return size_new(ws.ws_row, ws.ws_col);
}
