#ifndef EDITOR_TYPES_H
#define EDITOR_TYPES_H

#include <stdbool.h>
#include <stddef.h>

/* Key constants. Values above 0xff so they cannot collide with bytes
 * typed by the user. */
#define KEY_CTRL_H       0x08
#define KEY_CTRL_S       0x13
#define KEY_CTRL_D       0x04
#define KEY_CTRL_Q       0x11
#define KEY_BACKSPACE    0x7f
#define KEY_ESC          0x1b

#define KEY_UP           0x100
#define KEY_DOWN         0x101
#define KEY_LEFT         0x102
#define KEY_RIGHT        0x103

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
