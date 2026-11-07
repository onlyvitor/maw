#ifndef SIGNALS_H
#define SIGNALS_H

/* Install handlers for SIGINT/SIGTERM/SIGHUP/SIGQUIT (restore the
 * terminal and exit), SIGWINCH (request a redraw) and SIGTSTP/SIGCONT
 * (suspend and resume). Returns 0 on success, -1 on failure. */
int signals_install(void);

/* Non-zero when a resize is pending; clears the flag. */
int signals_take_resize(void);

#endif
