#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/buffer/buffer.h"
#include "src/editor/editor.h"
#include "src/editor/edit_ops.h"
#include "src/fileio/fileio.h"
#include "src/viewport/geometry.h"
#include "src/viewport/viewport.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) \
    do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define CHECK_SIZE(actual, expected) \
    do { \
        size_t a_ = (actual); \
        size_t e_ = (expected); \
        checks++; \
        if (a_ != e_) { \
            failures++; \
            printf("FAIL %s:%d: %s == %s (%zu != %zu)\n", __FILE__, __LINE__, \
                   #actual, #expected, a_, e_); \
        } \
    } while (0)

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL)
    {
        printf("FATAL: out of memory\n");
        exit(2);
    }
    return ptr;
}

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = xmalloc(len);
    memcpy(copy, s, len);
    return copy;
}

static Editor *make_editor(const char *text)
{
    Editor *ed = xmalloc(sizeof *ed);
    size_t len = strlen(text);
    EditorFile *ef = xmalloc(sizeof *ef);
    memset(ef, 0, sizeof *ef);
    ef->data = xmalloc(len + 1);
    memcpy(ef->data, text, len + 1);
    ef->len = len;
    ef->cap = len + 1;
    ef->path = xstrdup("unit-test.tmp");
    ef->mode = 0644;
    ef->exist = false;
    ed->file = ef;
    ed->cursor = len;
    ed->dirty = false;
    return ed;
}

static void dispose_editor(Editor *ed)
{
    free(ed->file->path);
    free(ed->file->data);
    free(ed->file);
    free(ed);
}



/* ------------------------------------------------------------------ */
/* buffer tests                                                        */
/* ------------------------------------------------------------------ */

static void test_line_start_index(void)
{
    const char *data = "abc\ndef\n";

    CHECK_SIZE(buffer_line_start_index(data, 0), 0);
    CHECK_SIZE(buffer_line_start_index(data, 2), 0);
    CHECK_SIZE(buffer_line_start_index(data, 3), 0);
    CHECK_SIZE(buffer_line_start_index(data, 4), 4);
    CHECK_SIZE(buffer_line_start_index(data, 7), 4);
    CHECK_SIZE(buffer_line_start_index(data, 8), 8);
    CHECK_SIZE(buffer_line_start_index("", 0), 0);
}

static void test_line_end_index(void)
{
    const char *data = "abc\ndef\n";
    const char *no_newline = "abc\ndef";

    CHECK_SIZE(buffer_line_end_index(data, 8, 0), 3);
    CHECK_SIZE(buffer_line_end_index(data, 8, 4), 7);
    CHECK_SIZE(buffer_line_end_index(data, 8, 8), 8);
    CHECK_SIZE(buffer_line_end_index(no_newline, 7, 0), 3);
    CHECK_SIZE(buffer_line_end_index(no_newline, 7, 4), 7);
    CHECK_SIZE(buffer_line_end_index("", 0, 0), 0);
}

static void test_row_column(void)
{
    const char *data = "abc\ndef\n";

    size_t row, column;

    buffer_row_column(data, 8, 0, &row, &column);
    CHECK_SIZE(row, 0);
    CHECK_SIZE(column, 0);

    buffer_row_column(data, 8, 6, &row, &column);
    CHECK_SIZE(row, 1);
    CHECK_SIZE(column, 2);

    buffer_row_column(data, 8, 8, &row, &column);
    CHECK_SIZE(row, 2);
    CHECK_SIZE(column, 0);

    row = 99;
    buffer_row_column(data, 8, 6, NULL, &column);
    CHECK_SIZE(column, 2);
    CHECK_SIZE(row, 99);

    column = 99;
    buffer_row_column(data, 8, 6, &row, NULL);
    CHECK_SIZE(row, 1);
    CHECK_SIZE(column, 99);
}

static void test_offset_of_row(void)
{
    const char *data = "abc\ndef\n";

    CHECK(buffer_offset_of_row(data, 8, 0) == data);
    CHECK(buffer_offset_of_row(data, 8, 1) == data + 4);
    CHECK(buffer_offset_of_row(data, 8, 2) == data + 8);
    CHECK(buffer_offset_of_row(data, 8, 3) == NULL);

    CHECK(buffer_offset_of_row("", 0, 0) != NULL);
    CHECK(buffer_offset_of_row("", 0, 1) == NULL);
    CHECK(buffer_offset_of_row("abc", 3, 1) == NULL);
}

/* ------------------------------------------------------------------ */
/* editor movement tests                                               */
/* ------------------------------------------------------------------ */

static void test_move_left(void)
{
    Editor *ed = make_editor("ab\ncd");

    ed->cursor = 0;
    CHECK(editor_move_left(ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed->cursor, 0);

    ed->cursor = 5;
    CHECK(editor_move_left(ed) == CURSOR_MOTION_LEFT);
    CHECK_SIZE(ed->cursor, 4);

    ed->cursor = 3;
    CHECK(editor_move_left(ed) == CURSOR_MOTION_PREVIOUS_LINE);
    CHECK_SIZE(ed->cursor, 2);

    dispose_editor(ed);
}

static void test_move_right(void)
{
    Editor *ed = make_editor("ab\ncd");

    ed->cursor = 5;
    CHECK(editor_move_right(ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed->cursor, 5);

    ed->cursor = 0;
    CHECK(editor_move_right(ed) == CURSOR_MOTION_RIGHT);
    CHECK_SIZE(ed->cursor, 1);

    ed->cursor = 2;
    CHECK(editor_move_right(ed) == CURSOR_MOTION_NEXT_LINE);
    CHECK_SIZE(ed->cursor, 3);

    dispose_editor(ed);
}

static void test_move_up(void)
{
    Editor *ed = make_editor("abcde\nxy\n");

    ed->cursor = 9;
    CHECK(editor_move_up(ed) == CURSOR_MOTION_UP);
    CHECK_SIZE(ed->cursor, 6);

    ed->cursor = 8;
    CHECK(editor_move_up(ed) == CURSOR_MOTION_UP);
    CHECK_SIZE(ed->cursor, 2);

    ed->cursor = 0;
    CHECK(editor_move_up(ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed->cursor, 0);

    dispose_editor(ed);
}

static void test_move_down(void)
{
    Editor *ed = make_editor("abcde\nxy\n");

    ed->cursor = 0;
    CHECK(editor_move_down(ed) == CURSOR_MOTION_DOWN);
    CHECK_SIZE(ed->cursor, 6);

    ed->cursor = 5;
    CHECK(editor_move_down(ed) == CURSOR_MOTION_DOWN);
    CHECK_SIZE(ed->cursor, 8);

    ed->cursor = 9;
    CHECK(editor_move_down(ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed->cursor, 9);

    dispose_editor(ed);
}

/* ------------------------------------------------------------------ */
/* editor key handling tests                                           */
/* ------------------------------------------------------------------ */

static void press(Editor *ed, int key, EditorAction *action, EditorChange *change)
{
    *action = editor_handle_key(ed, key, change);
}

static void test_handle_key_appends(void)
{
    Editor *ed = make_editor("abc");

    EditorAction action;
    EditorChange change;
    press(ed, 'X', &action, &change);

    CHECK(action == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_APPEND);
    CHECK(change.byte == 'X');
    CHECK(strcmp(ed->file->data, "abcX") == 0);
    CHECK_SIZE(ed->cursor, 4);
    CHECK(ed->dirty);

    dispose_editor(ed);
}

static void test_handle_key_inserts_in_middle(void)
{
    Editor *ed = make_editor("abc");
    ed->cursor = 1;

    EditorAction action;
    EditorChange change;
    press(ed, 'X', &action, &change);

    CHECK(action == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_LINE);
    CHECK(change.start == 0);
    CHECK(!change.removed_newline);
    CHECK(strcmp(ed->file->data, "aXbc") == 0);
    CHECK_SIZE(ed->cursor, 2);

    dispose_editor(ed);
}

static void test_handle_key_backspace(void)
{
    Editor *ed = make_editor("ab\ncd");
    EditorAction action;
    EditorChange change;

    ed->cursor = 0;
    press(ed, KEY_BACKSPACE, &action, &change);
    CHECK(action == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_NONE);
    CHECK(strcmp(ed->file->data, "ab\ncd") == 0);

    ed->cursor = 3;
    press(ed, KEY_BACKSPACE, &action, &change);
    CHECK(action == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_LINE);
    CHECK(change.start == 0);
    CHECK(change.removed_newline);
    CHECK(strcmp(ed->file->data, "abcd") == 0);
    CHECK_SIZE(ed->cursor, 2);
    CHECK(ed->dirty);

    dispose_editor(ed);
}

static void test_handle_key_moves(void)
{
    Editor *ed = make_editor("ab\ncd");
    ed->cursor = 3;

    EditorAction action;
    EditorChange change;
    press(ed, KEY_LEFT, &action, &change);

    CHECK(action == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_CURSOR);
    CHECK(change.motion == CURSOR_MOTION_PREVIOUS_LINE);
    CHECK_SIZE(ed->cursor, 2);
    CHECK(!ed->dirty);

    dispose_editor(ed);
}

static void test_handle_key_quit(void)
{
    Editor *ed = make_editor("");

    EditorAction action;
    EditorChange change;
    press(ed, KEY_CTRL_D, &action, &change);

    CHECK(action == ACTION_QUIT);
    CHECK(change.kind == CHANGE_NONE);
    CHECK_SIZE(ed->file->len, 0);

    dispose_editor(ed);
}

/* ------------------------------------------------------------------ */
/* fileio tests                                                        */
/* ------------------------------------------------------------------ */

static void test_insert_bytes(void)
{
    EditorFile *ef = fileio_from_data("test", "abc", 3);

    CHECK(edit_insert_byte(ef, 3, 'X') == EDIT_OK);
    CHECK(strcmp(ef->data, "abcX") == 0);
    CHECK_SIZE(ef->len, 4);

    CHECK(edit_insert_byte(ef, 1, 'Y') == EDIT_OK);
    CHECK(strcmp(ef->data, "aYbcX") == 0);
    CHECK_SIZE(ef->len, 5);

    fileio_close(ef);
}

static void test_insert_grows_buffer(void)
{
    EditorFile *ef = fileio_from_data("test", "", 0);

    for (int i = 0; i < 200; i++)
    {
        CHECK(edit_insert_byte(ef, i, 'x') == EDIT_OK);
    }
    CHECK_SIZE(ef->len, 200);
    CHECK(ef->cap >= ef->len);

    fileio_close(ef);
}

static void test_remove_byte_at(void)
{
    EditorFile *ef = fileio_from_data("test", "abcdef", 6);

    CHECK(edit_remove_byte(ef, 3) == EDIT_OK);
    CHECK(strcmp(ef->data, "abcef") == 0);
    CHECK_SIZE(ef->len, 5);

    CHECK(edit_remove_byte(ef, 0) == EDIT_OK);
    CHECK(strcmp(ef->data, "bcef") == 0);
    CHECK_SIZE(ef->len, 4);

    fileio_close(ef);
}

/* ------------------------------------------------------------------ */
/* viewport tests                                                      */
/* ------------------------------------------------------------------ */

static void test_viewport_starts_at_first_row(void)
{
    Viewport vp = viewport_new(size_new(24, 80));
    CHECK_SIZE(vp.top_row, 0);
    CHECK_SIZE(viewport_screen_row(&vp, 0), 0);
}

static void test_ensure_visible_inside_window(void)
{
    Viewport vp = viewport_new(size_new(10, 80));
    CHECK(!viewport_ensure_visible(&vp, 0));
    CHECK(!viewport_ensure_visible(&vp, 9));
    CHECK_SIZE(vp.top_row, 0);

    CHECK(viewport_ensure_visible(&vp, 20));
    CHECK(!viewport_ensure_visible(&vp, 11));
    CHECK(!viewport_ensure_visible(&vp, 20));
    CHECK_SIZE(vp.top_row, 11);
}

static void test_ensure_visible_scrolls_down(void)
{
    Viewport vp = viewport_new(size_new(10, 80));
    CHECK(viewport_ensure_visible(&vp, 10));
    CHECK_SIZE(vp.top_row, 1);
    CHECK_SIZE(viewport_screen_row(&vp, 10), 9);

    CHECK(viewport_ensure_visible(&vp, 30));
    CHECK_SIZE(vp.top_row, 21);
    CHECK_SIZE(viewport_screen_row(&vp, 30), 9);
}

static void test_ensure_visible_scrolls_up(void)
{
    Viewport vp = viewport_new(size_new(10, 80));
    CHECK(viewport_ensure_visible(&vp, 30));
    CHECK(viewport_ensure_visible(&vp, 5));
    CHECK_SIZE(vp.top_row, 5);
    CHECK_SIZE(viewport_screen_row(&vp, 5), 0);
    CHECK_SIZE(viewport_screen_row(&vp, 14), 9);
}

static void test_resize_only_reports_changes(void)
{
    Viewport vp = viewport_new(size_new(24, 80));
    CHECK(!viewport_resize(&vp, size_new(24, 80)));
    CHECK(viewport_resize(&vp, size_new(30, 100)));
    CHECK_SIZE(vp.size.rows, 30);
    CHECK_SIZE(vp.size.columns, 100);
    CHECK(!viewport_resize(&vp, size_new(30, 100)));
}

static void test_size_clamps_and_content_width(void)
{
    Size zero = size_new(0, 0);
    CHECK_SIZE(zero.rows, 1);
    CHECK_SIZE(zero.columns, 1);
    CHECK_SIZE(size_content_width(zero), 1);

    CHECK_SIZE(size_content_width(size_new(24, 80)), 79);
    CHECK_SIZE(size_content_width(size_new(24, 2)), 1);

    Size fallback = size_fallback();
    CHECK_SIZE(fallback.rows, 24);
    CHECK_SIZE(fallback.columns, 80);
}

static void test_size_viewport_reserves_status_line(void)
{
    Size window = size_viewport(size_fallback());
    CHECK_SIZE(window.rows, 23);
    CHECK_SIZE(window.columns, 80);

    Size minimal = size_viewport(size_new(1, 80));
    CHECK_SIZE(minimal.rows, 1);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    freopen("/dev/null", "w", stderr);

    test_line_start_index();
    test_line_end_index();
    test_row_column();
    test_offset_of_row();

    test_move_left();
    test_move_right();
    test_move_up();
    test_move_down();

    test_handle_key_appends();
    test_handle_key_inserts_in_middle();
    test_handle_key_backspace();
    test_handle_key_moves();
    test_handle_key_quit();

    test_insert_bytes();
    test_insert_grows_buffer();
    test_remove_byte_at();

    test_viewport_starts_at_first_row();
    test_ensure_visible_inside_window();
    test_ensure_visible_scrolls_down();
    test_ensure_visible_scrolls_up();
    test_resize_only_reports_changes();
    test_size_clamps_and_content_width();
    test_size_viewport_reserves_status_line();

    if (failures != 0)
    {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }

    printf("all %d unit checks passed\n", checks);
    return 0;
}
