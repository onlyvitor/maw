# Oldvim

Oldvim is a minimal terminal editor: it opens a full-screen window on
the file, and it edits one byte at a time in raw mode until you quit or
save. The window scrolls to follow the cursor; only the rows a key
disturbed are repainted.

The implementation is pure C17 (`main.c`, `editor/`, `fileio/`,
`io/`). A Rust reimplementation that satisfied this behavior was
removed in favor of this C tree, and the current behavior was ported
back from it with byte-level screen-stream parity (see
`docs/PORT.md`).

## Building

```sh
make             # dev build
make release
make test-unit   # unit checks, 170
make test        # unit checks plus the pty suite, 15
make run         # runs it on text.rst
```

The binary is `./oldvim`.

## Controls

| Key | Action |
| --- | --- |
| printable bytes | insert at cursor |
| Backspace / Ctrl+H | delete the previous byte |
| arrows | move the cursor between bytes and lines |
| Ctrl+S | save the file and keep editing |
| Ctrl+D | quit (pending edits are saved) |

## Screen

The editor takes over the alternate screen and gives the shell screen
back on exit, including on SIGINT/SIGTERM/SIGHUP/SIGQUIT. It opens with
the cursor (the end of the file) on screen and scrolls the window by
the least amount needed to keep the cursor visible. A scroll or a
terminal resize repaints the whole window; typing and cursor motion
repaint only the rows they disturb. Long lines are clipped to the
terminal width (the last cell stays free); horizontal scrolling does
not exist yet.

Saving is atomic: the content goes to `<file>.tmp.XXXXXX`, is synced to
disk, and is renamed over the original. If a save step fails, the
temporary file is kept and its name is printed.

Piping stdin into the editor works without a tty as well:

```sh
printf 'ZZ' | oldvim file.txt > /dev/null
```

## Layout

```
main.c                    wiring: open, raw mode, draw, key loop, save
fileio/fileio.c           the buffer, loading and the atomic save
editor/editor.c           cursor motions and key dispatch, no I/O
io/input/input.c          key decoding
io/output/output.c        escape-code rendering
io/terminal/terminal.c    raw mode, terminal size and signal restoration
io/viewport/viewport.c    the visible window and scrolling
io/input/key.h            the key constants
tests/unit_test.c         unit suite
tests/test_editor.py      pty end-to-end suite
docs/PORT.md              architecture and rewrite history
```

## Tests

```sh
make test               # unit + pty against ./oldvim
make test-sanitize      # both suites under ASan/UBSan
OLDVIM_BIN=./oldvim python3 tests/test_editor.py
```

`OLDVIM_BIN` points the pty suite at any other build.
