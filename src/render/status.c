#include "status.h"
#include "escape.h"
#include "../viewport/geometry.h"
#include "../buffer/buffer.h"
#include <stdio.h>

static int position_cursor(const Editor *ed, const Viewport *vp)
{
    size_t row, column;
    buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &row, &column);

    size_t screen_row = row >= vp->top_row ? row - vp->top_row : 0;
    size_t rows = viewport_size(vp).rows;
    if (screen_row > rows - 1) screen_row = rows - 1;

    size_t screen_column = column;
    size_t width = size_content_width(viewport_size(vp));
    if (screen_column > width) screen_column = width;

    return escape_goto_position(screen_row, screen_column);
}

int status_paint(const Editor *ed, const Viewport *vp)
{
    size_t width = size_content_width(viewport_size(vp));

    size_t row, column;
    buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &row, &column);

    char text[1024];
    int written = snprintf(text, sizeof text, "%s%s %zu,%zu",
                           ed->file->path, ed->dirty ? " *" : "", row + 1, column + 1);
    if (written < 0) return -1;

    size_t len = (size_t)written;
    if (len > sizeof text - 1) len = sizeof text - 1;
    if (len > width) len = width;

    if (escape_goto_position(viewport_size(vp).rows, 0) != 0 ||
        escape_erase_line() != 0)
        return -1;

    if (escape_puts("\033[7m") != 0 ||
        escape_write(text, len) != 0 ||
        escape_puts("\033[0m") != 0)
        return -1;

    return 0;
}

int status_refresh(const Editor *ed, const Viewport *vp)
{
    if (status_paint(ed, vp) != 0) return -1;
    if (position_cursor(ed, vp) != 0) return -1;
    return escape_flush();
}
