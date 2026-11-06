#include "output.h"

#include <stdio.h>
#include <string.h>

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
static int position_cursor(const Editor *ed, const Viewport *vp)
{
    size_t row, column;

    editor_row_column(ed->file, ed->cursor, &row, &column);

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

/* Paint the status line. It states where the editing is: the file
 * name, a marker for pending edits and the 1-based cursor position,
 * in reverse video. */
static int write_status(const Editor *ed, const Viewport *vp)
{
    size_t width = size_content_width(viewport_size(vp));

    size_t row, column;

    editor_row_column(ed->file, ed->cursor, &row, &column);

    char text[1024];

    int written = snprintf(text, sizeof text, "%s%s %zu,%zu", ed->file->path,
                           ed->dirty ? " *" : "", row + 1, column + 1);

    if (written < 0)
    {
        return -1;
    }

    size_t len = (size_t)written < sizeof text - 1 ? (size_t)written : sizeof text - 1;

    if (len > width)
    {
        len = width;
    }

    if (screen_goto_position(viewport_size(vp).rows, 0) != 0 ||
        screen_erase_line() != 0)
    {
        return -1;
    }

    if (screen_puts("\033[7m") != 0 || screen_write(text, len) != 0 ||
        screen_puts("\033[0m") != 0)
    {
        return -1;
    }

    return 0;
}

/* Bring the status line up to date and put the cursor back. */
static int refresh_status(const Editor *ed, const Viewport *vp)
{
    if (write_status(ed, vp) != 0 || position_cursor(ed, vp) != 0)
    {
        return -1;
    }

    return screen_flush();
}

int screen_draw(const Editor *ed, const Viewport *vp)
{
    const char *limit = ed->file->data + ed->file->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    if (screen_goto_origin() != 0)
    {
        return -1;
    }

    const char *offset = offset_of_row(ed->file->data, ed->file->len, vp->top_row);

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

    if (write_status(ed, vp) != 0)
    {
        return -1;
    }

    if (position_cursor(ed, vp) != 0)
    {
        return -1;
    }

    return screen_flush();
}

/* Repaint from the row that contains start down to the bottom of the
 * viewport, status line included, then put the cursor back. */
static int redraw_from(const Editor *ed, const Viewport *vp, size_t start)
{
    const char *limit = ed->file->data + ed->file->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    size_t start_row, column;

    editor_row_column(ed->file, start, &start_row, &column);

    size_t first_screen_row = start_row - vp->top_row;

    /* Move to the beginning of the affected row and erase below it. */
    if (screen_goto_position(first_screen_row, 0) != 0 || screen_erase_below() != 0)
    {
        return -1;
    }

    const char *offset = ed->file->data + start;

    for (size_t screen_row = first_screen_row; screen_row < rows; screen_row++)
    {
        if (screen_row > first_screen_row && screen_puts("\r\n") != 0)
        {
            return -1;
        }

        if (offset == NULL)
        {
            /* The buffer ran out; the rows stay erased below. */
            continue;
        }

        const char *next;

        if (write_clipped_line(limit, offset, width, &next) != 0)
        {
            return -1;
        }

        offset = next;
    }

    /* The erase-below reached the status line, so repaint it too. */
    if (write_status(ed, vp) != 0)
    {
        return -1;
    }

    if (position_cursor(ed, vp) != 0)
    {
        return -1;
    }

    return screen_flush();
}

/* The cursor moved without changing the buffer: the status text has
 * to name the new position. */
static int move_cursor(const Editor *ed, const Viewport *vp)
{
    return refresh_status(ed, vp);
}

/* A byte was appended at the end of the buffer. Echo it in place while
 * it fits on screen; the newline and the clipped tail repaint the row. */
static int append_byte(const Editor *ed, const Viewport *vp, char byte)
{
    size_t row, column;

    editor_row_column(ed->file, ed->cursor, &row, &column);

    if (byte == '\n' || column > size_content_width(viewport_size(vp)))
    {
        return redraw_from(ed, vp, line_start_index(ed->file->data, ed->cursor));
    }

    if (screen_putc(byte) != 0)
    {
        return -1;
    }

    /* The echoed byte moved the cursor, so the position on the status
     * line has moved too. */
    return refresh_status(ed, vp);
}

/* Repaint whatever the key dispatch reported for this editor state. */
int screen_apply_change(const Editor *ed, Viewport *vp, Size size,
                        const EditorChange *change)
{
    size_t row, column;

    editor_row_column(ed->file, ed->cursor, &row, &column);

    /* A new geometry invalidates every row, so repaint the whole
     * window and make sure the cursor is still in it. */
    if (viewport_resize(vp, size_viewport(size)))
    {
        viewport_ensure_visible(vp, row);

        return screen_draw(ed, vp);
    }

    /* Scrolling invalidates every row as well. */
    if (viewport_ensure_visible(vp, row))
    {
        return screen_draw(ed, vp);
    }

    switch (change->kind)
    {
    case CHANGE_NONE:
        /* Ctrl+S clears the dirty flag: the status marker goes too. */
        return refresh_status(ed, vp);
    case CHANGE_CURSOR:
        if (change->motion == CURSOR_MOTION_NONE)
        {
            return refresh_status(ed, vp);
        }

        return move_cursor(ed, vp);
    case CHANGE_LINE:
        return redraw_from(ed, vp, change->start);
    case CHANGE_APPEND:
        return append_byte(ed, vp, change->byte);
    }

    return -1;
}
