#include "text.h"

#include <stdbool.h>
#include <stdint.h>

static bool is_continuation(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

/* Decode the UTF-8 sequence at pos. Returns its byte length, or 1 when
 * the sequence is invalid; *codepoint receives the decoded value or the
 * raw byte for invalid input. */
static size_t decode_utf8(const char *data, size_t len, size_t pos,
                          uint32_t *codepoint)
{
    unsigned char first = (unsigned char)data[pos];
    size_t need;
    uint32_t value;
    uint32_t minimum;

    if (first < 0x80)
    {
        *codepoint = first;
        return 1;
    }

    if ((first & 0xE0) == 0xC0)
    {
        need = 2;
        value = first & 0x1F;
        minimum = 0x80;
    }
    else if ((first & 0xF0) == 0xE0)
    {
        need = 3;
        value = first & 0x0F;
        minimum = 0x800;
    }
    else if ((first & 0xF8) == 0xF0)
    {
        need = 4;
        value = first & 0x07;
        minimum = 0x10000;
    }
    else
    {
        *codepoint = first;
        return 1;
    }

    if (pos + need > len)
    {
        *codepoint = first;
        return 1;
    }

    for (size_t i = 1; i < need; i++)
    {
        unsigned char byte = (unsigned char)data[pos + i];
        if (!is_continuation(byte))
        {
            *codepoint = first;
            return 1;
        }
        value = (value << 6) | (byte & 0x3F);
    }

    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
    {
        *codepoint = first;
        return 1;
    }

    *codepoint = value;
    return need;
}

static bool is_combining(uint32_t cp)
{
    return (cp >= 0x0300 && cp <= 0x036F) ||
           (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) ||
           (cp >= 0x2060 && cp <= 0x206F) ||
           cp == 0x200B || cp == 0x200C || cp == 0x200D ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

static bool is_wide(uint32_t cp)
{
    return (cp >= 0x1100 && cp <= 0x115F) ||
           (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) ||
           (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xA960 && cp <= 0xA97F) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE19) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
}

static size_t codepoint_cells(uint32_t cp)
{
    if (is_combining(cp))
    {
        return 0;
    }
    if (is_wide(cp))
    {
        return 2;
    }
    return 1;
}

size_t text_char_length(const char *data, size_t len, size_t pos)
{
    uint32_t codepoint;
    return decode_utf8(data, len, pos, &codepoint);
}

size_t text_char_cells(const char *data, size_t len, size_t pos)
{
    uint32_t codepoint;
    size_t length = decode_utf8(data, len, pos, &codepoint);

    if (length == 1 && (unsigned char)data[pos] >= 0x80)
    {
        return 1;
    }
    return codepoint_cells(codepoint);
}

size_t text_width(const char *data, size_t start, size_t end)
{
    return text_width_from(data, start, end, 0);
}

size_t text_width_from(const char *data, size_t start, size_t end,
                       size_t start_cell)
{
    size_t width = 0;
    size_t column = start_cell;
    size_t pos = start;

    while (pos < end)
    {
        unsigned char byte = (unsigned char)data[pos];

        if (byte == '\n')
        {
            break;
        }
        if (byte == '\r' && pos + 1 == end)
        {
            break;
        }
        if (byte == '\t')
        {
            size_t spaces = TEXT_TAB_WIDTH - column % TEXT_TAB_WIDTH;

            width += spaces;
            column += spaces;
            pos++;
            continue;
        }
        if (byte < 0x20 || byte == 0x7f)
        {
            width += 2;
            column += 2;
            pos++;
            continue;
        }

        uint32_t codepoint;
        size_t length = decode_utf8(data, end, pos, &codepoint);
        size_t cells;

        if (length == 1 && byte >= 0x80)
        {
            cells = 1;
        }
        else
        {
            cells = codepoint_cells(codepoint);
        }

        width += cells;
        column += cells;
        pos += length;
    }

    return width;
}

size_t text_prev_offset(const char *data, size_t pos)
{
    if (pos == 0)
    {
        return 0;
    }

    size_t offset = pos - 1;
    while (offset > 0 && is_continuation((unsigned char)data[offset]))
    {
        offset--;
    }
    return offset;
}

size_t text_next_offset(const char *data, size_t len, size_t pos)
{
    return pos + text_char_length(data, len, pos);
}

size_t text_offset_at_column(const char *data, size_t start, size_t end,
                             size_t target_cells)
{
    size_t width = 0;
    size_t pos = start;

    while (pos < end)
    {
        unsigned char byte = (unsigned char)data[pos];
        size_t cells;
        size_t length;

        if (byte == '\n')
        {
            break;
        }
        if (byte == '\r' && pos + 1 == end)
        {
            pos++;
            continue;
        }
        if (byte == '\t')
        {
            cells = TEXT_TAB_WIDTH - width % TEXT_TAB_WIDTH;
            length = 1;
        }
        else if (byte < 0x20 || byte == 0x7f)
        {
            cells = 2;
            length = 1;
        }
        else
        {
            uint32_t codepoint;
            length = decode_utf8(data, end, pos, &codepoint);
            if (length == 1 && byte >= 0x80)
            {
                cells = 1;
            }
            else
            {
                cells = codepoint_cells(codepoint);
            }
        }

        if (width + cells > target_cells)
        {
            break;
        }
        width += cells;
        pos += length;
    }

    return pos;
}
