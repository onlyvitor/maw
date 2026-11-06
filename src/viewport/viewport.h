#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "geometry.h"

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

/* The geometry this viewport draws under. */
Size viewport_size(const Viewport *vp);

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
