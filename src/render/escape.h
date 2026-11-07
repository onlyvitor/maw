#ifndef ESCAPE_H
#define ESCAPE_H

#include <stddef.h>

/* Low-level escape code primitives. Return 0 on success, -1 on write failure. */

int escape_puts(const char *s);
int escape_write(const char *s, size_t n);
int escape_putc(char c);
int escape_flush(void);

int escape_goto_origin(void);
int escape_goto_position(size_t row, size_t column);
int escape_erase_line(void);
int escape_erase_below(void);

/* Write text clipped to max_cells display cells. Tabs expand to spaces,
 * control bytes become caret notation and invalid UTF-8 becomes '?', so
 * file contents cannot inject escape sequences into the terminal.
 * Returns 0 on success and stores the cells written in *cells. */
int escape_write_text(const char *text, size_t len, size_t max_cells,
                      size_t *cells);

#endif
