#ifndef OUTPUT_H
#define OUTPUT_H

#include <stddef.h>

#include "editor/editor.h"

/*
 * Minimal terminal output primitives. They return 0 on success and -1 when
 * writing to stdout fails.
 */

int screen_puts(const char *s);
int screen_write(const char *s, size_t n);
int screen_putc(char c);
int screen_flush(void);

int screen_carriage_return(void);
int screen_erase_below(void);
int screen_goto_column(size_t column);
int screen_move_left(size_t columns);
int screen_move_right(size_t columns);
int screen_move_up(size_t rows);
int screen_move_down(size_t rows);

/* Draw the cursor motion that editor_move_* reported. */
int screen_apply_motion(const EditorFile *ef, size_t cursor, CursorMotion motion);

/* Redraw from the start of a line and put the cursor back at cursor. */
int screen_redraw_from(const EditorFile *ef, size_t start, size_t cursor);

/* Repaint whatever editor_handle_key reported. */
int screen_apply_change(const Editor *ed, const EditorChange *change);

#endif
