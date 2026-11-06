#ifndef RENDER_H
#define RENDER_H

#include "../editor/editor.h"
#include "../viewport/viewport.h"
#include "../editor/editor_types.h"

/* High-level rendering operations. Return 0 on success, -1 on failure. */

int render_draw(const Editor *ed, const Viewport *vp);
int render_apply_change(const Editor *ed, Viewport *vp, const EditorChange *change);

#endif
