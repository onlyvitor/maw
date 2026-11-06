#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <stddef.h>

/* Terminal geometry in cells. */
typedef struct Size
{
    size_t rows;
    size_t columns;
} Size;

/* 80x24, the conventional fallback for terminals that report nothing. */
Size size_fallback(void);

/* Clamp both cell counts to at least one. */
Size size_new(size_t rows, size_t columns);

/* How many bytes of a row fit on screen. The last cell is left unused
 * so a full row never parks the terminal in the wrap-pending state. */
size_t size_content_width(Size size);

/* The window the buffer is drawn in for a given screen geometry: the
 * last screen row is reserved for the status line. */
Size size_viewport(Size screen);

#endif
