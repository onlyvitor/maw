#include "render.h"
#include "escape.h"
#include "status.h"
#include "../viewport/viewport.h"
#include "../viewport/geometry.h"
#include "../buffer/buffer.h"
#include "../text/text.h"
#include "../editor/editor_types.h"
#include "../syntax/markdown.h"

/* Terminal palette for the semantic Markdown styles. 16-color SGR
 * sequences keep the editor usable on any ANSI terminal. */
static const char *md_style_code(MdStyle style)
{
    switch (style)
    {
    case MD_STYLE_HEADING:
        return "\033[1;33m"; /* bold yellow */
    case MD_STYLE_LINK:
        return "\033[4;34m"; /* underlined blue */
    case MD_STYLE_NONE:
    default:
        return NULL;
    }
}

/* Write a line as a sequence of plain and styled byte ranges, clipping
 * to `width` display cells. */
static int write_spans(const char *text, size_t len, size_t width,
                       const MdSpan *spans, size_t span_count)
{
    size_t pos = 0;
    size_t used = 0;

    for (size_t i = 0; i < span_count && used < width; i++)
    {
        const MdSpan *span = &spans[i];
        const char *code = md_style_code(span->style);

        if (span->start < pos || span->end > len)
        {
            continue;
        }

        if (span->start > pos)
        {
            size_t gap = span->start - pos;
            size_t expected = text_width_from(text + pos, 0, gap, used);
            size_t cells = 0;

            if (escape_write_text_at(text + pos, gap, used, width - used,
                                     &cells) != 0)
            {
                return -1;
            }

            used += cells;
            pos = span->start;

            if (cells < expected)
            {
                /* Clipped inside the gap; stop the line here. */
                return 0;
            }
            if (used >= width)
            {
                break;
            }
        }

        size_t span_len = span->end - span->start;
        size_t expected = text_width_from(text + span->start, 0, span_len, used);
        size_t cells = 0;

        if (code == NULL)
        {
            if (escape_write_text_at(text + span->start, span_len, used,
                                     width - used, &cells) != 0)
            {
                return -1;
            }
        }
        else if (escape_write_styled_text_at(text + span->start, span_len, used,
                                             width - used, code, &cells) != 0)
        {
            return -1;
        }

        used += cells;
        pos = span->end;

        if (cells < expected)
        {
            /* Clipped inside the span; stop the line here. */
            return 0;
        }
    }

    if (used < width && pos < len)
    {
        return escape_write_text_at(text + pos, len - pos, used, width - used,
                                    NULL);
    }
    return 0;
}

static int write_clipped_line(const char *limit, const char *start, size_t width,
                              MarkdownState *markdown, const char **next)
{
    const char *end = start;
    while (end < limit && *end != '\n')
    {
        end++;
    }

    MdSpan spans[MD_MAX_SPANS];
    size_t span_count = 0;

    if (markdown != NULL)
    {
        span_count = markdown_scan_line(markdown, start,
                                        (size_t)(end - start), spans,
                                        MD_MAX_SPANS);
    }

    if (write_spans(start, (size_t)(end - start), width, spans,
                    span_count) != 0)
    {
        return -1;
    }

    *next = end < limit ? end + 1 : NULL;
    return 0;
}

int render_draw(const Editor *ed, const Viewport *vp)
{
    const char *limit = ed->file->data + ed->file->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    if (escape_goto_origin() != 0) return -1;

    const char *offset = buffer_offset_of_row(ed->file->data, ed->file->len, vp->top_row);

    bool markdown = markdown_applies(ed->file->path);
    MarkdownState state;
    markdown_state_init(&state);

    if (markdown && offset != NULL)
    {
        markdown_state_at(ed->file->data, ed->file->len,
                          (size_t)(offset - ed->file->data), &state);
    }

    for (size_t screen_row = 0; screen_row < rows; screen_row++)
    {
        if (screen_row > 0 && escape_puts("\r\n") != 0) return -1;
        if (escape_erase_line() != 0) return -1;

        if (offset != NULL &&
            write_clipped_line(limit, offset, width,
                               markdown ? &state : NULL, &offset) != 0)
            return -1;
    }

    if (status_paint(ed, vp) != 0) return -1;
    if (status_position_cursor(ed, vp) != 0) return -1;
    return escape_flush();
}

static int redraw_from(const Editor *ed, const Viewport *vp, size_t start)
{
    const char *limit = ed->file->data + ed->file->len;
    size_t width = size_content_width(viewport_size(vp));
    size_t rows = viewport_size(vp).rows;

    size_t start_row;
    buffer_row_column(ed->file->data, ed->file->len, start, &start_row, NULL);

    size_t first_screen_row = start_row - vp->top_row;

    if (escape_goto_position(first_screen_row, 0) != 0 ||
        escape_erase_below() != 0)
        return -1;

    const char *offset = ed->file->data + start;

    bool markdown = markdown_applies(ed->file->path);
    MarkdownState state;
    markdown_state_init(&state);

    if (markdown)
    {
        markdown_state_at(ed->file->data, ed->file->len, start, &state);
    }

    for (size_t screen_row = first_screen_row; screen_row < rows; screen_row++)
    {
        if (screen_row > first_screen_row && escape_puts("\r\n") != 0)
            return -1;

        if (offset == NULL)
            continue;

        const char *next;
        if (write_clipped_line(limit, offset, width,
                               markdown ? &state : NULL, &next) != 0)
            return -1;
        offset = next;
    }

    if (status_paint(ed, vp) != 0) return -1;
    if (status_position_cursor(ed, vp) != 0) return -1;
    return escape_flush();
}

static int append_byte(const Editor *ed, const Viewport *vp, char byte)
{
    size_t start = buffer_line_start_index(ed->file->data, ed->cursor);
    size_t column = text_width(ed->file->data, start, ed->cursor);
    unsigned char value = (unsigned char)byte;

    if (markdown_applies(ed->file->path) ||
        value < 0x20 || value == 0x7f || value >= 0x80 ||
        column > size_content_width(viewport_size(vp)))
    {
        return redraw_from(ed, vp, start);
    }

    if (escape_putc(byte) != 0) return -1;
    return status_refresh(ed, vp);
}

int render_apply_change(const Editor *ed, Viewport *vp, const EditorChange *change)
{
    size_t row;
    buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &row, NULL);

    if (viewport_ensure_visible(vp, row))
    {
        return render_draw(ed, vp);
    }

    switch (change->kind)
    {
    case CHANGE_NONE:
        return status_refresh(ed, vp);
    case CHANGE_CURSOR:
        return status_refresh(ed, vp);
    case CHANGE_LINE:
        return redraw_from(ed, vp, change->start);
    case CHANGE_APPEND:
        return append_byte(ed, vp, change->byte);
    }
    return -1;
}
