#ifndef TERMINAL_H
#define TERMINAL_H

/*
 * Switch the controlling terminal between raw mode (no line buffering,
 * no echo, no software flow control) and the saved configuration.
 *
 * Both return 0 on success and -1 when the attributes cannot be changed.
 * When stdin is not a tty they are no-ops.
 */
int rawmode_on(void);
int rawmode_off(void);

#endif
