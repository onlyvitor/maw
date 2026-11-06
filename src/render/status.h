#ifndef STATUS_H
#define STATUS_H

#include <stddef.h>

#include "../editor/editor.h"
#include "../viewport/viewport.h"

/* Paint the status line (file name, dirty marker, cursor position).
 * Returns 0 on success, -1 on failure. */
int status_paint(const Editor *ed, const Viewport *vp);

/* Update status line and reposition cursor. */
int status_refresh(const Editor *ed, const Viewport *vp);

#endif
