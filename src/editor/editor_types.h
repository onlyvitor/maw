#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "../key.h"

/* What a movement did, so the caller can render it without knowing
 * the buffer layout. NONE means the cursor did not move. */
typedef enum CursorMotion
{
    CURSOR_MOTION_NONE,
    CURSOR_MOTION_LEFT,
    CURSOR_MOTION_RIGHT,
    CURSOR_MOTION_PREVIOUS_LINE,
    CURSOR_MOTION_NEXT_LINE,
    CURSOR_MOTION_UP,
    CURSOR_MOTION_DOWN,
} CursorMotion;

/* What the caller has to do after a key was handled. */
typedef enum EditorAction
{
    ACTION_CONTINUE,
    ACTION_QUIT,
    ACTION_SUSPEND,
    ACTION_ERROR,
} EditorAction;

/* The part of the buffer that has to be repainted after a change. */
typedef enum ChangeKind
{
    CHANGE_NONE,
    CHANGE_CURSOR,
    CHANGE_LINE,
    CHANGE_APPEND,
} ChangeKind;

typedef struct EditorChange
{
    ChangeKind kind;
    CursorMotion motion;        /* CHANGE_CURSOR */
    size_t start;               /* CHANGE_LINE: first byte of the line */
    bool removed_newline;       /* CHANGE_LINE: the cursor moved up a row */
    char byte;                  /* CHANGE_APPEND */
} EditorChange;

#endif
