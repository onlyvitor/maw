#ifndef FILEIO_H
#define FILEIO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct EditorFile
{
    char *path;
    char *data;
    size_t len;
    size_t cap;
    mode_t mode;
    bool exist;
} EditorFile;

EditorFile *open_file(const char *filename);
int insert_bytes(EditorFile *ef, size_t pos, const char *s, size_t n);
int save_file(EditorFile *ef);
void close_file(EditorFile *ef);
int remove_byte_at(EditorFile *ef, size_t pos);

#endif
