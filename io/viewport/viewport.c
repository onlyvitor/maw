#include "viewport.h"

Size size_fallback(void)
{
    Size size = {24, 80};

    return size;
}

Size size_new(size_t rows, size_t columns)
{
    Size size = {rows > 0 ? rows : 1, columns > 0 ? columns : 1};

    return size;
}

size_t size_content_width(Size size)
{
    if (size.columns <= 1)
    {
        return 1;
    }

    return size.columns - 1;
}

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
