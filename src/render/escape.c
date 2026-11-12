#include "escape.h"

#include <stdio.h>

#include "../text/text.h"

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

int escape_write_text(const char *text, size_t len, size_t max_cells,
                      size_t *cells)
{
    return escape_write_text_at(text, len, 0, max_cells, cells);
}

int escape_write_text_at(const char *text, size_t len, size_t start_cell,
                         size_t max_cells, size_t *cells)
{
    size_t used = 0;
    size_t column = start_cell;
    size_t pos = 0;

    while (pos < len && used < max_cells)
    {
        unsigned char byte = (unsigned char)text[pos];

        if (byte == '\n')
        {
            break;
        }
        if (byte == '\r' && pos + 1 == len)
        {
            break;
        }
        if (byte == '\t')
        {
            size_t spaces = TEXT_TAB_WIDTH - column % TEXT_TAB_WIDTH;
            if (spaces > max_cells - used)
            {
                spaces = max_cells - used;
            }
            while (spaces > 0)
            {
                if (escape_putc(' ') != 0)
                {
                    return -1;
                }
                used++;
                column++;
                spaces--;
            }
            pos++;
            continue;
        }
        if (byte < 0x20 || byte == 0x7f)
        {
            if (max_cells - used < 2)
            {
                break;
            }
            char shown = byte == 0x7f ? '?' : (char)(byte ^ 0x40);
            if (escape_putc('^') != 0 || escape_putc(shown) != 0)
            {
                return -1;
            }
            used += 2;
            column += 2;
            pos++;
            continue;
        }

        size_t length = text_char_length(text, len, pos);
        size_t char_cells;
        int replace = 0;

        if (length == 1 && byte >= 0x80)
        {
            char_cells = 1;
            replace = 1;
        }
        else if (length == 2 && byte == 0xC2 &&
                 (unsigned char)text[pos + 1] >= 0x80 &&
                 (unsigned char)text[pos + 1] <= 0x9F)
        {
            /* C1 controls must not reach the terminal raw. */
            char_cells = 1;
            replace = 1;
        }
        else
        {
            char_cells = text_char_cells(text, len, pos);
        }

        if (char_cells > max_cells - used)
        {
            break;
        }

        if (replace)
        {
            if (escape_putc('?') != 0)
            {
                return -1;
            }
        }
        else if (escape_write(text + pos, length) != 0)
        {
            return -1;
        }

        used += char_cells;
        column += char_cells;
        pos += length;
    }

    if (cells != NULL)
    {
        *cells = used;
    }
    return 0;
}

int escape_write_styled_text(const char *text, size_t len, size_t max_cells,
                             const char *sgr, size_t *cells)
{
    return escape_write_styled_text_at(text, len, 0, max_cells, sgr, cells);
}

int escape_write_styled_text_at(const char *text, size_t len,
                                size_t start_cell, size_t max_cells,
                                const char *sgr, size_t *cells)
{
    size_t used = 0;
    int result = 0;

    if (escape_puts(sgr) != 0)
    {
        return -1;
    }

    if (escape_write_text_at(text, len, start_cell, max_cells, &used) != 0)
    {
        result = -1;
    }

    /* Never leave the terminal in a colored state, even on failure. */
    if (escape_puts("\033[0m") != 0)
    {
        return -1;
    }

    if (cells != NULL)
    {
        *cells = used;
    }
    return result;
}
