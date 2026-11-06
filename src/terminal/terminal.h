#ifndef TERMINAL_H
#define TERMINAL_H

#include <signal.h>
#include <stddef.h>

#include "../viewport/geometry.h"

/* Terminal state for signal handlers. */
extern struct termios saved_termios;
extern volatile sig_atomic_t rawmode_enabled;

/* Switch the controlling terminal between raw mode (no line buffering,
 * no echo, no software flow control) and the saved configuration.
 * Returns 0 on success, -1 on failure. No-op when stdin is not a tty. */
int terminal_rawmode_on(void);
int terminal_rawmode_off(void);

/* Query the current terminal geometry. Returns fallback on failure. */
Size terminal_size(void);

/* Check if stdin is a tty. */
int terminal_is_interactive(void);

#endif
