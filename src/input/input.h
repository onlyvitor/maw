#ifndef INPUT_H
#define INPUT_H

#include "key.h"

/* Decode one key press from stdin. Returns EOF at end of input or error. */
int input_scan(void);

#endif
