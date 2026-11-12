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
#include "src/syntax/markdown.h"
#include "src/text/text.h"
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
/* text tests                                                          */
/* ------------------------------------------------------------------ */

static void test_utf8_lengths_and_cells(void)
{
    const char *data = "a" "\xC3\xA9" "\xE4\xB8\xAD" "\xF0\x9F\x98\x80";
    size_t len = strlen(data);

    CHECK_SIZE(text_char_length(data, len, 0), 1);
    CHECK_SIZE(text_char_length(data, len, 1), 2);
    CHECK_SIZE(text_char_length(data, len, 3), 3);
    CHECK_SIZE(text_char_length(data, len, 6), 4);

    CHECK_SIZE(text_char_cells(data, len, 0), 1);
    CHECK_SIZE(text_char_cells(data, len, 1), 1);
    CHECK_SIZE(text_char_cells(data, len, 3), 2);
    CHECK_SIZE(text_char_cells(data, len, 6), 2);

    CHECK_SIZE(text_char_length("\xC3", 1, 0), 1);
    CHECK_SIZE(text_char_length("\xC3\x28", 2, 0), 1);
}

static void test_text_width_and_offsets(void)
{
    CHECK_SIZE(text_width("abc", 0, 3), 3);
    CHECK_SIZE(text_width("\t", 0, 1), 8);
    CHECK_SIZE(text_width("a\t", 0, 2), 8);
    CHECK_SIZE(text_width("1234567\t", 0, 8), 8);
    CHECK_SIZE(text_width("12345678\t", 0, 9), 16);
    CHECK_SIZE(text_width("a\r", 0, 2), 1);
    CHECK_SIZE(text_width("\x01", 0, 1), 2);
    CHECK_SIZE(text_width("\xC3\xA9", 0, 2), 1);
    CHECK_SIZE(text_width("\xE4\xB8\xAD", 0, 3), 2);

    CHECK_SIZE(text_width_from("a\t", 0, 2, 0), 8);
    CHECK_SIZE(text_width_from("a\t", 0, 2, 3), 5);
    CHECK_SIZE(text_width_from("\t", 0, 1, 9), 7);
    CHECK_SIZE(text_width_from("ab", 0, 2, 4), 2);

    const char *data = "a" "\xC3\xA9" "z";
    CHECK_SIZE(text_next_offset(data, 4, 0), 1);
    CHECK_SIZE(text_next_offset(data, 4, 1), 3);
    CHECK_SIZE(text_prev_offset(data, 3), 1);
    CHECK_SIZE(text_prev_offset(data, 1), 0);

    CHECK_SIZE(text_offset_at_column("a\tb", 0, 3, 8), 2);
    CHECK_SIZE(text_offset_at_column("a\tb", 0, 3, 3), 1);
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

static void test_moves_over_utf8(void)
{
    Editor *ed = make_editor("a" "\xC3\xA9" "z");

    ed->cursor = 4;
    CHECK(editor_move_left(ed) == CURSOR_MOTION_LEFT);
    CHECK_SIZE(ed->cursor, 3);
    CHECK(editor_move_left(ed) == CURSOR_MOTION_LEFT);
    CHECK_SIZE(ed->cursor, 1);
    CHECK(editor_move_right(ed) == CURSOR_MOTION_RIGHT);
    CHECK_SIZE(ed->cursor, 3);

    dispose_editor(ed);
}

static void test_vertical_movement_uses_display_columns(void)
{
    Editor *ed = make_editor("\xE4\xB8\xAD" "\xE4\xB8\xAD" "\nab\n");

    ed->cursor = 6;
    CHECK(editor_move_down(ed) == CURSOR_MOTION_DOWN);
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

static void test_handle_key_backspace_removes_utf8_character(void)
{
    Editor *ed = make_editor("a" "\xC3\xA9" "z");
    ed->cursor = 3;

    EditorAction action;
    EditorChange change;
    press(ed, KEY_BACKSPACE, &action, &change);

    CHECK(action == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_LINE);
    CHECK(strcmp(ed->file->data, "az") == 0);
    CHECK_SIZE(ed->cursor, 1);

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

static void test_save_new_file_respects_umask(void)
{
    char path[] = "/tmp/maw-perm-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
    {
        return;
    }
    close(fd);
    unlink(path);

    mode_t old_mask = umask(0077);
    EditorFile *ef = fileio_from_data(path, "x", 1);
    CHECK(ef != NULL);

    if (ef != NULL)
    {
        CHECK(fileio_save(ef) == 0);

        struct stat st;
        CHECK(stat(path, &st) == 0);
        CHECK((st.st_mode & 0777) == 0600);
        fileio_close(ef);
    }

    umask(old_mask);
    unlink(path);
}

static void test_save_preserves_existing_mode(void)
{
    char path[] = "/tmp/maw-perm-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0)
    {
        return;
    }
    close(fd);
    CHECK(chmod(path, 0640) == 0);

    EditorFile *ef = fileio_open(path);
    CHECK(ef != NULL);

    if (ef != NULL)
    {
        CHECK(edit_insert_byte(ef, ef->len, 'x') == EDIT_OK);
        CHECK(fileio_save(ef) == 0);

        struct stat st;
        CHECK(stat(path, &st) == 0);
        CHECK((st.st_mode & 0777) == 0640);
        fileio_close(ef);
    }

    unlink(path);
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
/* markdown tests                                                      */
/* ------------------------------------------------------------------ */

static size_t scan_markdown(MarkdownState *state, const char *line,
                            MdSpan *spans)
{
    return markdown_scan_line(state, line, strlen(line), spans, MD_MAX_SPANS);
}

static void test_markdown_applies(void)
{
    CHECK(markdown_applies("notes.md"));
    CHECK(markdown_applies("/tmp/README.MD"));
    CHECK(markdown_applies("a.markdown"));
    CHECK(markdown_applies("a.MarkDown"));

    CHECK(!markdown_applies("notes.txt"));
    CHECK(!markdown_applies("md"));
    CHECK(!markdown_applies("notes.md.txt"));
    CHECK(!markdown_applies("notes.mdown"));
    CHECK(!markdown_applies(".mdx"));
    CHECK(!markdown_applies(NULL));
}

static void test_markdown_headings(void)
{
    MarkdownState state;
    MdSpan spans[MD_MAX_SPANS];
    size_t n;

    markdown_state_init(&state);

    const char *heading = "# Title";
    n = scan_markdown(&state, heading, spans);
    CHECK_SIZE(n, 1);
    CHECK_SIZE(spans[0].start, 0);
    CHECK_SIZE(spans[0].end, strlen(heading));
    CHECK(spans[0].style == MD_STYLE_HEADING);

    CHECK_SIZE(scan_markdown(&state, "   ### indented", spans), 1);
    CHECK_SIZE(scan_markdown(&state, "#", spans), 1);
    CHECK_SIZE(scan_markdown(&state, "#\tTab", spans), 1);
    CHECK_SIZE(scan_markdown(&state, "###### six", spans), 1);

    CHECK_SIZE(scan_markdown(&state, "#nospace", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "####### seven", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "    # four spaces", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "text # not", spans), 0);

    n = scan_markdown(&state, "####\r", spans);
    CHECK_SIZE(n, 1);
    CHECK_SIZE(spans[0].end, 4);
}

static void test_markdown_links(void)
{
    MarkdownState state;
    MdSpan spans[MD_MAX_SPANS];
    size_t n;

    markdown_state_init(&state);

    const char *line = "see [docs](https://x.y) and [b](c)";
    n = scan_markdown(&state, line, spans);
    CHECK_SIZE(n, 2);
    CHECK_SIZE(spans[0].start, strlen("see "));
    CHECK_SIZE(spans[0].end, strlen("see [docs](https://x.y)"));
    CHECK(spans[0].style == MD_STYLE_LINK);
    CHECK_SIZE(spans[1].start, strlen("see [docs](https://x.y) and "));
    CHECK_SIZE(spans[1].end, strlen(line));

    n = scan_markdown(&state, "[a](b(c)d)", spans);
    CHECK_SIZE(n, 1);
    CHECK_SIZE(spans[0].end, strlen("[a](b(c)d)"));

    n = scan_markdown(&state, "![alt](img.png)", spans);
    CHECK_SIZE(n, 1);
    CHECK_SIZE(spans[0].start, 0);
    CHECK_SIZE(spans[0].end, strlen("![alt](img.png)"));

    n = scan_markdown(&state, "# [a](b)", spans);
    CHECK_SIZE(n, 1);
    CHECK(spans[0].style == MD_STYLE_HEADING);

    CHECK_SIZE(scan_markdown(&state, "\\[a](b)", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "[a](b", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "[a] (b)", spans), 0);

    CHECK_SIZE(scan_markdown(&state, "`[a](b)`", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "``a `[b](c)` d``", spans), 0);

    const char *many = "[a](1) [b](2) [c](3)";
    CHECK_SIZE(markdown_scan_line(&state, many, strlen(many), spans, 2), 2);
}

static void test_markdown_fences(void)
{
    MarkdownState state;
    MdSpan spans[MD_MAX_SPANS];

    markdown_state_init(&state);
    CHECK_SIZE(scan_markdown(&state, "```", spans), 0);
    CHECK(state.in_fence);
    CHECK(state.fence_char == '`');
    CHECK_SIZE(state.fence_len, 3);

    CHECK_SIZE(scan_markdown(&state, "# hidden", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "[a](b)", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "``` trailing", spans), 0);
    CHECK(state.in_fence);

    CHECK_SIZE(scan_markdown(&state, "````", spans), 0);
    CHECK(!state.in_fence);
    CHECK_SIZE(scan_markdown(&state, "# visible", spans), 1);

    markdown_state_init(&state);
    CHECK_SIZE(scan_markdown(&state, "````", spans), 0);
    CHECK_SIZE(scan_markdown(&state, "```", spans), 0);
    CHECK(state.in_fence);
    CHECK_SIZE(scan_markdown(&state, "~~~~", spans), 0);
    CHECK(state.in_fence);
    CHECK_SIZE(scan_markdown(&state, "`````", spans), 0);
    CHECK(!state.in_fence);

    markdown_state_init(&state);
    CHECK_SIZE(scan_markdown(&state, "~~~ python", spans), 0);
    CHECK(state.in_fence);
    CHECK(state.fence_char == '~');
    CHECK_SIZE(scan_markdown(&state, "~~~", spans), 0);
    CHECK(!state.in_fence);

    markdown_state_init(&state);
    CHECK_SIZE(scan_markdown(&state, "    ```", spans), 0);
    CHECK(!state.in_fence);
}

static void test_markdown_state_at(void)
{
    const char *data = "```\n# hidden\n```\n# visible\n";
    size_t len = strlen(data);
    size_t visible = strlen("```\n# hidden\n```\n");
    MarkdownState state;
    MdSpan spans[MD_MAX_SPANS];

    markdown_state_at(data, len, 0, &state);
    CHECK(!state.in_fence);

    markdown_state_at(data, len, strlen("```\n"), &state);
    CHECK(state.in_fence);

    markdown_state_at(data, len, visible, &state);
    CHECK(!state.in_fence);
    CHECK_SIZE(markdown_scan_line(&state, data + visible, len - visible,
                                  spans, MD_MAX_SPANS), 1);
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

    test_utf8_lengths_and_cells();
    test_text_width_and_offsets();

    test_move_left();
    test_move_right();
    test_move_up();
    test_move_down();
    test_moves_over_utf8();
    test_vertical_movement_uses_display_columns();

    test_handle_key_appends();
    test_handle_key_inserts_in_middle();
    test_handle_key_backspace();
    test_handle_key_backspace_removes_utf8_character();
    test_handle_key_moves();
    test_handle_key_quit();

    test_insert_bytes();
    test_insert_grows_buffer();
    test_save_new_file_respects_umask();
    test_save_preserves_existing_mode();
    test_remove_byte_at();

    test_viewport_starts_at_first_row();
    test_ensure_visible_inside_window();
    test_ensure_visible_scrolls_down();
    test_ensure_visible_scrolls_up();
    test_resize_only_reports_changes();
    test_size_clamps_and_content_width();
    test_size_viewport_reserves_status_line();

    test_markdown_applies();
    test_markdown_headings();
    test_markdown_links();
    test_markdown_fences();
    test_markdown_state_at();

    if (failures != 0)
    {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }

    printf("all %d unit checks passed\n", checks);
    return 0;
}
