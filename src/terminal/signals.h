#ifndef SIGNALS_H
#define SIGNALS_H

/* Install signal handlers for SIGINT, SIGTERM, SIGHUP, SIGQUIT.
 * Returns 0 on success, -1 on failure. */
int signals_install(void);

#endif
