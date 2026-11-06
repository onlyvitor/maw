#ifndef TERMINAL_H
#define TERMINAL_H

#include "../viewport/viewport.h"

/*
 * Switch the controlling terminal between raw mode (no line buffering,
 * no echo, no software flow control) and the saved configuration.
 *
 * Both return 0 on success and -1 when the attributes cannot be changed.
 * When stdin is not a tty they are no-ops.
 */
int rawmode_on(void);
int rawmode_off(void);

/*
 * The current terminal geometry, in cells. Terminals that report
 * nothing (including a non-tty stdout) fall back to 80x24.
 */
Size terminal_size(void);

#endif
