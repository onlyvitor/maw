# Terminal contract

`src/terminal/` owns the terminal state. Nothing else calls `termios`
or writes the alternate-screen sequences directly.

## Raw mode

When stdin is a tty, `terminal_rawmode_on` saves the current termios and
enables:

- `ICANON` and `ECHO` off (one byte per read, no echo);
- `ISIG` off (`Ctrl+C`, `Ctrl+Z` and `Ctrl+\` arrive as bytes);
- `IXON`, `IXOFF` and `IXANY` off (`Ctrl+S` and `Ctrl+Q` arrive as bytes);
- `VMIN = 1`, `VTIME = 0` (blocking reads).

The editor maps the bytes itself: `Ctrl+S` saves, `Ctrl+D`/`Ctrl+Q`
quit, `Ctrl+Z` suspends and `Ctrl+C`/`Ctrl+\` are ignored. An external
`SIGINT` still exits with status 130.

When stdin is not a tty, raw mode is a no-op and the alternate screen
is never entered. The input loop still reads stdin, which is how
`printf 'ZZ' | maw file.txt` works.

## Screen and geometry

The editor enters the alternate screen with `\033[?1049h` and leaves it
with `\033[?1049l`. Geometry comes from `TIOCGWINSZ` on stdout, queried
on demand with an 80x24 fallback. The last screen row is reserved for
the status line; content rows use `columns - 1` cells so a full row
never leaves the terminal in the wrap-pending state.

`SIGWINCH` only sets a flag. When the flag is observed, the app queries
the new geometry, scrolls the viewport to keep the cursor visible and
repaints the whole window. The read in `src/input/input.c` handles
`EINTR` and returns `KEY_RESIZE`, so a resize repaints without a key
press.

## Signals

| Signal | Behavior |
| --- | --- |
| `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT` | restore termios, leave the alternate screen, `_exit(128 + signum)` |
| `SIGWINCH` | set the resize flag; the main loop repaints |
| `SIGTSTP` | restore termios, leave the alternate screen, re-raise with the default action |
| `SIGCONT` | if suspended, re-enter raw mode and the alternate screen, then repaint |

The `SIGTSTP` handler unblocks `SIGTSTP` before re-raising; without this
the pending signal would be delivered to the handler again after it
returns. Handlers use `write(2)` rather than stdio so they stay safe
enough for signal context. The fatal handlers do not save pending edits.
