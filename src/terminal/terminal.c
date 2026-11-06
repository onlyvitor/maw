#define _POSIX_C_SOURCE 200809L

#include "terminal.h"
#include "signals.h"

#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct termios saved_termios;
volatile sig_atomic_t rawmode_enabled = 0;

static const char enter_screen[] = "\033[?1049h\033[H\033[2J";
static const char leave_screen[] = "\033[?1049l";

static int write_screen(const char *seq)
{
    return fputs(seq, stdout) == EOF || fflush(stdout) == EOF ? -1 : 0;
}

int terminal_rawmode_on(void)
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

    signals_install();

    struct termios raw = saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | IXOFF | IXANY);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
    {
        perror("error: cannot enable the raw mode");
        return -1;
    }

    if (write_screen(enter_screen) != 0)
    {
        perror("error: cannot switch the screen");
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
        return -1;
    }

    rawmode_enabled = 1;
    return 0;
}

int terminal_rawmode_off(void)
{
    if (!rawmode_enabled)
    {
        return 0;
    }

    int rc = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) == -1)
    {
        perror("error: cannot restore the terminal attributes");
        rc = -1;
    }

    rawmode_enabled = 0;

    if (write_screen(leave_screen) != 0)
    {
        perror("error: cannot switch the screen");
        rc = -1;
    }

    return rc;
}

Size terminal_size(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_row == 0 || ws.ws_col == 0)
    {
        Size fallback = {24, 80};
        return fallback;
    }
    Size size = {(size_t)ws.ws_row, (size_t)ws.ws_col};
    return size;
}

int terminal_is_interactive(void)
{
    return isatty(STDIN_FILENO) != 0;
}
