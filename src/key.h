#ifndef KEY_H
#define KEY_H

/* Key codes returned by the input decoder. Values above 0xff cannot
 * collide with bytes typed by the user. */
#define KEY_CTRL_C        0x03
#define KEY_CTRL_D        0x04
#define KEY_CTRL_H        0x08
#define KEY_CTRL_Q        0x11
#define KEY_CTRL_S        0x13
#define KEY_CTRL_Z        0x1a
#define KEY_CTRL_BACKSLASH 0x1c
#define KEY_ESC           0x1b
#define KEY_BACKSPACE     0x7f

#define KEY_UP            0x100
#define KEY_DOWN          0x101
#define KEY_LEFT          0x102
#define KEY_RIGHT         0x103
#define KEY_RESIZE        0x104

#endif
