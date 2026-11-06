#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

/* Index of the first byte of the line that contains pos. */
size_t buffer_line_start_index(const char *data, size_t pos);

/* Index of the newline that ends the line containing pos, or len. */
size_t buffer_line_end_index(const char *data, size_t len, size_t pos);

/* 0-based row and byte column of pos. */
void buffer_row_column(const char *data, size_t len, size_t pos,
                       size_t *row, size_t *column);

/* First byte of the given 0-based row, or NULL past the last row.
 * A trailing newline opens one more empty row at the end of the buffer. */
const char *buffer_offset_of_row(const char *data, size_t len, size_t row);

#endif
