#include "editor.h"

#include <stdio.h>
#include <stdlib.h>

Editor *editor_open(const char *path)
{
    EditorFile *file = open_file(path);

    if (file == NULL)
    {
        return NULL;
    }

    Editor *ed = malloc(sizeof *ed);

    if (ed == NULL)
    {
        fprintf(stderr, "error: cannot allocate memory for the editor state\n");
        close_file(file);
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

    close_file(ed->file);
    free(ed);
}

size_t line_start_index(const char *data, size_t pos)
{
    size_t start = pos;

    while (start > 0 && data[start - 1] != '\n')
    {
        start--;
    }

    return start;
}

size_t line_end_index(const char *data, size_t len, size_t pos)
{
    size_t end = pos;

    while (end < len && data[end] != '\n')
    {
        end++;
    }

    return end;
}

void editor_row_column(const EditorFile *ef, size_t pos, size_t *row, size_t *column)
{
    if (row != NULL)
    {
        size_t r = 0;

        for (size_t i = 0; i < pos; i++)
        {
            if (ef->data[i] == '\n')
            {
                r++;
            }
        }

        *row = r;
    }

    if (column != NULL)
    {
        *column = pos - line_start_index(ef->data, pos);
    }
}

CursorMotion editor_move_left(Editor *ed)
{
    if (ed->cursor == 0)
    {
        return CURSOR_MOTION_NONE;
    }

    if (ed->file->data[ed->cursor - 1] == '\n')
    {
        /* Jump to the end of the previous line. */
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
        /* Jump to the beginning of the next line. */
        ed->cursor++;
        return CURSOR_MOTION_NEXT_LINE;
    }

    ed->cursor++;
    return CURSOR_MOTION_RIGHT;
}

CursorMotion editor_move_up(Editor *ed)
{
    size_t start = line_start_index(ed->file->data, ed->cursor);

    if (start == 0)
    {
        return CURSOR_MOTION_NONE;
    }

    /* The newline that ends the previous line. */
    size_t prev_end = start - 1;
    size_t prev_start = line_start_index(ed->file->data, prev_end);
    size_t prev_len = prev_end - prev_start;
    size_t column = ed->cursor - start;
    size_t new_column = column < prev_len ? column : prev_len;

    ed->cursor = prev_start + new_column;
    return CURSOR_MOTION_UP;
}

CursorMotion editor_move_down(Editor *ed)
{
    size_t start = line_start_index(ed->file->data, ed->cursor);
    size_t end = line_end_index(ed->file->data, ed->file->len, start);

    if (end == ed->file->len)
    {
        /* There is no line below the last one. */
        return CURSOR_MOTION_NONE;
    }

    size_t next_start = end + 1;
    size_t next_end = line_end_index(ed->file->data, ed->file->len, next_start);
    size_t next_len = next_end - next_start;
    size_t column = ed->cursor - start;
    size_t new_column = column < next_len ? column : next_len;

    ed->cursor = next_start + new_column;
    return CURSOR_MOTION_DOWN;
}

EditorAction editor_handle_key(Editor *ed, int key, EditorChange *change)
{
    change->kind = CHANGE_NONE;

    if (key == CTRL_D)
    {
        return ACTION_QUIT;
    }

    if (key == CTRL_S)
    {
        if (save_file(ed->file) != 0)
        {
            return ACTION_ERROR;
        }

        ed->dirty = false;
        return ACTION_CONTINUE;
    }

    if (key == LEFT_ARROW || key == RIGHT_ARROW || key == UP_ARROW || key == DOWN_ARROW)
    {
        CursorMotion motion;

        if (key == LEFT_ARROW)
        {
            motion = editor_move_left(ed);
        }
        else if (key == RIGHT_ARROW)
        {
            motion = editor_move_right(ed);
        }
        else if (key == UP_ARROW)
        {
            motion = editor_move_up(ed);
        }
        else
        {
            motion = editor_move_down(ed);
        }

        change->kind = CHANGE_CURSOR;
        change->motion = motion;
        return ACTION_CONTINUE;
    }

    if (key == KEY_BACKSPACE)
    {
        if (ed->cursor == 0)
        {
            return ACTION_CONTINUE;
        }

        size_t pos = ed->cursor - 1;
        bool removed_newline = ed->file->data[pos] == '\n';
        size_t start = line_start_index(ed->file->data, pos);

        if (remove_byte_at(ed->file, pos) != 0)
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

    char byte = (char)key;
    bool at_end = ed->cursor == ed->file->len;
    size_t start = line_start_index(ed->file->data, ed->cursor);

    if (insert_bytes(ed->file, ed->cursor, &byte, 1) != 0)
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
