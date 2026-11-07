#define _POSIX_C_SOURCE 200809L

#include "app.h"

#include <signal.h>
#include <stdio.h>

#include "../editor/editor.h"
#include "../input/input.h"
#include "../render/render.h"
#include "../terminal/terminal.h"
#include "../terminal/signals.h"
#include "../viewport/viewport.h"
#include "../viewport/geometry.h"
#include "../buffer/buffer.h"
#include "../editor/editor_types.h"

static void ensure_cursor_visible(Editor *ed, Viewport *vp)
{
    size_t row;
    buffer_row_column(ed->file->data, ed->file->len, ed->cursor, &row, NULL);
    viewport_ensure_visible(vp, row);
}

static int redraw_screen(Editor *ed, Viewport *vp)
{
    viewport_resize(vp, size_viewport(terminal_size()));
    ensure_cursor_visible(ed, vp);
    return render_draw(ed, vp);
}

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

    if (interactive && redraw_screen(ed, &vp) != 0)
    {
        fprintf(stderr, "error: cannot draw the file\n");
        status = 1;
    }

    while (status == 0)
    {
        if (interactive && signals_take_resize())
        {
            if (redraw_screen(ed, &vp) != 0)
            {
                fprintf(stderr, "error: cannot draw the file\n");
                status = 1;
                break;
            }
            continue;
        }

        int key = input_scan();

        if (key == INPUT_EOF)
        {
            eof = true;
            break;
        }
        if (key == INPUT_ERROR)
        {
            status = 1;
            break;
        }
        if (key == KEY_RESIZE)
        {
            if (interactive && redraw_screen(ed, &vp) != 0)
            {
                fprintf(stderr, "error: cannot draw the file\n");
                status = 1;
                break;
            }
            continue;
        }

        EditorChange change;
        EditorAction action = editor_handle_key(ed, key, &change);

        if (action == ACTION_QUIT)
        {
            break;
        }
        if (action == ACTION_SUSPEND)
        {
            if (interactive)
            {
                raise(SIGTSTP);
            }
            continue;
        }
        if (action == ACTION_ERROR)
        {
            status = 1;
            break;
        }

        if (!interactive)
        {
            continue;
        }

        bool resized = viewport_resize(&vp, size_viewport(terminal_size()));
        if (resized)
        {
            ensure_cursor_visible(ed, &vp);
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

    if (terminal_rawmode_off() != 0)
    {
        status = 1;
    }

    if (eof)
    {
        printf("\n[EOF]\n");
    }

    if (status == 0 && ed->dirty)
    {
        if (fileio_save(ed->file) != 0)
        {
            fprintf(stderr, "error: cannot save the file\n");
            status = 1;
        }
    }

    editor_close(ed);
    return status;
}
