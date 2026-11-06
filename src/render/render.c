#include "render.h"
#include "escape.h"
#include "status.h"
#include "../viewport/viewport.h"
#include "../viewport/geometry.h"
#include "../buffer/buffer.h"
#include "../editor/editor_types.h"

static int write_clipped_line(const char *limit, const char *start, size_t width,
                              const char **next)
{
    const char *end = start;
    while (end < limit && *end != '\n' && (size_t)(end - start) < width)
        end++;

    if (escape_write(start, (size_t)(end - start)) != 0)
        return -1;

    const char *tail = end;
    while (tail < limit && *tail != '\n')
        tail++;

    *next = tail < limit ? tail + 1 : NULL;
    return 0;
}

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

int render_draw(const Editor *ed, const Viewport *vp)
{
    const char *limit = ed->file->data + ed->file->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    if (escape_goto_origin() != 0) return -1;

    const char *offset = buffer_offset_of_row(ed->file->data, ed->file->len, vp->top_row);

    for (size_t screen_row = 0; screen_row < rows; screen_row++)
    {
        if (screen_row > 0 && escape_puts("\r\n") != 0) return -1;
        if (escape_erase_line() != 0) return -1;

        if (offset != NULL && write_clipped_line(limit, offset, width, &offset) != 0)
            return -1;
    }

    if (status_paint(ed, vp) != 0) return -1;
    if (position_cursor(ed, vp) != 0) return -1;
    return escape_flush();
}

static int redraw_from(const Editor *ed, const Viewport *vp, size_t start)
{
    const char *limit = ed->file->data + ed->file->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    size_t start_row, column;
    buffer_row_column(ed->file->data, ed->file->len, start, &start_row, &column);

    size_t first_screen_row = start_row - vp->top_row;

    if (escape_goto_position(first_screen_row, 0) != 0 ||
        escape_erase_below() != 0)
        return -1;

    const char *offset = ed->file->data + start;

    for (size_t screen_row = first_screen_row; screen_row < rows; screen_row++)
    {
        if (screen_row > first_screen_row && escape_puts("\r\n") != 0)
            return -1;

        if (offset == NULL)
            continue;

        const char *next;
        if (write_clipped_line(limit, offset, width, &next) != 0)
            return -1;
        offset = next;
    }

    if (status_paint(ed, vp) != 0) return -1;
    if (position_cursor(ed, vp) != 0) return -1;
    return escape_flush();
}

static int move_cursor(const Editor *ed, const Viewport *vp)
{
    return status_refresh(ed, vp);
}

static int append_byte(const Editor *ed, const Viewport *vp, char byte)
{
    size_t row, column;
    buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &row, &column);

    if (byte == '\n' || column > size_content_width(viewport_size(vp)))
    {
        return redraw_from(ed, vp, buffer_line_start_index(ed->file->data, ed->cursor));
    }

    if (escape_putc(byte) != 0) return -1;
    return status_refresh(ed, vp);
}

int render_apply_change(const Editor *ed, Viewport *vp, const EditorChange *change)
{
    size_t row, column;
    buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &row, &column);

    if (viewport_ensure_visible(vp, row))
    {
        return render_draw(ed, vp);
    }

    switch (change->kind)
    {
    case CHANGE_NONE:
        return status_refresh(ed, vp);
    case CHANGE_CURSOR:
        if (change->motion == CURSOR_MOTION_NONE)
            return status_refresh(ed, vp);
        return move_cursor(ed, vp);
    case CHANGE_LINE:
        return redraw_from(ed, vp, change->start);
    case CHANGE_APPEND:
        return append_byte(ed, vp, change->byte);
    }
    return -1;
}
