#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "editor/editor.h"
#include "io/input/input.h"
#include "io/output/output.h"
#include "io/terminal/terminal.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    Editor *ed = editor_open(argv[1]);

    if (ed == NULL)
    {
        return 1;
    }

    if (rawmode_on() != 0)
    {
        editor_close(ed);
        return 1;
    }

    bool interactive = isatty(STDIN_FILENO) != 0;
    int status = 0;

    if (interactive && (screen_write(ed->file->data, ed->file->len) != 0 || screen_flush() != 0))
    {
        fprintf(stderr, "error: cannot draw the file\n");
        status = 1;
    }

    while (status == 0)
    {
        int key = scan();

        if (key == EOF)
        {
            if (ferror(stdin))
            {
                status = 1;
            }
            else
            {
                printf("\n[EOF]\n");
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

        if (interactive && screen_apply_change(ed, &change) != 0)
        {
            fprintf(stderr, "error: cannot draw the change\n");
            status = 1;
            break;
        }
    }

    if (rawmode_off() != 0)
    {
        status = 1;
    }

    if (status == 0 && save_file(ed->file) != 0)
    {
        status = 1;
    }

    editor_close(ed);

    return status;
}
