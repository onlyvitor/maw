#include "input.h"

int scan(void)
{
    int c = getchar();

    if (c == EOF && ferror(stdin))
    {
        fprintf(stderr, "error: cannot read the character\n");
    }
    else if (c == CTRL_S)
    {
        c = CTRL_S;
    }
    else if (c == CTRL_H)
    {
        c = KEY_BACKSPACE;
    }
    else if (c == ESC)
    {
        int segment1 = getchar();

        if (segment1 == EOF)
        {
            return EOF;
        }

        /* CSI (ESC [) and SS3 (ESC O) both introduce cursor key sequences. */
        if (segment1 == '[' || segment1 == 'O')
        {
            switch (getchar())
            {
            case 'A':
                c = UP_ARROW;
                break;
            case 'B':
                c = DOWN_ARROW;
                break;
            case 'C':
                c = RIGHT_ARROW;
                break;
            case 'D':
                c = LEFT_ARROW;
                break;
            case EOF:
                return EOF;
            }
        }
    }

    return c;
}
