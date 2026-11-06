#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>

#include "editor/editor.h"
#include "io/viewport/viewport.h"

/*
 * Screen output for a viewport: the editor reports what changed, this
 * module owns the escape codes and the screen geometry. Only the
 * visible rows of the buffer are ever written, and the last screen
 * row hosts the status line. The functions return 0 on success and
 * -1 when writing to stdout fails.
 */

int screen_puts(const char *s);
int screen_write(const char *s, size_t n);
int screen_putc(char c);
int screen_flush(void);

/* Move to the top-left cell. */
int screen_goto_origin(void);

/* Put the cursor at a 0-based screen row and column. */
int screen_goto_position(size_t row, size_t column);

/* Erase the current line from the cursor to its end. */
int screen_erase_line(void);

/* Erase from the cursor to the bottom of the screen. */
int screen_erase_below(void);

/* Repaint every visible row and the status line, then put the cursor
 * back on screen. */
int screen_draw(const Editor *ed, const Viewport *vp);

/* Repaint whatever editor_handle_key reported. */
int screen_apply_change(const Editor *ed, Viewport *vp, Size size,
                        const EditorChange *change);

#endif
