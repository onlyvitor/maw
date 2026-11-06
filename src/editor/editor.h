#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <stddef.h>

#include "src/fileio/fileio.h"
#include "editor_types.h"

/* The editing state that lives above the file. */
typedef struct Editor
{
    EditorFile *file;
    size_t cursor;
    bool dirty;
} Editor;

/* Create an editor for the given file path. Returns NULL on failure. */
Editor *editor_open(const char *path);

/* Free the editor and its file. */
void editor_close(Editor *ed);

/* Cursor motion functions. Return the motion performed. */
CursorMotion editor_move_left(Editor *ed);
CursorMotion editor_move_right(Editor *ed);
CursorMotion editor_move_up(Editor *ed);
CursorMotion editor_move_down(Editor *ed);

/* Handle a single key press. Returns the action to take. */
EditorAction editor_handle_key(Editor *ed, int key, EditorChange *change);

#endif
