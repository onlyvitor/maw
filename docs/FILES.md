# File map

This document describes the responsibility of every tracked file in the
repository. It complements `README.md` (user-facing behavior),
`docs/TERMINAL.md` (terminal and signal contract) and `docs/IO.md`
(input, rendering and save flow).

## Dependency direction

The code is layered so that terminal and filesystem details stay at the
edges:

```text
main.c
└── app/app.c
    ├── editor/editor.c ──> editor/edit_ops.c ──> fileio/fileio.c
    │                  ├── buffer/buffer.c
    │                  └── text/text.c
    ├── input/input.c ──> key.h
    ├── render/render.c ──> render/status.c ──> render/escape.c
    │                  └──> syntax/markdown.c
    ├── terminal/terminal.c ──> terminal/signals.c
    └── viewport/viewport.c ──> viewport/geometry.c
```

`buffer`, `text`, `viewport`, `syntax`, `editor` and `edit_ops` contain no
terminal I/O. Only `fileio` touches files, `input` reads stdin, `render` writes
stdout and `terminal` calls `termios`/`ioctl`.

## Entry point and orchestration

| File | Responsibility |
| --- | --- |
| `src/main.c` | Process entry point. Validates that a file name was given, prints the usage message and delegates to `app_run`. |
| `src/app/app.h` | Declares `app_run`, the single public entry point of the application layer. |
| `src/app/app.c` | Owns the editor lifecycle: opens the file, enables raw mode, creates the viewport, performs the initial draw, runs the key loop, handles resize/suspend actions, saves only when dirty, restores the terminal and prints non-interactive output. It is the only place that wires all modules together. |

## Editing model

| File | Responsibility |
| --- | --- |
| `src/editor/editor_types.h` | Defines the values exchanged between the editor and the app: `CursorMotion`, `EditorAction`, `ChangeKind` and `EditorChange`. Includes the shared key constants from `src/key.h`. |
| `src/editor/editor.h` | Declares the `Editor` state (`file`, byte `cursor`, `dirty` flag) and the public editing API (`editor_open`, `editor_close`, motions and `editor_handle_key`). |
| `src/editor/editor.c` | Implements the editing behavior: opens the file through `fileio`, moves by UTF-8 character boundaries, performs display-column-aware vertical motion, inserts bytes, removes whole code points on backspace, handles save/quit/suspend keys and fills the `EditorChange` that tells the renderer what to repaint. It performs no terminal I/O. |
| `src/editor/edit_ops.h` | Declares the low-level edit operations (`edit_insert_byte`, `edit_remove_byte`, `edit_save`) and their `EditResult` status. |
| `src/editor/edit_ops.c` | Thin editing facade over `fileio`: inserts or removes bytes and saves the buffer. Keeping this layer small lets the editor state stay independent of buffer allocation details. |
| `src/fileio/fileio.h` | Defines `EditorFile` (path, byte buffer, length, capacity, mode, existence flag) and the file API used by the editor and tests. |
| `src/fileio/fileio.c` | Loads a file (or starts an empty buffer for a new path), grows the byte buffer on demand, and saves atomically: writes `<file>.tmp.XXXXXX` in the same directory, applies permissions, calls `fsync`, renames over the target and syncs the parent directory. New files use `0666 & ~umask`; existing files keep their `0777` mode bits. Also provides `fileio_from_data` for unit tests. |
| `src/buffer/buffer.h` | Declares the read-only line helpers over the raw byte buffer: line start, line end, row/byte-column lookup and first byte of a row. |
| `src/buffer/buffer.c` | Implements those helpers by scanning for `\n`. It knows nothing about terminals, display widths or allocation. A trailing newline opens one final empty row. |
| `src/text/text.h` | Declares the UTF-8 and display-width contract: character length, character cells, text width (optionally from a starting display column), previous/next character boundary and byte offset for a target display column. |
| `src/text/text.c` | Custom UTF-8 decoder and width tables. Treats invalid or truncated sequences as one byte, counts combining marks as zero cells, wide/emoji ranges as two cells, expands tabs at 8-column stops and skips a trailing `\r` from CRLF. Used by motions, the status line and the renderer so all columns are measured consistently. |

## Input and terminal

| File | Responsibility |
| --- | --- |
| `src/key.h` | Single source of truth for key codes. Bytes below `0x100` can collide with inserted text; synthesized keys (`KEY_UP`, `KEY_RESIZE`, etc.) start at `0x100`. |
| `src/input/input.h` | Declares `input_scan` and the `INPUT_EOF`/`INPUT_ERROR` sentinels. |
| `src/input/input.c` | Reads one byte at a time with `read(2)`, maps `Ctrl+H` to backspace and decodes `ESC [ A/B/C/D` and `ESC O A/B/C/D` into arrow keys. `EINTR` with a pending resize returns `KEY_RESIZE`; a lone ESC followed by a normal key pushes that key back. |
| `src/terminal/terminal.h` | Declares the terminal contract: saved/raw `termios`, the `rawmode_enabled` flag used by signal handlers, the alternate-screen sequences, raw-mode entry/exit, size query and tty check. |
| `src/terminal/terminal.c` | Implements raw mode (canonical input, echo, `ISIG` and software flow control off; `VMIN=1`, `VTIME=0`), enters/leaves the alternate screen, queries `TIOCGWINSZ` on stdout with an 80x24 fallback and reports whether stdin is a tty. It installs the signal handlers before entering raw mode. |
| `src/terminal/signals.h` | Declares `signals_install` and the resize flag consumer `signals_take_resize`. |
| `src/terminal/signals.c` | Installs handlers for fatal signals (`SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`), `SIGWINCH`, `SIGTSTP` and `SIGCONT`. Fatal signals restore the terminal and exit with `128 + signum`; resize only sets a flag; suspend restores the terminal and re-raises `SIGTSTP`; continue re-enters raw mode and the alternate screen. Handlers use `write(2)` and stay out of stdio. |

## Syntax

| File | Responsibility |
| --- | --- |
| `src/syntax/markdown.h` | Declares the semantic Markdown styles (`heading`, `link`), styled byte spans, fenced-block state and the scanning API. It knows nothing about terminals, escape codes or I/O. |
| `src/syntax/markdown.c` | Line-based Markdown scanner. Detects ATX headings, inline links, inline code spans and fenced code blocks (carrying fence state from line to line), and returns sorted, non-overlapping byte spans. `markdown_applies` matches the `.md` and `.markdown` extensions case-insensitively; `markdown_state_at` rebuilds the fence state at the first drawn line. |

## Rendering

| File | Responsibility |
| --- | --- |
| `src/render/escape.h` | Declares the low-level output primitives (`puts`, `write`, `putc`, flush, cursor movement, erase), the sanitized text writer `escape_write_text` and its styled variant `escape_write_styled_text`. |
| `src/render/escape.c` | Implements escape-code output and the only safe path for writing file content: clips to display cells, expands tabs, renders control bytes in caret notation and replaces invalid UTF-8 (including C1 controls) with `?`. This prevents file bytes from injecting terminal escapes. The styled variant always resets attributes, including on failure or clipping. |
| `src/render/render.h` | Declares the high-level rendering operations: a full draw and the minimal repaint for a change. |
| `src/render/render.c` | Implements the repaint strategy. A full draw walks the visible rows and clips each line; `render_apply_change` chooses status-only refresh, line repaint or the append fast path. Scrolling or resizing triggers a full repaint. In Markdown files it maps the scanner's spans to fixed SGR colors (bold yellow headings, underlined blue links); every append redraws the line so styled text is updated as it is typed. Printable ASCII appends in other files are echoed in place; control bytes, UTF-8 bytes and over-width appends redraw the line. |
| `src/render/status.h` | Declares status-line painting, cursor placement and the combined refresh operation. |
| `src/render/status.c` | Paints the reverse-video status row with the file name, dirty marker, 1-based row/display column and the `^S:save  ^Q:quit` hint. It sanitizes the path through `escape_write_text` and positions the terminal cursor using display cells, clamped to the content width and visible rows. |

## Viewport and geometry

| File | Responsibility |
| --- | --- |
| `src/viewport/geometry.h` | Defines `Size` and the geometry helpers shared by the terminal, app, renderer and viewport. |
| `src/viewport/geometry.c` | Supplies the 80x24 fallback, clamps dimensions to at least one, computes content width as `columns - 1` (leaving the wrap-pending cell free) and reserves the last screen row for the status line. |
| `src/viewport/viewport.h` | Declares the `Viewport` state (`top_row`, geometry) and its scrolling API. |
| `src/viewport/viewport.c` | Implements viewport creation, resize detection, minimal scrolling to keep the cursor row visible and mapping from buffer rows to screen rows. It performs no I/O. |

## Tests and build

| File | Responsibility |
| --- | --- |
| `tests/unit_test.c` | Deterministic unit suite. Covers buffer line/row helpers, UTF-8 length and width, cursor motions (including display columns), key handling, backspace over code points, edit operations, file modes/umask, viewport/geometry and the Markdown scanner (headings, links, inline code, fences and extension matching). Uses the local `CHECK`/`CHECK_SIZE` macros; new test functions must be added to `main`. |
| `tests/test_editor.py` | End-to-end suite driven through a pty. Spawns the built binary, sends real key bytes and asserts screen output and file contents. Covers editing, scrolling, clipping, resize, suspend/resume, sanitization, UTF-8, the status line, alternate screen, save semantics, non-interactive stdin and Markdown highlighting (including suppression inside code). |
| `Makefile` | Build and test entry points. `all`/`debug` produce `maw`; `release` uses `-O2`; `run` opens `text.rst`; `test-unit` builds and runs the unit suite; `test` also runs the pty suite with `MAW_BIN=./maw`; `test-sanitize` builds both with ASan/UBSan; `clean` removes generated binaries. `CC` defaults to clang but can be overridden; headers are discovered automatically. |

Generated artifacts (`maw`, `tests/unit_tests`, `__pycache__/`) are
ignored and must not be committed.

## Documentation and repository files

| File | Responsibility |
| --- | --- |
| `README.md` | Project overview, build/test commands, keyboard controls and user-visible terminal/save behavior. |
| `docs/TERMINAL.md` | Terminal contract: raw-mode flags, alternate screen, geometry and signal behavior. |
| `docs/IO.md` | Input decoding, editing model, repaint strategy, sanitized output and the atomic save flow. |
| `docs/FILES.md` | This file map. Update it when a file is added, removed or changes responsibility. |
| `.gitignore` | Ignores built binaries, Python bytecode, editor metadata, temporary saves and the sample `text.rst`. |
