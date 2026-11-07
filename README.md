# Maw

⣿⣿⣿⣿⣿⣷⣿⣿⣿⡅⡹⢿⠆⠙⠋⠉⠻⠿⣿⣿⣿⣿⣿⣿⣮⠻⣦⡙⢷⡑⠘⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣌⠡⠌⠂⣙⠻⣛⠻⠷⠐⠈⠛⢱⣮⣷⣽⣿
⣿⣿⣿⣿⡇⢿⢹⣿⣶⠐⠁⠀⣀⣠⣤⠄⠀⠀⠈⠙⠻⣿⣿⣿⣦⣵⣌⠻⣷⢝⠦⠚⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⢟⣻⣿⣊⡃⠀⣙⠿⣿⣿⣿⣎⢮⡀⢮⣽⣿⣿
⢿⣿⣿⣿⣧⡸⡎⡛⡩⠖⠀⣴⣿⣿⣿⠀⠀⠀⠀⠸⠇⠀⠙⢿⣿⣿⣿⣷⣌⢷⣑⢷⣄⠻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⣫⠶⠛⠉⠀⠁⠀⠈⠈⠀⠠⠜⠻⣿⣆⢿⣼⣿⣿⣿
⢐⣿⣿⣿⣿⣧⢧⣧⢻⣦⢀⣹⣿⣿⣿⣇⠀⠄⠀⠀⠀⡀⠀⠈⢻⣿⣿⣿⣿⣷⣝⢦⡹⠷⡙⢿⣿⣿⣿⣿⣿⣿⣿⣿⠈⠁⠀⠀⠀⠁⠀⠀⠀⠱⣶⣄⡀⠀⠈⠛⠜⣿⣿⣿⣿
⠀⠊⢫⣿⣏⣿⡌⣼⣄⢫⡌⣿⣿⣿⣿⣿⣦⡈⠲⣄⣤⣤⡡⢀⣠⣿⣿⣿⣿⣿⣿⣷⣼⣍⢬⣦⡙⣿⣿⣿⣿⣿⣯⢁⡄⠀⡀⡀⠀⠄⢈⣠⢪⠀⣿⣿⣿⣦⠀⢉⢂⠹⡿⣿⣿
⠀⠀⠄⢹⢃⢻⣟⠙⣿⣦⠱⢻⣿⣿⣿⣿⣿⣿⣷⣬⣍⣭⣥⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⡙⢿⣼⡿⣿⣿⣿⣿⣿⣷⣄⠘⣱⢦⣤⡴⡿⢈⣼⣿⣿⣿⣇⣴⣶⣮⣅⢻⣿⡏
⠀⠀⠈⠹⣇⢡⢿⡆⠻⣿⣷⠀⢻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣍⡻⣿⣟⣻⣿⣿⣿⣿⣷⣦⣥⣬⣤⣴⣾⣿⣿⣿⣿⣷⣿⣿⣿⣿⣷⡜⠃
⠀⠀⠀⢀⣘⠈⢂⠃⣧⡹⣿⣷⡄⠙⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣮⣅⡙⢿⣟⠿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠋⡕⠂
⠀⠀⠀⠀⠀⠀⠛⢷⣜⢷⡌⠻⣿⣿⣦⣝⣻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣯⣹⣷⣦⣹⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠉⠃⠀

maw is a minimal terminal editor: it opens a full-screen window on
the file, and it edits one byte at a time in raw mode until you quit or
save. The window scrolls to follow the cursor; only the rows a key
disturbed are repainted.

The implementation is pure C17 under `src/`: `app/`, `buffer/`,
`editor/`, `fileio/`, `input/`, `render/`, `terminal/`, `text/` and
`viewport/`. The terminal and I/O contracts are described in
`docs/TERMINAL.md` and `docs/IO.md`.

## Building

```sh
make             # dev build
make release
make test-unit   # unit checks, 382
make test        # unit checks plus the pty suite, 22
make run         # runs it on text.rst
```

The binary is `./maw`.

## Controls

| Key | Action |
| --- | --- |
| printable bytes | insert at cursor |
| Backspace / Ctrl+H | delete the previous character |
| arrows | move the cursor between characters and lines |
| Ctrl+S | save the file and keep editing |
| Ctrl+D / Ctrl+Q | quit (pending edits are saved) |
| Ctrl+Z | suspend and resume with the shell |
| Ctrl+C / Ctrl+\ | ignored by the editor |

## Screen

The editor takes over the alternate screen and gives the shell screen
back on exit, including on SIGINT/SIGTERM/SIGHUP/SIGQUIT. Ctrl+Z and
SIGTSTP restore the terminal before stopping and re-enter the alternate
screen on SIGCONT. It opens with the cursor (the end of the file) on
screen and scrolls the window by the least amount needed to keep the
cursor visible. A scroll or a terminal resize repaints the whole window;
SIGWINCH triggers the resize repaint without waiting for a key. Typing
and cursor motion repaint only the rows they disturb. Long lines are
clipped to the terminal width (the last cell stays free); horizontal
scrolling does not exist yet.

Tabs expand to 8-column stops. Control bytes are displayed in caret
notation and invalid UTF-8 becomes `?`, so file contents cannot inject
escape sequences into the terminal. Cursor columns and the status line
count display cells, not bytes, and arrow/backspace motions respect
UTF-8 character boundaries.

The last row of the window is a status line, in reverse video, showing
the file name, a `*` marker while there are pending edits, and the
1-based cursor position; saving clears the marker.

Saving is atomic: the content goes to `<file>.tmp.XXXXXX`, is synced to
disk, and is renamed over the original. If a save step fails, the
temporary file is kept and its name is printed. Quitting without
pending edits does not rewrite the file. New files respect the process
umask; existing permissions are preserved.

All signals restore the terminal before exiting. Ctrl+C and Ctrl+\ are
plain editing bytes (ignored), not signals; an external SIGINT still
exits with status 130 and does not save pending edits.

Piping stdin into the editor works without a tty as well:

```sh
printf 'ZZ' | maw file.txt > /dev/null
```

## Layout

```
src/main.c                argument check and app entry point
src/app/app.c             open, raw mode, key loop, draw and save wiring
src/buffer/buffer.c       line, row and column helpers over raw bytes
src/editor/editor.c       cursor motions and key dispatch, no I/O
src/editor/edit_ops.c     insert, remove and save primitives
src/fileio/fileio.c       the buffer, loading and the atomic save
src/input/input.c         key decoding and resize interruption
src/key.h                 the shared key constants
src/render/render.c       full and partial repaints
src/render/status.c       status line and cursor placement
src/render/escape.c       escape codes and sanitized text output
src/terminal/terminal.c   raw mode, geometry and alternate screen
src/terminal/signals.c    fatal, resize and suspend/resume signals
src/text/text.c           UTF-8 lengths, display width and tab stops
src/viewport/viewport.c   the visible window and scrolling
src/viewport/geometry.c   terminal geometry helpers
tests/unit_test.c         unit suite
tests/test_editor.py      pty end-to-end suite
docs/TERMINAL.md          terminal, signals and raw mode notes
docs/IO.md                input, rendering and save flow notes
docs/FILES.md             responsibility of every project file
```

## Tests

```sh
make test               # unit + pty against ./maw
make test-sanitize      # both suites under ASan/UBSan
MAW_BIN=./maw python3 tests/test_editor.py
```

`MAW_BIN` points the pty suite at any other build. If the packaged
clang has no ASan/UBSan runtime, run `make test-sanitize CC=gcc`.
