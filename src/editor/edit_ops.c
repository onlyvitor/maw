#include "edit_ops.h"

EditResult edit_insert_byte(EditorFile *ef, size_t pos, char byte)
{
    return fileio_insert(ef, pos, &byte, 1) == 0 ? EDIT_OK : EDIT_ERROR;
}

EditResult edit_remove_byte(EditorFile *ef, size_t pos)
{
    return fileio_remove(ef, pos) == 0 ? EDIT_OK : EDIT_ERROR;
}

EditResult edit_save(EditorFile *ef)
{
    return fileio_save(ef) == 0 ? EDIT_OK : EDIT_ERROR;
}
