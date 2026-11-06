#include "input.h"
#include "key.h"
#include <stdio.h>

int input_scan(void)
{
    int c = getchar();

    if (c == EOF && ferror(stdin))
    {
        return EOF;
    }
    else if (c == KEY_CTRL_S)
    {
        c = KEY_CTRL_S;
    }
    else if (c == KEY_CTRL_H)
    {
        c = KEY_BACKSPACE;
    }
    else if (c == KEY_ESC)
    {
        int segment1 = getchar();
        if (segment1 == EOF) return EOF;

        if (segment1 == '[' || segment1 == 'O')
        {
            int segment2 = getchar();
            if (segment2 == EOF) return EOF;

            switch (segment2)
            {
            case 'A': c = KEY_UP; break;
            case 'B': c = KEY_DOWN; break;
            case 'C': c = KEY_RIGHT; break;
            case 'D': c = KEY_LEFT; break;
            case EOF: return EOF;
            }
        }
    }

    return c;
}
