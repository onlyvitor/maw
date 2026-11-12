#ifndef MARKDOWN_H
#define MARKDOWN_H

#include <stdbool.h>
#include <stddef.h>

/* Semantic styles produced by the Markdown scanner. The renderer maps
 * them to terminal colors; this module knows nothing about escape
 * codes, terminals or I/O. */
typedef enum MdStyle
{
    MD_STYLE_NONE,
    MD_STYLE_HEADING,
    MD_STYLE_LINK,
} MdStyle;

/* A styled byte range of one line. Offsets are relative to the line
 * start; spans are sorted and never overlap. */
typedef struct MdSpan
{
    size_t start;
    size_t end;
    MdStyle style;
} MdSpan;

/* Number of spans a single line can carry before the rest is dropped. */
#define MD_MAX_SPANS 32

/* Fenced code block state carried from line to line. */
typedef struct MarkdownState
{
    bool in_fence;
    char fence_char;
    size_t fence_len;
} MarkdownState;

/* True for .md and .markdown paths (case-insensitive). */
bool markdown_applies(const char *path);

void markdown_state_init(MarkdownState *state);

/* Consume one line, update the fence state and collect its spans.
 * `line` may end with a trailing '\r'. When `out` is NULL the line is
 * only scanned for fence state. Returns the number of spans stored
 * (at most capacity). */
size_t markdown_scan_line(MarkdownState *state, const char *line, size_t len,
                          MdSpan *out, size_t capacity);

/* State at the beginning of the line that starts at data[line_start]. */
void markdown_state_at(const char *data, size_t len, size_t line_start,
                       MarkdownState *state);

#endif
