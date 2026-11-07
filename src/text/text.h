#ifndef TEXT_H
#define TEXT_H

#include <stddef.h>

#define TEXT_TAB_WIDTH 8

/* Byte length (1..4) of the UTF-8 sequence starting at data[pos].
 * Invalid or truncated sequences count as one byte. */
size_t text_char_length(const char *data, size_t len, size_t pos);

/* Display cells of the character at data[pos]: 0 for combining marks,
 * 2 for wide or emoji characters, otherwise 1. Tabs are handled by
 * text_width and escape_write_text. */
size_t text_char_cells(const char *data, size_t len, size_t pos);

/* Display cells of data[start..end), expanding tabs at TEXT_TAB_WIDTH
 * stops. A '\r' that ends the slice (a CRLF ending) takes no cells. */
size_t text_width(const char *data, size_t start, size_t end);

/* Byte offset of the previous/next character boundary. */
size_t text_prev_offset(const char *data, size_t pos);
size_t text_next_offset(const char *data, size_t len, size_t pos);

/* Byte offset in data[start..end) that best matches target_cells.
 * Targets inside a tab or a wide character stop before it. */
size_t text_offset_at_column(const char *data, size_t start, size_t end,
                             size_t target_cells);

#endif
