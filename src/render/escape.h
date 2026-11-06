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

#endif
