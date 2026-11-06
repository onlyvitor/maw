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

EditorFile *fileio_open(const char *filename);
void fileio_close(EditorFile *ef);
int fileio_insert(EditorFile *ef, size_t pos, const char *s, size_t n);
int fileio_remove(EditorFile *ef, size_t pos);
int fileio_save(EditorFile *ef);

/* For testing: create an EditorFile from existing data. */
EditorFile *fileio_from_data(const char *path, const char *data, size_t len);

#endif
