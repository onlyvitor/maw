/* Feature-test macro: must be defined before any system header. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fileio.h"

/* Keep only the permission bits of the original file. */
#define PERMISSION_MASK 07777
#define DEFAULT_FILE_MODE 0644

/* Smallest allocation for a buffer that started empty. */
#define MIN_BUFFER_CAPACITY 64

/* Leave room so the first edits do not reallocate the whole buffer. */
static size_t initial_capacity(size_t len)
{
    if (len < MIN_BUFFER_CAPACITY)
    {
        return MIN_BUFFER_CAPACITY;
    }

    if (len > (SIZE_MAX - 1) / 2)
    {
        return len + 1;
    }

    return len + len / 2 + 1;
}

static char *dup_string(const char *s)
{
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);

    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, s, len);

    return copy;
}

static char *make_temp_path(const char *path)
{
    static const char suffix[] = ".tmp.XXXXXX";
    size_t len = strlen(path);

    char *tmp = malloc(len + sizeof(suffix));

    if (tmp == NULL)
    {
        return NULL;
    }

    memcpy(tmp, path, len);
    memcpy(tmp + len, suffix, sizeof(suffix));

    return tmp;
}

static int write_all(int fd, const char *data, size_t len)
{
    size_t written = 0;

    while (written < len)
    {
        ssize_t rc = write(fd, data + written, len - written);

        if (rc < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        written += (size_t)rc;
    }

    return 0;
}

static void fsync_parent_dir(const char *path)
{
    char *copy = dup_string(path);

    if (copy == NULL)
    {
        return;
    }

    char *slash = strrchr(copy, '/');
    const char *dir;

    if (slash == NULL)
    {
        dir = ".";
    }
    else if (slash == copy)
    {
        slash[1] = '\0';
        dir = copy;
    }
    else
    {
        *slash = '\0';
        dir = copy;
    }

    int fd = open(dir, O_RDONLY);

    if (fd >= 0)
    {
        /* Best effort: the rename already happened. */
        fsync(fd);
        close(fd);
    }

    free(copy);
}

/* Read the whole file into a NUL-terminated buffer. */
static int load_content(const char *filename, bool exist, char **out_data, size_t *out_len)
{
    char *content = NULL;

    if (!exist)
    {
        content = malloc(1);

        if (content == NULL)
        {
            fprintf(stderr, "error: cannot allocate memory for the file content\n");
            return -1;
        }

        content[0] = '\0';
        *out_data = content;
        *out_len = 0;
        return 0;
    }

    FILE *fp = fopen(filename, "rb");

    if (fp == NULL)
    {
        fprintf(stderr, "error: cannot open %s: %s\n", filename, strerror(errno));
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "error: cannot seek to the end of the file\n");
        fclose(fp);
        return -1;
    }

    long file_end = ftell(fp);

    if (file_end < 0)
    {
        fprintf(stderr, "error: cannot get the file size\n");
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "error: cannot seek to the start of the file\n");
        fclose(fp);
        return -1;
    }

    size_t file_size = (size_t)file_end;

    content = malloc(file_size + 1);

    if (content == NULL)
    {
        fprintf(stderr, "error: cannot allocate memory for the file content\n");
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(content, 1, file_size, fp);

    if (read_bytes != file_size)
    {
        if (ferror(fp))
        {
            fprintf(stderr, "error: cannot read the file\n");
        }
        else
        {
            fprintf(stderr, "error: unexpected end of file\n");
        }

        free(content);
        fclose(fp);
        return -1;
    }

    if (fclose(fp) != 0)
    {
        fprintf(stderr, "error: cannot close the file\n");
        free(content);
        return -1;
    }

    content[read_bytes] = '\0';

    *out_data = content;
    *out_len = read_bytes;

    return 0;
}

/* Take ownership of content on success and of nothing on failure. */
static EditorFile *make_editor_file(const char *filename, char *content, size_t len,
                                    const struct stat *st, bool exist)
{
    EditorFile *ef = malloc(sizeof *ef);

    if (ef == NULL)
    {
        fprintf(stderr, "error: cannot allocate memory for the editor file\n");
        free(content);
        return NULL;
    }

    ef->path = dup_string(filename);

    if (ef->path == NULL)
    {
        fprintf(stderr, "error: cannot allocate memory for the file path\n");
        free(content);
        free(ef);
        return NULL;
    }

    ef->data = content;
    ef->len = len;
    ef->cap = initial_capacity(len);

    char *grown = realloc(content, ef->cap);

    if (grown != NULL)
    {
        ef->data = grown;
    }
    else
    {
        /* Keep the smaller allocation; insert_bytes will grow it later. */
        ef->cap = len + 1;
    }
    ef->mode = exist ? (st->st_mode & PERMISSION_MASK) : DEFAULT_FILE_MODE;
    ef->exist = exist;

    return ef;
}

EditorFile *open_file(const char *filename)
{
    struct stat st;
    bool exist = false;

    if (stat(filename, &st) == 0)
    {
        exist = true;
    }
    else if (errno != ENOENT)
    {
        fprintf(stderr, "error: cannot stat %s: %s\n", filename, strerror(errno));
        return NULL;
    }

    char *content = NULL;
    size_t content_len = 0;

    if (load_content(filename, exist, &content, &content_len) != 0)
    {
        return NULL;
    }

    return make_editor_file(filename, content, content_len, &st, exist);
}

int insert_bytes(EditorFile *ef, size_t pos, const char *s, size_t n)
{
    if (ef == NULL)
    {
        fprintf(stderr, "error: cannot insert into a null editor file\n");
        return -1;
    }

    if (pos > ef->len)
    {
        fprintf(stderr, "error: insert position is out of range\n");
        return -1;
    }

    if (n == 0)
    {
        return 0;
    }

    if (n > SIZE_MAX - ef->len - 1)
    {
        fprintf(stderr, "error: the file content would be too large\n");
        return -1;
    }

    size_t needed = ef->len + n + 1;

    if (needed > ef->cap)
    {
        size_t cap = ef->cap > 0 ? ef->cap : MIN_BUFFER_CAPACITY;

        while (cap < needed)
        {
            if (cap > SIZE_MAX / 2)
            {
                cap = needed;
                break;
            }

            cap *= 2;
        }

        char *data = realloc(ef->data, cap);

        if (data == NULL)
        {
            fprintf(stderr, "error: cannot grow the file content buffer\n");
            return -1;
        }

        ef->data = data;
        ef->cap = cap;
    }

    memmove(ef->data + pos + n, ef->data + pos, ef->len - pos);
    memcpy(ef->data + pos, s, n);
    ef->len += n;
    ef->data[ef->len] = '\0';

    return 0;
}

/* Create tmp_path and fill it with the buffer, syncing it to disk. */
static int write_temp_file(const EditorFile *ef, char *tmp_path)
{
    int fd = mkstemp(tmp_path);

    if (fd < 0)
    {
        fprintf(stderr, "error: cannot create %s: %s\n", tmp_path, strerror(errno));
        return -1;
    }

    bool ok = true;

    if (write_all(fd, ef->data, ef->len) != 0)
    {
        perror("error: cannot write the temporary file");
        ok = false;
    }

    if (ok && fchmod(fd, ef->mode) != 0)
    {
        fprintf(stderr, "error: cannot set the file permissions: %s\n", strerror(errno));
        ok = false;
    }

    if (ok && fsync(fd) != 0)
    {
        perror("error: cannot sync the temporary file");
        ok = false;
    }

    if (close(fd) != 0)
    {
        perror("error: cannot close the temporary file");
        ok = false;
    }

    return ok ? 0 : -1;
}

/* Atomically put the temporary file in place of the edited file. */
static int replace_file(const char *tmp_path, const char *path)
{
    if (rename(tmp_path, path) != 0)
    {
        fprintf(stderr, "error: cannot replace %s: %s\n", path, strerror(errno));
        return -1;
    }

    fsync_parent_dir(path);

    return 0;
}

int save_file(EditorFile *ef)
{
    char *tmp_path = make_temp_path(ef->path);

    if (tmp_path == NULL)
    {
        fprintf(stderr, "error: cannot allocate memory for the temporary path\n");
        return -1;
    }

    if (write_temp_file(ef, tmp_path) != 0 || replace_file(tmp_path, ef->path) != 0)
    {
        /* Keep the temporary file so the edited content is not lost. */
        fprintf(stderr, "error: the edited content was kept in %s\n", tmp_path);
        free(tmp_path);
        return -1;
    }

    free(tmp_path);

    ef->exist = true;

    return 0;
}

void close_file(EditorFile *ef)
{
    if (ef == NULL)
    {
        return;
    }

    free(ef->path);
    free(ef->data);
    free(ef);
}

int remove_byte_at(EditorFile *ef, size_t pos)
{
    if (ef == NULL)
    {
        fprintf(stderr, "error: cannot remove a byte from a null editor file\n");
        return -1;
    }

    if (pos >= ef->len)
    {
        fprintf(stderr, "error: remove position is out of range\n");
        return -1;
    }

    memmove(ef->data + pos, ef->data + pos + 1, ef->len - pos - 1);
    ef->len--;
    ef->data[ef->len] = '\0';

    return 0;
}
