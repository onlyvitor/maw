#include "editor.h"
#include "edit_ops.h"
#include "src/buffer/buffer.h"
#include <stdio.h>
#include <stdlib.h>

Editor *editor_open(const char *path)
{
    EditorFile *file = fileio_open(path);
    if (file == NULL)
    {
        return NULL;
    }

    Editor *ed = malloc(sizeof *ed);
    if (ed == NULL)
    {
        fprintf(stderr, "error: cannot allocate memory for the editor state\n");
        fileio_close(file);
        return NULL;
    }

    ed->file = file;
    ed->cursor = file->len;
    ed->dirty = false;
    return ed;
}

void editor_close(Editor *ed)
{
    if (ed == NULL)
    {
        return;
    }
    fileio_close(ed->file);
    free(ed);
}

CursorMotion editor_move_left(Editor *ed)
{
    if (ed->cursor == 0)
    {
        return CURSOR_MOTION_NONE;
    }

    if (ed->file->data[ed->cursor - 1] == '\n')
    {
        ed->cursor--;
        return CURSOR_MOTION_PREVIOUS_LINE;
    }

    ed->cursor--;
    return CURSOR_MOTION_LEFT;
}

CursorMotion editor_move_right(Editor *ed)
{
    if (ed->cursor == ed->file->len)
    {
        return CURSOR_MOTION_NONE;
    }

    if (ed->file->data[ed->cursor] == '\n')
    {
        ed->cursor++;
        return CURSOR_MOTION_NEXT_LINE;
    }

    ed->cursor++;
    return CURSOR_MOTION_RIGHT;
}

CursorMotion editor_move_up(Editor *ed)
{
    size_t start = buffer_line_start_index(ed->file->data, ed->cursor);

    if (start == 0)
    {
        return CURSOR_MOTION_NONE;
    }

    size_t prev_end = start - 1;
    size_t prev_start = buffer_line_start_index(ed->file->data, prev_end);
    size_t prev_len = prev_end - prev_start;
    size_t column = ed->cursor - start;
    size_t new_column = column < prev_len ? column : prev_len;

    ed->cursor = prev_start + new_column;
    return CURSOR_MOTION_UP;
}

CursorMotion editor_move_down(Editor *ed)
{
    size_t start = buffer_line_start_index(ed->file->data, ed->cursor);
    size_t end = buffer_line_end_index(ed->file->data, ed->file->len, start);

    if (end == ed->file->len)
    {
        return CURSOR_MOTION_NONE;
    }

    size_t next_start = end + 1;
    size_t next_end = buffer_line_end_index(ed->file->data, ed->file->len, next_start);
    size_t next_len = next_end - next_start;
    size_t column = ed->cursor - start;
    size_t new_column = column < next_len ? column : next_len;

    ed->cursor = next_start + new_column;
    return CURSOR_MOTION_DOWN;
}

static EditorAction handle_backspace(Editor *ed, EditorChange *change)
{
    if (ed->cursor == 0)
    {
        return ACTION_CONTINUE;
    }

    size_t pos = ed->cursor - 1;
    bool removed_newline = ed->file->data[pos] == '\n';
    size_t start = buffer_line_start_index(ed->file->data, pos);

    if (edit_remove_byte(ed->file, pos) != EDIT_OK)
    {
        return ACTION_ERROR;
    }

    ed->cursor = pos;
    ed->dirty = true;

    change->kind = CHANGE_LINE;
    change->start = start;
    change->removed_newline = removed_newline;
    return ACTION_CONTINUE;
}

static EditorAction handle_insert(Editor *ed, int key, EditorChange *change)
{
    char byte = (char)key;
    bool at_end = ed->cursor == ed->file->len;
    size_t start = buffer_line_start_index(ed->file->data, ed->cursor);

    if (edit_insert_byte(ed->file, ed->cursor, byte) != EDIT_OK)
    {
        return ACTION_ERROR;
    }

    ed->cursor++;
    ed->dirty = true;

    change->kind = at_end ? CHANGE_APPEND : CHANGE_LINE;
    change->start = start;
    change->removed_newline = false;
    change->byte = byte;
    return ACTION_CONTINUE;
}

EditorAction editor_handle_key(Editor *ed, int key, EditorChange *change)
{
    change->kind = CHANGE_NONE;

    if (key == KEY_CTRL_D || key == KEY_CTRL_Q || key == KEY_CTRL_1)
    {
        return ACTION_QUIT;
    }

    if (key == KEY_CTRL_S)
    {
        if (edit_save(ed->file) != EDIT_OK)
        {
            return ACTION_ERROR;
        }
        ed->dirty = false;
        return ACTION_CONTINUE;
    }

    CursorMotion motion = CURSOR_MOTION_NONE;

    switch (key)
    {
    case KEY_LEFT:
        motion = editor_move_left(ed);
        break;
    case KEY_RIGHT:
        motion = editor_move_right(ed);
        break;
    case KEY_UP:
        motion = editor_move_up(ed);
        break;
    case KEY_DOWN:
        motion = editor_move_down(ed);
        break;
    case KEY_BACKSPACE:
        return handle_backspace(ed, change);
    default:
        if (key >= 0 && key <= 0xFF)
        {
            return handle_insert(ed, key, change);
        }
        return ACTION_CONTINUE;
    }

    if (motion != CURSOR_MOTION_NONE)
    {
        change->kind = CHANGE_CURSOR;
        change->motion = motion;
    }
    return ACTION_CONTINUE;
}
