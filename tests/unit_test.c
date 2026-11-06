/* Feature-test macro: must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "editor/editor.h"
#include "fileio/fileio.h"

static int checks;
static int failures;

#define CHECK(cond)                                                           \
    do                                                                        \
    {                                                                         \
        checks++;                                                             \
        if (!(cond))                                                          \
        {                                                                     \
            failures++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

#define CHECK_SIZE(actual, expected)                                          \
    do                                                                        \
    {                                                                         \
        size_t a_ = (actual);                                                 \
        size_t e_ = (expected);                                               \
        checks++;                                                             \
        if (a_ != e_)                                                         \
        {                                                                     \
            failures++;                                                       \
            printf("FAIL %s:%d: %s == %s (%zu != %zu)\n", __FILE__, __LINE__, \
                   #actual, #expected, a_, e_);                               \
        }                                                                     \
    } while (0)

#define CHECK_STR(actual, expected)                                           \
    do                                                                        \
    {                                                                         \
        const char *a_ = (actual);                                            \
        const char *e_ = (expected);                                          \
        checks++;                                                             \
        if (strcmp(a_, e_) != 0)                                              \
        {                                                                     \
            failures++;                                                       \
            printf("FAIL %s:%d: %s == %s (\"%s\" != \"%s\")\n", __FILE__,     \
                   __LINE__, #actual, #expected, a_, e_);                     \
        }                                                                     \
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

/* Editor backed by a private, growable copy of text. */
static Editor make_editor(const char *text)
{
    Editor ed = {0};
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

    ed.file = ef;
    ed.cursor = len;
    ed.dirty = false;

    return ed;
}

static void dispose_editor(Editor *ed)
{
    free(ed->file->path);
    free(ed->file->data);
    free(ed->file);
}

static char *make_temp_dir(void)
{
    char template[] = "/tmp/oldvim-unit-XXXXXX";
    char *dir = mkdtemp(template);

    if (dir == NULL)
    {
        printf("FATAL: cannot create a temporary directory\n");
        exit(2);
    }

    return xstrdup(dir);
}

static char *join_path(const char *dir, const char *name)
{
    size_t size = strlen(dir) + strlen(name) + 2;
    char *path = xmalloc(size);

    snprintf(path, size, "%s/%s", dir, name);

    return path;
}

static void write_file(const char *path, const char *data, mode_t mode)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL)
    {
        printf("FATAL: cannot write %s\n", path);
        exit(2);
    }

    if (fwrite(data, 1, strlen(data), fp) != strlen(data) || fclose(fp) != 0)
    {
        printf("FATAL: cannot write %s\n", path);
        exit(2);
    }

    if (chmod(path, mode) != 0)
    {
        printf("FATAL: cannot chmod %s\n", path);
        exit(2);
    }
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    long size;
    char *data;

    if (fp == NULL)
    {
        printf("FATAL: cannot read %s\n", path);
        exit(2);
    }

    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
        fseek(fp, 0, SEEK_SET) != 0)
    {
        printf("FATAL: cannot size %s\n", path);
        exit(2);
    }

    data = xmalloc((size_t)size + 1);

    if (fread(data, 1, (size_t)size, fp) != (size_t)size || fclose(fp) != 0)
    {
        printf("FATAL: cannot read %s\n", path);
        exit(2);
    }

    data[size] = '\0';
    *out_len = (size_t)size;

    return data;
}

/* ------------------------------------------------------------------ */
/* line helpers                                                        */
/* ------------------------------------------------------------------ */

static void test_line_start_index(void)
{
    const char *data = "abc\ndef\n";

    CHECK_SIZE(line_start_index(data, 0), 0);
    CHECK_SIZE(line_start_index(data, 2), 0);
    CHECK_SIZE(line_start_index(data, 3), 0); /* on the newline */
    CHECK_SIZE(line_start_index(data, 4), 4); /* start of the second line */
    CHECK_SIZE(line_start_index(data, 7), 4);
    CHECK_SIZE(line_start_index(data, 8), 8); /* EOF after a newline */
    CHECK_SIZE(line_start_index("", 0), 0);
}

static void test_line_end_index(void)
{
    const char *data = "abc\ndef\n";
    const char *no_newline = "abc\ndef";

    CHECK_SIZE(line_end_index(data, 8, 0), 3);
    CHECK_SIZE(line_end_index(data, 8, 4), 7);
    CHECK_SIZE(line_end_index(data, 8, 8), 8);
    CHECK_SIZE(line_end_index(no_newline, 7, 0), 3);
    CHECK_SIZE(line_end_index(no_newline, 7, 4), 7);
    CHECK_SIZE(line_end_index("", 0, 0), 0);
}

static void test_editor_row_column(void)
{
    EditorFile ef = {0};
    size_t row = 99;
    size_t column = 99;

    ef.data = (char *)"abc\ndef\n";
    ef.len = 8;

    editor_row_column(&ef, 0, &row, &column);
    CHECK_SIZE(row, 0);
    CHECK_SIZE(column, 0);

    editor_row_column(&ef, 6, &row, &column);
    CHECK_SIZE(row, 1);
    CHECK_SIZE(column, 2);

    editor_row_column(&ef, 8, &row, &column);
    CHECK_SIZE(row, 2);
    CHECK_SIZE(column, 0);

    /* Each output is optional. */
    row = 99;
    editor_row_column(&ef, 6, NULL, &column);
    CHECK_SIZE(column, 2);
    CHECK_SIZE(row, 99);

    column = 99;
    editor_row_column(&ef, 6, &row, NULL);
    CHECK_SIZE(row, 1);
    CHECK_SIZE(column, 99);
}

/* ------------------------------------------------------------------ */
/* movement                                                            */
/* ------------------------------------------------------------------ */

static void test_move_left(void)
{
    Editor ed = make_editor("ab\ncd");

    ed.cursor = 0;
    CHECK(editor_move_left(&ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed.cursor, 0);

    ed.cursor = 5;
    CHECK(editor_move_left(&ed) == CURSOR_MOTION_LEFT);
    CHECK_SIZE(ed.cursor, 4);

    ed.cursor = 3; /* start of the second line */
    CHECK(editor_move_left(&ed) == CURSOR_MOTION_PREVIOUS_LINE);
    CHECK_SIZE(ed.cursor, 2); /* end of the first line */

    dispose_editor(&ed);
}

static void test_move_right(void)
{
    Editor ed = make_editor("ab\ncd");

    ed.cursor = 5;
    CHECK(editor_move_right(&ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed.cursor, 5);

    ed.cursor = 0;
    CHECK(editor_move_right(&ed) == CURSOR_MOTION_RIGHT);
    CHECK_SIZE(ed.cursor, 1);

    ed.cursor = 2; /* on the newline */
    CHECK(editor_move_right(&ed) == CURSOR_MOTION_NEXT_LINE);
    CHECK_SIZE(ed.cursor, 3);

    dispose_editor(&ed);
}

static void test_move_up(void)
{
    Editor ed = make_editor("abcde\nxy\n");

    ed.cursor = 9; /* empty last line */
    CHECK(editor_move_up(&ed) == CURSOR_MOTION_UP);
    CHECK_SIZE(ed.cursor, 6); /* start of "xy" */

    ed.cursor = 8; /* end of "xy" */
    CHECK(editor_move_up(&ed) == CURSOR_MOTION_UP);
    CHECK_SIZE(ed.cursor, 2); /* clamped to length of "abcde" */

    ed.cursor = 0;
    CHECK(editor_move_up(&ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed.cursor, 0);

    dispose_editor(&ed);
}

static void test_move_down(void)
{
    Editor ed = make_editor("abcde\nxy\n");

    ed.cursor = 0;
    CHECK(editor_move_down(&ed) == CURSOR_MOTION_DOWN);
    CHECK_SIZE(ed.cursor, 6);

    ed.cursor = 5; /* end of "abcde" */
    CHECK(editor_move_down(&ed) == CURSOR_MOTION_DOWN);
    CHECK_SIZE(ed.cursor, 8); /* clamped to "xy" */

    ed.cursor = 9; /* empty last line, nothing below */
    CHECK(editor_move_down(&ed) == CURSOR_MOTION_NONE);
    CHECK_SIZE(ed.cursor, 9);

    dispose_editor(&ed);
}

/* ------------------------------------------------------------------ */
/* buffer primitives                                                   */
/* ------------------------------------------------------------------ */

static void test_insert_bytes(void)
{
    Editor ed = make_editor("");

    CHECK(insert_bytes(ed.file, 0, "hello", 5) == 0);
    CHECK_SIZE(ed.file->len, 5);
    CHECK_STR(ed.file->data, "hello");
    CHECK_SIZE(ed.file->data[ed.file->len], '\0');

    /* Insert in the middle. */
    CHECK(insert_bytes(ed.file, 2, "XY", 2) == 0);
    CHECK_STR(ed.file->data, "heXYllo");

    /* Insert at the end. */
    CHECK(insert_bytes(ed.file, ed.file->len, "!", 1) == 0);
    CHECK_STR(ed.file->data, "heXYllo!");

    /* n == 0 keeps the buffer untouched. */
    CHECK(insert_bytes(ed.file, 0, "ignored", 0) == 0);
    CHECK_STR(ed.file->data, "heXYllo!");

    /* Out of range and null buffer. */
    CHECK(insert_bytes(ed.file, ed.file->len + 1, "x", 1) == -1);
    CHECK(insert_bytes(NULL, 0, "x", 1) == -1);

    dispose_editor(&ed);
}

static void test_insert_grows_the_buffer(void)
{
    Editor ed = make_editor("");
    char *chunk = xmalloc(1000);

    memset(chunk, 'x', 1000);

    CHECK_SIZE(ed.file->cap, 1);
    CHECK(insert_bytes(ed.file, 0, chunk, 1000) == 0);
    CHECK(ed.file->cap >= 1001);
    CHECK_SIZE(ed.file->len, 1000);
    CHECK_SIZE(ed.file->data[1000], '\0');
    CHECK(ed.file->data[0] == 'x');
    CHECK(ed.file->data[999] == 'x');

    free(chunk);
    dispose_editor(&ed);
}

static void test_remove_byte_at(void)
{
    Editor ed = make_editor("abc");

    CHECK(remove_byte_at(ed.file, 1) == 0);
    CHECK_STR(ed.file->data, "ac");
    CHECK_SIZE(ed.file->len, 2);

    CHECK(remove_byte_at(ed.file, 1) == 0);
    CHECK_STR(ed.file->data, "a");

    CHECK(remove_byte_at(ed.file, 0) == 0);
    CHECK_STR(ed.file->data, "");
    CHECK_SIZE(ed.file->len, 0);

    CHECK(remove_byte_at(ed.file, 0) == -1);
    CHECK(remove_byte_at(NULL, 0) == -1);

    dispose_editor(&ed);
}

/* ------------------------------------------------------------------ */
/* key handling                                                        */
/* ------------------------------------------------------------------ */

static void test_handle_key_appends(void)
{
    Editor ed = make_editor("abc");
    EditorChange change;

    CHECK(editor_handle_key(&ed, 'X', &change) == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_APPEND);
    CHECK(change.byte == 'X');
    CHECK_STR(ed.file->data, "abcX");
    CHECK_SIZE(ed.cursor, 4);
    CHECK(ed.dirty);

    dispose_editor(&ed);
}

static void test_handle_key_inserts_in_the_middle(void)
{
    Editor ed = make_editor("abc");

    ed.cursor = 1;

    EditorChange change;
    CHECK(editor_handle_key(&ed, 'X', &change) == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_LINE);
    CHECK_SIZE(change.start, 0);
    CHECK_STR(ed.file->data, "aXbc");
    CHECK_SIZE(ed.cursor, 2);

    dispose_editor(&ed);
}

static void test_handle_key_backspace(void)
{
    Editor ed = make_editor("ab\ncd");
    EditorChange change;

    ed.cursor = 0;
    CHECK(editor_handle_key(&ed, KEY_BACKSPACE, &change) == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_NONE);
    CHECK_STR(ed.file->data, "ab\ncd");

    /* Joining the two lines. */
    ed.cursor = 3;
    CHECK(editor_handle_key(&ed, KEY_BACKSPACE, &change) == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_LINE);
    CHECK(change.removed_newline);
    CHECK_SIZE(change.start, 0);
    CHECK_SIZE(ed.cursor, 2);
    CHECK_STR(ed.file->data, "abcd");

    dispose_editor(&ed);
}

static void test_handle_key_moves(void)
{
    Editor ed = make_editor("ab\ncd");
    EditorChange change;

    ed.cursor = 3;
    CHECK(editor_handle_key(&ed, LEFT_ARROW, &change) == ACTION_CONTINUE);
    CHECK(change.kind == CHANGE_CURSOR);
    CHECK(change.motion == CURSOR_MOTION_PREVIOUS_LINE);
    CHECK_SIZE(ed.cursor, 2);

    dispose_editor(&ed);
}

static void test_handle_key_quit(void)
{
    Editor ed = make_editor("");
    EditorChange change;

    CHECK(editor_handle_key(&ed, CTRL_D, &change) == ACTION_QUIT);
    CHECK(change.kind == CHANGE_NONE);

    dispose_editor(&ed);
}

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */

static void test_open_and_save(void)
{
    char *dir = make_temp_dir();
    char *path = join_path(dir, "file.txt");
    size_t len;
    char *data;

    write_file(path, "hello\n", 0640);

    Editor *ed = editor_open(path);
    CHECK(ed != NULL);
    CHECK_SIZE(ed->file->len, 6);
    CHECK_SIZE(ed->cursor, 6);
    CHECK_SIZE(ed->file->mode, 0640);
    CHECK(ed->file->exist);

    data = read_file(path, &len);
    CHECK_SIZE(len, 6);
    CHECK(strcmp(data, "hello\n") == 0);
    free(data);

    CHECK(editor_handle_key(ed, '!', &(EditorChange){0}) == ACTION_CONTINUE);
    CHECK(save_file(ed->file) == 0);

    data = read_file(path, &len);
    CHECK_SIZE(len, 7);
    CHECK(strcmp(data, "hello\n!") == 0);
    free(data);

    {
        struct stat st;

        CHECK(stat(path, &st) == 0);
        CHECK_SIZE(st.st_mode & 07777, 0640);
    }

    editor_close(ed);
    unlink(path);
    rmdir(dir);
    free(path);
    free(dir);
}

static void test_create_new_file(void)
{
    char *dir = make_temp_dir();
    char *path = join_path(dir, "new.txt");
    size_t len;
    char *data;

    Editor *ed = editor_open(path);
    CHECK(ed != NULL);
    CHECK_SIZE(ed->file->len, 0);
    CHECK(!ed->file->exist);

    CHECK(editor_handle_key(ed, 'h', &(EditorChange){0}) == ACTION_CONTINUE);
    CHECK(editor_handle_key(ed, 'i', &(EditorChange){0}) == ACTION_CONTINUE);
    CHECK(save_file(ed->file) == 0);
    CHECK(ed->file->exist);

    data = read_file(path, &len);
    CHECK_SIZE(len, 2);
    CHECK(strcmp(data, "hi") == 0);
    free(data);

    {
        struct stat st;

        CHECK(stat(path, &st) == 0);
        CHECK_SIZE(st.st_mode & 07777, 0644);
    }

    editor_close(ed);
    unlink(path);
    rmdir(dir);
    free(path);
    free(dir);
}

static void test_save_after_removal(void)
{
    char *dir = make_temp_dir();
    char *path = join_path(dir, "remove.txt");
    size_t len;
    char *data;

    write_file(path, "abc\ndef\n", 0644);

    Editor *ed = editor_open(path);
    CHECK(ed != NULL);

    ed->cursor = 4; /* start of the second line */
    CHECK(editor_handle_key(ed, KEY_BACKSPACE, &(EditorChange){0}) == ACTION_CONTINUE);
    CHECK(save_file(ed->file) == 0);

    data = read_file(path, &len);
    CHECK_SIZE(len, 7);
    CHECK(strcmp(data, "abcdef\n") == 0);
    free(data);

    editor_close(ed);
    unlink(path);
    rmdir(dir);
    free(path);
    free(dir);
}

int main(void)
{
    /* The negative-path checks expect error messages; keep the log clean. */
    freopen("/dev/null", "w", stderr);

    test_line_start_index();
    test_line_end_index();
    test_editor_row_column();

    test_move_left();
    test_move_right();
    test_move_up();
    test_move_down();

    test_insert_bytes();
    test_insert_grows_the_buffer();
    test_remove_byte_at();

    test_handle_key_appends();
    test_handle_key_inserts_in_the_middle();
    test_handle_key_backspace();
    test_handle_key_moves();
    test_handle_key_quit();

    test_open_and_save();
    test_create_new_file();
    test_save_after_removal();

    if (failures != 0)
    {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }

    printf("all %d unit checks passed\n", checks);
    return 0;
}
