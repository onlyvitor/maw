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

int screen_carriage_return(void)
{
    return fputc('\r', stdout) == EOF ? -1 : 0;
}

int screen_erase_below(void)
{
    return screen_puts("\033[J");
}

int screen_goto_column(size_t column)
{
    if (column == 0)
    {
        return 0;
    }

    return fprintf(stdout, "\033[%zuC", column) < 0 ? -1 : 0;
}

int screen_move_left(size_t columns)
{
    if (columns == 0)
    {
        return 0;
    }

    return fprintf(stdout, "\033[%zuD", columns) < 0 ? -1 : 0;
}

int screen_move_right(size_t columns)
{
    return screen_goto_column(columns);
}

int screen_move_up(size_t rows)
{
    if (rows == 0)
    {
        return 0;
    }

    return fprintf(stdout, "\033[%zuA", rows) < 0 ? -1 : 0;
}

int screen_move_down(size_t rows)
{
    if (rows == 0)
    {
        return 0;
    }

    return fprintf(stdout, "\033[%zuB", rows) < 0 ? -1 : 0;
}

static size_t cursor_column(const EditorFile *ef, size_t cursor)
{
    size_t column = 0;

    editor_row_column(ef, cursor, NULL, &column);

    return column;
}

int screen_apply_motion(const EditorFile *ef, size_t cursor, CursorMotion motion)
{
    switch (motion)
    {
    case CURSOR_MOTION_NONE:
        return 0;
    case CURSOR_MOTION_LEFT:
        return screen_move_left(1) != 0 || screen_flush() != 0 ? -1 : 0;
    case CURSOR_MOTION_RIGHT:
        return screen_move_right(1) != 0 || screen_flush() != 0 ? -1 : 0;
    case CURSOR_MOTION_NEXT_LINE:
        return screen_puts("\r\n") != 0 || screen_flush() != 0 ? -1 : 0;
    case CURSOR_MOTION_PREVIOUS_LINE:
        return screen_move_up(1) != 0 || screen_carriage_return() != 0 ||
                       screen_goto_column(cursor_column(ef, cursor)) != 0 ||
                       screen_flush() != 0
                   ? -1
                   : 0;
    case CURSOR_MOTION_UP:
        return screen_move_up(1) != 0 || screen_carriage_return() != 0 ||
                       screen_goto_column(cursor_column(ef, cursor)) != 0 ||
                       screen_flush() != 0
                   ? -1
                   : 0;
    case CURSOR_MOTION_DOWN:
        return screen_move_down(1) != 0 || screen_carriage_return() != 0 ||
                       screen_goto_column(cursor_column(ef, cursor)) != 0 ||
                       screen_flush() != 0
                   ? -1
                   : 0;
    }

    return -1;
}

int screen_redraw_from(const EditorFile *ef, size_t start, size_t cursor)
{
    /* Move to the beginning of the line and erase everything below it. */
    if (screen_carriage_return() != 0 || screen_erase_below() != 0)
    {
        return -1;
    }

    if (screen_write(ef->data + start, ef->len - start) != 0)
    {
        return -1;
    }

    /* Count how many lines were printed after the cursor. */
    size_t rows = 0;

    for (size_t i = cursor; i < ef->len; i++)
    {
        if (ef->data[i] == '\n')
        {
            rows++;
        }
    }

    if (screen_move_up(rows) != 0 || screen_carriage_return() != 0)
    {
        return -1;
    }

    /* Move right to the column where the cursor lives. */
    size_t column = cursor - line_start_index(ef->data, cursor);

    if (screen_goto_column(column) != 0)
    {
        return -1;
    }

    return screen_flush();
}

int screen_apply_change(const Editor *ed, const EditorChange *change)
{
    int rc = 0;

    switch (change->kind)
    {
    case CHANGE_NONE:
        return 0;
    case CHANGE_CURSOR:
        return screen_apply_motion(ed->file, ed->cursor, change->motion);
    case CHANGE_LINE:
        if (change->removed_newline)
        {
            rc = screen_move_up(1);
        }

        if (rc == 0)
        {
            rc = screen_redraw_from(ed->file, change->start, ed->cursor);
        }

        return rc;
    case CHANGE_APPEND:
        return screen_putc(change->byte) != 0 || screen_flush() != 0 ? -1 : 0;
    }

    return -1;
}
