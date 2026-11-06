#include "viewport.h"

Viewport viewport_new(Size size)
{
    Viewport vp = {0, size};
    return vp;
}

Size viewport_size(const Viewport *vp)
{
    return vp->size;
}

bool viewport_resize(Viewport *vp, Size size)
{
    if (vp->size.rows == size.rows && vp->size.columns == size.columns)
    {
        return false;
    }

    vp->size = size;
    return true;
}

bool viewport_ensure_visible(Viewport *vp, size_t cursor_row)
{
    if (cursor_row < vp->top_row)
    {
        vp->top_row = cursor_row;
        return true;
    }

    if (cursor_row >= vp->top_row + vp->size.rows)
    {
        vp->top_row = cursor_row + 1 - vp->size.rows;
        return true;
    }

    return false;
}

size_t viewport_screen_row(const Viewport *vp, size_t row)
{
    return row - vp->top_row;
}
