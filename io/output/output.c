#include "output.h"

#include <stdio.h>

int screen_puts(const char *s)
{
    return fputs(s, stdout) == EOF ? -1 : 0;
}

int screen_write(const char *s, size_t n)
{
    if (n == 0)
    {
        return 0;
    }

    return fwrite(s, 1, n, stdout) == n ? 0 : -1;
}

int screen_putc(char c)
{
    return putchar(c) == EOF ? -1 : 0;
}

int screen_flush(void)
{
    return fflush(stdout) == EOF ? -1 : 0;
}

int screen_goto_origin(void)
{
    return screen_puts("\033[H");
}

int screen_goto_position(size_t row, size_t column)
{
    return fprintf(stdout, "\033[%zu;%zuH", row + 1, column + 1) < 0 ? -1 : 0;
}

int screen_erase_line(void)
{
    return screen_puts("\033[K");
}

int screen_erase_below(void)
{
    return screen_puts("\033[J");
}

/* Write one logical line clipped to width bytes and hand the caller
 * the offset of the next line, or NULL when the buffer ends. */
static int write_clipped_line(const char *limit, const char *start, size_t width,
                              const char **next)
{
    const char *end = start;

    while (end < limit && *end != '\n' && (size_t)(end - start) < width)
    {
        end++;
    }

    if (screen_write(start, (size_t)(end - start)) != 0)
    {
        return -1;
    }

    /* Skip whatever did not fit until the end of the line. */
    const char *tail = end;

    while (tail < limit && *tail != '\n')
    {
        tail++;
    }

    *next = tail < limit ? tail + 1 : NULL;

    return 0;
}

/* Put the terminal cursor where the editor cursor is. The viewport has
 * already been scrolled to keep that row visible. */
static int position_cursor(const EditorFile *ef, size_t cursor, const Viewport *vp)
{
    size_t row, column;

    editor_row_column(ef, cursor, &row, &column);

    size_t screen_row = row >= vp->top_row ? row - vp->top_row : 0;

    size_t rows = viewport_size(vp).rows;

    if (screen_row > rows - 1)
    {
        screen_row = rows - 1;
    }

    size_t screen_column = column;
    size_t width = size_content_width(viewport_size(vp));

    if (screen_column > width)
    {
        screen_column = width;
    }

    return screen_goto_position(screen_row, screen_column);
}

/* Repaint every visible row and put the cursor back on screen. */
int screen_draw(const EditorFile *ef, size_t cursor, const Viewport *vp)
{
    const char *limit = ef->data + ef->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    if (screen_goto_origin() != 0)
    {
        return -1;
    }

    const char *offset = offset_of_row(ef->data, ef->len, vp->top_row);

    for (size_t screen_row = 0; screen_row < rows; screen_row++)
    {
        if (screen_row > 0 && screen_puts("\r\n") != 0)
        {
            return -1;
        }

        if (screen_erase_line() != 0)
        {
            return -1;
        }

        if (offset != NULL && write_clipped_line(limit, offset, width, &offset) != 0)
        {
            return -1;
        }
    }

    if (position_cursor(ef, cursor, vp) != 0)
    {
        return -1;
    }

    return screen_flush();
}

/* Scroll to keep the cursor on screen, then repaint whatever the key
 * dispatch reported for this editor state. */
int screen_apply_change(const EditorFile *ef, size_t cursor, Viewport *vp,
                        const EditorChange *change)
{
    (void)change;

    size_t row, column;

    editor_row_column(ef, cursor, &row, &column);
    viewport_ensure_visible(vp, row);

    return screen_draw(ef, cursor, vp);
}
