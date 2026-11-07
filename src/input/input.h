#ifndef INPUT_H
#define INPUT_H

#include "../key.h"

/* Sentinels returned by input_scan. */
#define INPUT_EOF (-1)
#define INPUT_ERROR (-2)

/* Decode one key press from stdin. Returns INPUT_EOF at end of input,
 * INPUT_ERROR on a read error, KEY_RESIZE when a pending resize
 * interrupted the read, or a decoded key. */
int input_scan(void);

#endif
