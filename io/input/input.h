#ifndef INPUT_H
#define INPUT_H

#include "key.h"

#include <stdio.h>

/* Decode one key press from stdin. Returns EOF at end of input. */
int scan(void);

#endif
