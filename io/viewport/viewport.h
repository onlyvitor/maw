#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <stdbool.h>
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

/* The visible slice of the buffer. top_row is the first buffer row
 * drawn at the top of the screen. A logical line always occupies
 * exactly one screen row; the renderer clips it to the terminal
 * width. This module holds no I/O and no escape codes. */
typedef struct Viewport
{
    size_t top_row;
    Size size;
} Viewport;

Viewport viewport_new(Size size);

/* Adopt a new geometry. Returns true when the screen has to be
 * repainted from scratch. */
bool viewport_resize(Viewport *vp, Size size);

/* Scroll the least amount that puts cursor_row on screen. Returns
 * true when the viewport moved, so the screen has to be repainted. */
bool viewport_ensure_visible(Viewport *vp, size_t cursor_row);

/* Screen row (0-based) where a buffer row lands. Only meaningful for
 * rows inside the visible window. */
size_t viewport_screen_row(const Viewport *vp, size_t row);

#endif
