#define _POSIX_C_SOURCE 200809L

#include "input.h"

#include <errno.h>
#include <unistd.h>

#include "../terminal/signals.h"

static int pending = -1;

static int read_byte(void)
{
    if (pending >= 0)
    {
        int byte = pending;
        pending = -1;
        return byte;
    }

    for (;;)
    {
        unsigned char byte;
        ssize_t count = read(STDIN_FILENO, &byte, 1);

        if (count == 1)
        {
            return (int)byte;
        }
        if (count == 0)
        {
            return INPUT_EOF;
        }
        if (errno == EINTR)
        {
            if (signals_take_resize())
            {
                return KEY_RESIZE;
            }
            continue;
        }
        return INPUT_ERROR;
    }
}

static int read_arrow(void)
{
    int segment1 = read_byte();
    if (segment1 < 0 || segment1 == KEY_RESIZE)
    {
        return segment1;
    }

    if (segment1 != '[' && segment1 != 'O')
    {
        /* A lone ESC followed by a normal key: keep the key for the
         * next call instead of swallowing it. */
        pending = segment1;
        return KEY_ESC;
    }

    int segment2 = read_byte();
    if (segment2 < 0 || segment2 == KEY_RESIZE)
    {
        return segment2;
    }

    switch (segment2)
    {
    case 'A':
        return KEY_UP;
    case 'B':
        return KEY_DOWN;
    case 'C':
        return KEY_RIGHT;
    case 'D':
        return KEY_LEFT;
    default:
        return KEY_ESC;
    }
}

int input_scan(void)
{
    int key = read_byte();

    if (key < 0 || key == KEY_RESIZE)
    {
        return key;
    }
    if (key == KEY_CTRL_H)
    {
        return KEY_BACKSPACE;
    }
    if (key == KEY_ESC)
    {
        return read_arrow();
    }
    return key;
}
