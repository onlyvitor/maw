# Oldvim: the second C rewrite

This repository carries the C17 implementation of Oldvim. What is
lived in Rust for a while is now back in C: the tree described here
was obtained by restoring the pre-port C implementation from history
and porting, on top of it, the windowing feature set that had been
added in Rust — with a byte-level screen-stream parity check against
the last Rust binary along the way.

## Architecture

| Module | Responsibility |
| --- | --- |
| `fileio/fileio.{c,h}` | `EditorFile`: a growable, NUL-terminated byte buffer plus the file path and metadata (`mode`, `exist`). Whole-file load, byte primitives (`insert_bytes`, `remove_byte_at`), atomic save (temp file + `fsync` + `rename` + parent-dir `fsync`), teardown. |
| `editor/editor.{c,h}` | `Editor` = `EditorFile*` + byte `cursor` + `dirty` flag. Line helpers (`line_start_index`, `line_end_index`, `offset_of_row`, `editor_row_column`), the four motion commands returning a `CursorMotion`, and `editor_handle_key` reporting an `EditorChange`. **No I/O.** |
| `io/input/{input.c,input.h,key.h}` | `scan()`: one decoded key per call. Ctrl+H→backspace, ESC `CSI\`/`SS3` sequences A/B/C/D into arrow keys above 0xFF. |
| `io/terminal/{terminal.c,terminal.h}` | Raw mode via termios (ICANON/ECHO/IXON etc. off, VMIN=1), no-op when stdin is not a tty, async-signal-safe fatal handlers (restore termios, leave the alternate screen, `_exit(128+signum)`), and `TIOCGWINSZ` geometry queried on demand with an 80×24 fallback. |
| `io/viewport/{viewport.c,viewport.h}` | The window onto the buffer: which logical row sits at the top, how wide a row may be (one screen row per logical line, clipped to `columns - 1`). `ensure_visible` scrolls by the least amount needed; `resize` adopts new geometry. Pure model, no escape codes. |
| `io/output/{output.c,output.h}` | Escapes and framing: full-viewport draw (each row `\r\n` + `\033[K` + clipped line), partial repaints (erase-below redraw, in-place byte echo, absolute cursor placement), and the dispatch that consults viewport resize/scroll and paints when they invalidate the screen. |
| `main.c` | Wiring: open, raw mode (which also enters the alternate screen), first draw of the viewport, key loop querying `terminal_size()` each interactive repaint so a resize is picked up without SIGWINCH, `[EOF]` reporting only after the normal screen returns, and the exit-status contract: 0 unless something failed, final save skipped after an error, unconditional save on quit and
clean EOF. |

## What the second rewrite restored

1. The C implementation was restored from repository history
   (`chore: restore the C implementation and remove the Rust port`) —
   its module map, key semantics, atomic save and tests were already
   verified to match the Rust tree byte-for-byte.
2. The windowing model was ported from Rust step by step, mirroring
   the commit sequence of the original feature work: the viewport
   model, `TIOCGWINSZ` sizing, the viewport draw, scroll-to-cursor,
   partial repaints, resize handling and the alternate screen.
3. Every commit was validated against the same gates: `-Wall -Wextra
   -Werror` builds, the unit suite, the pty suite and, from the
   partial-repaint commit onward, a byte-level diff of the terminal
   streams produced by the C and Rust binaries over the scenarios

   - plain inserting and erasing
   - joining lines with backspace
   - arrow motion and column clamping
   - appending newline at the end
   - inserting in the middle of a line
   - scrolling and clipped lines on a large file
   - mid-session resize with two different geometries
   - SIGINT/SIGTERM/SIGHUP/SIGQUIT statuses and untouched files

   All scenarios matched byte-by-byte, including exit codes (130,
   143, 129, 131) and file-content hashes.

One fix came out of that comparison and is kept as its own commit:
`fix: keep drawing the erased rows of a partial repaint` — a redraw
that runs out of buffer before the window floor still needs to emit
the `\r\n`s of the remaining rows, since the erase-below escape already
announced them.

## Intentional C characteristics (same behavior as the port)

1. **NUL-sentinel buffer**: `data[len] == '\0'`. Renderers slice by
   `limit = data + len` rather than trusting the sentinel, so binary
   files stay byte-transparent.
2. **Escape transparency**: the renderer writes raw escape bytes, and
   the last cell of a viewport row is left free so a full row never parks
   the terminal in the wrap-pending state.
3. **No SIGWINCH**: the geometry is queried per key. The pty suite
   drives a 24×80 window (`TIOCSWINSZ`) and asserts against the frame
   since the last full repaint (`ESC[H`).
4. **The `[EOF]` pathway**: clean EOF prints `\n[EOF]\n` after the
   normal screen is back and still saves with status 0.
5. **Allocation failure**: `malloc` failures keep the original
   contract (print `error: cannot allocate memory …`, exit 1); no
   other signal rail exists.

## Gates

```sh
make test-unit       # 170 checks
make test            # 170 unit checks + 15 pty tests
make test-sanitize   # both suites under ASan/UBSan
```
