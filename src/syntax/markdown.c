#include "markdown.h"

#include <string.h>

/* CommonMark allows at most three spaces before a fence or an ATX
 * heading marker. */
#define MD_MAX_INDENT 3

static bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

static char lower(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool suffix_is(const char *path, size_t len, const char *suffix)
{
    size_t suffix_len = strlen(suffix);

    if (len < suffix_len)
    {
        return false;
    }

    for (size_t i = 0; i < suffix_len; i++)
    {
        if (lower(path[len - suffix_len + i]) != suffix[i])
        {
            return false;
        }
    }
    return true;
}

bool markdown_applies(const char *path)
{
    if (path == NULL)
    {
        return false;
    }

    size_t len = strlen(path);
    return suffix_is(path, len, ".md") || suffix_is(path, len, ".markdown");
}

void markdown_state_init(MarkdownState *state)
{
    state->in_fence = false;
    state->fence_char = '\0';
    state->fence_len = 0;
}

/* A fence prefix is up to three spaces followed by at least three
 * backticks or tildes. `content` is the first byte after the run. */
typedef struct FencePrefix
{
    bool found;
    char ch;
    size_t len;
    size_t content;
} FencePrefix;

static FencePrefix fence_prefix(const char *line, size_t len)
{
    FencePrefix prefix = {false, '\0', 0, 0};
    size_t i = 0;

    while (i < len && line[i] == ' ')
    {
        i++;
    }

    if (i > MD_MAX_INDENT || i >= len)
    {
        return prefix;
    }

    char ch = line[i];
    if (ch != '`' && ch != '~')
    {
        return prefix;
    }

    size_t run = 0;
    while (i + run < len && line[i + run] == ch)
    {
        run++;
    }

    if (run < 3)
    {
        return prefix;
    }

    prefix.found = true;
    prefix.ch = ch;
    prefix.len = run;
    prefix.content = i + run;
    return prefix;
}

static bool rest_is_blank(const char *line, size_t len, size_t pos)
{
    for (size_t i = pos; i < len; i++)
    {
        if (!is_blank(line[i]))
        {
            return false;
        }
    }
    return true;
}

static bool is_atx_heading(const char *line, size_t len)
{
    size_t i = 0;

    while (i < len && line[i] == ' ')
    {
        i++;
    }

    if (i > MD_MAX_INDENT)
    {
        return false;
    }

    size_t hashes = 0;
    while (i + hashes < len && line[i + hashes] == '#')
    {
        hashes++;
    }

    if (hashes < 1 || hashes > 6)
    {
        return false;
    }

    size_t after = i + hashes;
    return after == len || is_blank(line[after]);
}

/* Byte offset after the closing run of an inline code span that starts
 * at `pos`, or false when the run is not closed on this line. */
static bool code_span_end(const char *line, size_t len, size_t pos,
                          size_t *end)
{
    size_t open_end = pos;

    while (open_end < len && line[open_end] == '`')
    {
        open_end++;
    }

    size_t run = open_end - pos;
    size_t i = open_end;

    while (i < len)
    {
        if (line[i] != '`')
        {
            i++;
            continue;
        }

        size_t start = i;
        while (i < len && line[i] == '`')
        {
            i++;
        }

        if (i - start == run)
        {
            *end = i;
            return true;
        }
    }

    return false;
}

/* Byte offset after the closing ')' of an inline link whose '[' is at
 * `open`. Handles escaped bytes, nested brackets in the text and
 * balanced parentheses in the destination. */
static bool link_end(const char *line, size_t len, size_t open, size_t *end)
{
    size_t i = open + 1;
    size_t depth = 1;

    while (i < len)
    {
        char c = line[i];

        if (c == '\\' && i + 1 < len)
        {
            i += 2;
            continue;
        }
        if (c == '[')
        {
            depth++;
        }
        else if (c == ']')
        {
            depth--;
            if (depth == 0)
            {
                break;
            }
        }
        i++;
    }

    if (i >= len || line[i] != ']')
    {
        return false;
    }

    size_t paren = i + 1;
    if (paren >= len || line[paren] != '(')
    {
        return false;
    }

    size_t j = paren + 1;
    size_t parens = 1;

    while (j < len)
    {
        char c = line[j];

        if (c == '\\' && j + 1 < len)
        {
            j += 2;
            continue;
        }
        if (c == '(')
        {
            parens++;
        }
        else if (c == ')')
        {
            parens--;
            if (parens == 0)
            {
                *end = j + 1;
                return true;
            }
        }
        j++;
    }

    return false;
}

static void add_span(MdSpan *out, size_t capacity, size_t *count,
                     size_t start, size_t end, MdStyle style)
{
    if (start >= end || *count >= capacity)
    {
        return;
    }

    out[*count].start = start;
    out[*count].end = end;
    out[*count].style = style;
    (*count)++;
}

static size_t scan_inline(const char *line, size_t len, MdSpan *out,
                          size_t capacity)
{
    size_t count = 0;
    size_t i = 0;

    while (i < len && count < capacity)
    {
        char c = line[i];

        if (c == '\\')
        {
            i += (i + 1 < len) ? 2 : 1;
            continue;
        }

        if (c == '`')
        {
            size_t end;

            if (code_span_end(line, len, i, &end))
            {
                i = end;
                continue;
            }

            while (i < len && line[i] == '`')
            {
                i++;
            }
            continue;
        }

        if (c == '[' || (c == '!' && i + 1 < len && line[i + 1] == '['))
        {
            size_t open = c == '[' ? i : i + 1;
            size_t end;

            if (link_end(line, len, open, &end))
            {
                add_span(out, capacity, &count, i, end, MD_STYLE_LINK);
                i = end;
                continue;
            }
        }

        i++;
    }

    return count;
}

size_t markdown_scan_line(MarkdownState *state, const char *line, size_t len,
                          MdSpan *out, size_t capacity)
{
    if (len > 0 && line[len - 1] == '\r')
    {
        len--;
    }

    FencePrefix fence = fence_prefix(line, len);

    if (state->in_fence)
    {
        if (fence.found && fence.ch == state->fence_char &&
            fence.len >= state->fence_len &&
            rest_is_blank(line, len, fence.content))
        {
            state->in_fence = false;
            state->fence_char = '\0';
            state->fence_len = 0;
        }
        return 0;
    }

    if (fence.found)
    {
        state->in_fence = true;
        state->fence_char = fence.ch;
        state->fence_len = fence.len;
        return 0;
    }

    if (is_atx_heading(line, len))
    {
        if (out != NULL && capacity > 0)
        {
            out[0].start = 0;
            out[0].end = len;
            out[0].style = MD_STYLE_HEADING;
            return 1;
        }
        return 0;
    }

    if (out == NULL || capacity == 0)
    {
        return 0;
    }

    return scan_inline(line, len, out, capacity);
}

void markdown_state_at(const char *data, size_t len, size_t line_start,
                       MarkdownState *state)
{
    markdown_state_init(state);

    if (data == NULL)
    {
        return;
    }

    if (line_start > len)
    {
        line_start = len;
    }

    size_t pos = 0;
    while (pos < line_start)
    {
        size_t end = pos;

        while (end < len && data[end] != '\n')
        {
            end++;
        }

        markdown_scan_line(state, data + pos, end - pos, NULL, 0);

        if (end >= len)
        {
            break;
        }
        pos = end + 1;
    }
}
