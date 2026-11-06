#ifndef EDIT_OPS_H
#define EDIT_OPS_H

#include "src/fileio/fileio.h"
#include "editor_types.h"

/* Edit operation result. */
typedef enum EditResult
{
    EDIT_OK,
    EDIT_ERROR,
} EditResult;

/* Insert a single byte at position. Returns EDIT_OK on success. */
EditResult edit_insert_byte(EditorFile *ef, size_t pos, char byte);

/* Remove the byte at position. Returns EDIT_OK on success. */
EditResult edit_remove_byte(EditorFile *ef, size_t pos);

/* Save the file. Returns EDIT_OK on success. */
EditResult edit_save(EditorFile *ef);

#endif
