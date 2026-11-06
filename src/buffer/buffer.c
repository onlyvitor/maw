#include "buffer.h"

size_t buffer_line_start_index(const char *data, size_t pos)
{
    size_t start = pos;

    while (start > 0 && data[start - 1] != '\n')
    {
        start--;
    }

    return start;
}

size_t buffer_line_end_index(const char *data, size_t len, size_t pos)
{
    size_t end = pos;

    while (end < len && data[end] != '\n')
    {
        end++;
    }

    return end;
}

void buffer_row_column(const char *data, size_t len, size_t pos,
                       size_t *row, size_t *column)
{
    (void)len;

    if (row != NULL)
    {
        size_t r = 0;

        for (size_t i = 0; i < pos; i++)
        {
            if (data[i] == '\n')
            {
                r++;
            }
        }

        *row = r;
    }

    if (column != NULL)
    {
        *column = pos - buffer_line_start_index(data, pos);
    }
}

const char *buffer_offset_of_row(const char *data, size_t len, size_t row)
{
    if (row == 0)
    {
        return data;
    }

    for (size_t i = 0; i < len; i++)
    {
        if (data[i] == '\n')
        {
            row--;

            if (row == 0)
            {
                return data + i + 1;
            }
        }
    }

    return NULL;
}
