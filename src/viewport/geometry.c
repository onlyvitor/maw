#include "geometry.h"

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

Size size_viewport(Size screen)
{
    if (screen.rows > 1)
    {
        screen.rows--;
    }
    return screen;
}
