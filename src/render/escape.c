#include "escape.h"
#include <stdio.h>

int escape_puts(const char *s)
{
    return fputs(s, stdout) == EOF ? -1 : 0;
}

int escape_write(const char *s, size_t n)
{
    if (n == 0) return 0;
    return fwrite(s, 1, n, stdout) == n ? 0 : -1;
}

int escape_putc(char c)
{
    return putchar(c) == EOF ? -1 : 0;
}

int escape_flush(void)
{
    return fflush(stdout) == EOF ? -1 : 0;
}

int escape_goto_origin(void)
{
    return escape_puts("\033[H");
}

int escape_goto_position(size_t row, size_t column)
{
    return fprintf(stdout, "\033[%zu;%zuH", row + 1, column + 1) < 0 ? -1 : 0;
}

int escape_erase_line(void)
{
    return escape_puts("\033[K");
}

int escape_erase_below(void)
{
    return escape_puts("\033[J");
}
