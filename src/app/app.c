#include "app.h"

#include <stdio.h>

#include "../editor/editor.h"
#include "../input/input.h"
#include "../render/render.h"
#include "../terminal/terminal.h"
#include "../viewport/viewport.h"
#include "../viewport/geometry.h"
#include "../buffer/buffer.h"
#include "../editor/editor_types.h"

int app_run(const char *filename)
{
    Editor *ed = editor_open(filename);
    if (ed == NULL)
    {
        return 1;
    }

    if (terminal_rawmode_on() != 0)
    {
        editor_close(ed);
        return 1;
    }

    bool interactive = terminal_is_interactive();
    int status = 0;
    bool eof = false;
    Viewport vp = viewport_new(size_viewport(terminal_size()));

    if (interactive)
    {
        size_t cursor_row, column;
        buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &cursor_row, &column);
        viewport_ensure_visible(&vp, cursor_row);

        if (render_draw(ed, &vp) != 0)
        {
            fprintf(stderr, "error: cannot draw the file\n");
            status = 1;
        }
    }

    while (status == 0)
    {
        int key = input_scan();

        if (key == EOF)
        {
            if (ferror(stdin))
            {
                status = 1;
            }
            else
            {
                eof = true;
            }
            break;
        }

        EditorChange change;
        EditorAction action = editor_handle_key(ed, key, &change);

        if (action == ACTION_QUIT)
        {
            break;
        }
        if (action == ACTION_ERROR)
        {
            status = 1;
            break;
        }

        if (interactive)
        {
            Size new_size = terminal_size();
            bool resized = viewport_resize(&vp, size_viewport(new_size));
            if (resized)
            {
                size_t cursor_row, column;
                buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &cursor_row, &column);
                viewport_ensure_visible(&vp, cursor_row);
            }

            if (resized || render_apply_change(ed, &vp, &change) != 0)
            {
                if (resized)
                {
                    if (render_draw(ed, &vp) != 0)
                    {
                        fprintf(stderr, "error: cannot draw the file\n");
                        status = 1;
                        break;
                    }
                }
                else if (render_apply_change(ed, &vp, &change) != 0)
                {
                    fprintf(stderr, "error: cannot draw the change\n");
                    status = 1;
                    break;
                }
            }
        }
    }

    if (terminal_rawmode_off() != 0)
    {
        status = 1;
    }

    if (eof)
    {
        printf("\n[EOF]\n");
    }

    if (status == 0 && fileio_save(ed->file) != 0)
    {
        status = 1;
    }

    editor_close(ed);
    return status;
}
