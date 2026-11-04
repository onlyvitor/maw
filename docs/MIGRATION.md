# Oldvim → Rust Migration Document

This document is the plan of record for reimplementing Oldvim in Rust.
The original C implementation is the single source of truth for
behavior. It lived at the top of this repository until the port
replaced it; its exact state is preserved in the Git history under the
tag `c-original`.

---

## 1. Original Architecture Overview

Oldvim is a minimal, line-oriented terminal text editor written in C17 for
POSIX systems. It opens a single file, prints it verbatim to the terminal,
and enters a raw-mode loop where every key press edits the buffer in place.
The cursor is a byte offset into the buffer. On quit (Ctrl+D) or save
(Ctrl+S) the buffer is written back atomically through a temporary file.

### Module map (post-`f2b79ae` layout)

| Path | Responsibility |
| --- | --- |
| `main.c` | Argument parsing, wiring: open editor, raw mode, draw file, key loop, final save. Owns the exit-status contract. |
| `fileio/fileio.{c,h}` | `EditorFile`: a growable, NUL-terminated byte buffer plus the file path and metadata (`mode`, `exist`). Whole-file load (`open_file`), byte primitives (`insert_bytes`, `remove_byte_at`), atomic save (`save_file` = temp file + `fsync` + `rename` + parent-dir `fsync`), teardown. |
| `editor/editor.{c,h}` | `Editor` = `EditorFile*` + byte `cursor` + `dirty` flag. Pure line helpers (`line_start_index`, `line_end_index`, `editor_row_column`), the four motion commands (`editor_move_*` returning a `CursorMotion` for the renderer), and the key dispatcher `editor_handle_key` returning an `EditorAction` plus an `EditorChange` describing what to repaint. **No I/O here.** |
| `io/input/{input.c,input.h,key.h}` | `scan()`: one decoded key per call. Maps Ctrl+H→backspace, decodes ESC-initiated CSI (`ESC [`) and SS3 (`ESC O`) sequences A/B/C/D into arrow keys >0xFF. `key.h` defines the key constants. |
| `io/terminal/{terminal.c,terminal.h}` | `rawmode_on`/`rawmode_off` via termios (ICANON, ECHO, IXON/IXOFF/IXANY off; VMIN=1/VTIME=0), no-ops when stdin is not a tty, saves/restores the original attributes, and installs async-signal-safe handlers (restore termios + `_exit(128+signum)`) for SIGINT/SIGTERM/SIGHUP/SIGQUIT. Global state: `saved_termios`, `rawmode_enabled`. |
| `io/output/{output.c,output.h}` | Screen primitives (putc/puts/write/flush, cursor escape codes) and change-driven rendering: `screen_apply_change` maps an `EditorChange` to minimal repaints (append echo, single-line redraw with erase-below, cursor motion codes). |
| `tests/unit_test.c` | 131 self-contained check-macro unit tests over editor + fileio. |
| `tests/test_editor.py` | 8 end-to-end tests driving the binary through a pty. |
| `Makefile` | clang, `-Wall -Wextra -Werror`, debug/release/sanitize variants, unit + pty test targets. |

### Execution flow

```
main(argc)
  ├─ editor_open(path)              -> EditorFile loaded + Editor{cursor=len, dirty=false}
  ├─ rawmode_on()                   -> termios raw mode (no-op if not a tty)
  ├─ screen_write(file->data, len)  -> print the whole file (only if isatty)
  ├─ loop: scan() -> key
  │    ├─ EOF      -> ferror ? status=1 : print "[EOF]"; break
  │    ├─ editor_handle_key(ed, key, &change)
  │    │     CTRL_D      -> ACTION_QUIT
  │    │     CTRL_S      -> save_file(); dirty=false
  │    │     arrows      -> editor_move_*() -> CHANGE_CURSOR(motion)
  │    │     BACKSPACE   -> remove_byte_at(cursor-1) -> CHANGE_LINE
  │    │     other byte  -> insert_bytes(cursor, byte) -> CHANGE_APPEND|CHANGE_LINE
  │    └─ screen_apply_change(ed, &change)   (only if isatty)
  ├─ rawmode_off()
  ├─ save_file(ed->file)            -> unconditional save on exit path (status==0)
  └─ editor_close(ed); return status
```

### Key implementation details that must be preserved

1. **Byte-oriented, no editing modes.** Insert/backspace are single-byte
   operations; the "vim" identity here is the key-driven line editing
   model, not modal editing or undo.
2. **Cursor semantics.** `editor_open` puts the cursor at end of buffer
   (`file->len`). Left at line start jumps to end of previous line
   (`PREVIOUS_LINE`); Right on a newline jumps to start of next line
   (`NEXT_LINE`). Up/Down clamp the column to the target line's length
   in bytes. Motions return `NONE` at the boundary.
3. **`EditorChange` rendering contract.** The editor never touches the
   terminal. It reports: `CHANGE_NONE`, `CHANGE_CURSOR(motion)`,
   `CHANGE_LINE{start, removed_newline}`, `CHANGE_APPEND{byte}`. The
   output layer implements `screen_redraw_from`: CR + erase-below + write
   `data[start..len]` + move up N rows (newlines past the cursor) + CR +
   goto column.
4. **Non-interactive mode.** When stdin is not a tty: no raw mode, no
   screen output, keys are just bytes from stdin (arrows not decoded
   meaningfully in cooked mode), final save still happens. This is also
   how the pty-less unit path works.
5. **Atomic save.** Temp path = `<path>.tmp.XXXXXX` via `mkstemp`,
   `fchmod` to saved mode (0644 default for new files), `fsync`,
   `close`, `rename`, best-effort parent-dir `fsync`. On any failure the
   temp file is deliberately kept and its name reported.
6. **Buffer invariants.** `data` is always NUL-terminated at `data[len]`.
   Insert grows the capacity geometrically (double, min 64). Initial
   capacity is `len + len/2 + 1` (min 64, fallback `len+1` if realloc
   fails).
7. **Error contract.** Errors print `error: ...` to stderr and turn into
   exit status 1. Ctrl+S save failure is `ACTION_ERROR` (loop breaks
   with status 1 — note: the final save is skipped in that path
   because `status != 0`).
8. **Ctrl+S reachability.** Flow control (IXON etc.) must be disabled in
   the same raw-mode switch, or Ctrl+S is eaten by the tty.
9. **EOF handling.** Clean EOF (no error) prints `\n[EOF]\n` and still
   falls through to save + exit 0.
10. **Signal safety.** Termios handlers restore the terminal and exit
    with `128+signum`; no cleanup of files occurs in that path.

### Known limitations / technical debt (inherited, not fixed silently)

- `EditorChange.start` for a backspace on a newline is computed as the
  line start *before* removal — same for insert; rendering covers whole
  remainder of file from `start`, so behavior is correct but repaint
  volume grows with file size.
- `scan()` silently swallows unknown escape sequences byte-wise (e.g.
  partial reads could misinterpret; acceptable for a POSIX tty with
  VMIN=1).
- `editor_handle_key` takes `int key` and casts to `char` — all keys
  above 0xFF must be handled before that point (arrows are). In Rust this
  becomes an enum, eliminating the cast.
- Global mutable state in `terminal.c` (`saved_termios`) is required by
  the signal handler; in Rust the signal handling strategy needs an
  equivalent.
- Saving uses the path string, not a handle: the file's directory must
  still exist at save time; saving across a moved parent is best-effort.
- No scroll, no windowing, no status line, no undo, no modes. This is the
  feature set by design (see Feature Inventory).

---

## 2. Feature Inventory (must all exist in Rust)

| # | Feature | Behavior anchor |
| --- | --- | --- |
| F1 | `oldvim <file>` CLI; usage error on missing arg, exit 1 | `main.c:10-16` |
| F2 | Open existing file: full read into buffer, mode bits captured | `open_file` |
| F3 | Open non-existing file: empty buffer, default mode 0644, `exist=false` | `open_file` |
| F4 | Initial cursor at end of buffer; not dirty | `editor_open` |
| F5 | Whole-file draw on startup + flush (tty only) | `main.c:34` |
| F6 | Raw mode on/off with saved-termios restore; tty detection no-op | `terminal.c` |
| F7 | Signal handlers restore termios and exit 128+n (INT/TERM/HUP/QUIT) | `terminal.c:15-43` |
| F8 | Ctrl+S: immediate atomic save mid-session (`save must not wait for quit`) | `editor_handle_key` |
| F9 | Ctrl+D: quit, pending edits saved by the final save | `main.c:61` |
| F10 | Final unconditional save after loop (even after clean EOF) | `main.c:85` |
| F11 | Typed byte echo: insert at cursor, cursor++, `CHANGE_APPEND` when at end | `editor_handle_key` |
| F12 | Insert in middle: line redraw from `start` | `editor_handle_key` |
| F13 | Backspace: remove previous byte; no-op at 0; newline removal joins lines and moves cursor up a row | `editor_handle_key` |
| F14 | Arrow decoding: CSI + SS3 forms for A/B/C/D | `input.c` |
| F15 | Ctrl+H mapped to backspace; ^S/^D pass through | `input.c`, `key.h` |
| F16 | Motions: left/right/up/down with the exact boundary + clamping semantics from §1.2 | `editor.c:89-164` |
| F17 | Byte primitives: insert (geometry growth, NUL invariants), remove-at | `fileio.c:300-360,453-472` |
| F18 | Atomic save pipeline (mkstemp → write → fchmod → fsync → close → rename → dir fsync) with kept-temp-on-failure message | `fileio.c:363-439` |
| F19 | Non-interactive stdin: no raw mode, no drawing, pipes work, still saves | `main.c:31,34,72` |
| F20 | `[EOF]` message on clean EOF, status 0, final save happens | `main.c:44-56` |
| F21 | Error messages to stderr, `error: ...` wording, exit status 1 paths | throughout |
| F22 | Unit test suite (equivalent coverage) | `tests/unit_test.c` |
| F23 | End-to-end pty test suite (equivalent coverage) | `tests/test_editor.py` |
| F24 | Makefile-equivalent build entry points (cargo check/test/run; C build untouched) | `Makefile` |

## 3. Module-to-Module Migration Mapping

| C module | Rust module | Notes |
| --- | --- | --- |
| `fileio/fileio.c` | `src/fileio.rs` | `EditorFile` → owned struct `EditorFile { path: PathBuf, data: Vec<u8>, mode: u32, exist: bool }`. `Vec<u8>` replaces `data/len/cap` (capacity/holding a NUL sentinel disappear; insertion growth handled by `Vec`). The NUL terminator is not needed by any consumer other than C string ops — the render layer slices `data[start..len]`. |
| `fileio` atomic save | `src/fileio.rs` (private fns) | Same pipeline using `libc::mkstemp` directly (a hand-rolled random-suffix fallback would only weaken the exclusivity guarantee that mkstemp already provides). `std` marks close() as unfallible, so the fallible `close` is performed through libc with the same error check as the original. |
| `io/terminal` | `src/io/terminal.rs` | Raw mode via `libc::tcgetattr/tcsetattr` (termios is not in `std`). State lives in the main flow instead of globals where possible; the signal-handler path needs process-global state — kept minimal and initialized in `main`. |
| `io/input` | `src/io/input.rs` | `scan() -> Key` with `enum Key { Byte(u8), Backspace, CtrlS, CtrlD, Exit, Up, Down, Left, Right }`. `Exit` replaces EOF (std read gives `Result<usize, io::Error>`; error ⇒ propagate). |
| `io/output` | `src/io/output.rs` | Same primitives over `io::stdout().write()`; `apply_change(ed, change)` keeps the exact escape-code sequences. |
| `editor/editor.c` | `src/editor.rs` | `Editor { file: EditorFile, cursor: usize, dirty: bool }`. `EditorAction`/`CursorMotion`/`EditorChange` become enums/struct with the same variants; `Option` vs the C NULL-input flexibility of `editor_row_column` → two methods `row(pos)` / `column(pos)` (callers only ever want one of them anyway). |
| `main.c` | `src/main.rs` | Thin driver; module internals independent of terminal I/O (output module parameter-less as in C). |
| `tests/unit_test.c` | `#[cfg(test)]` unit tests in each module + `tests/` integration | Same cases, expressed with `assert_eq!`. |
| `tests/test_editor.py` | `tests/e2e_test.py` | Same pty harness, pointed at the Rust binary (kept as a script, matching the original approach; pty testing in pure Rust would add a dep for no fidelity gain). |

### Dependencies policy

- `libc` (for termios + low-level file flags only) — justified: `std`
  does not expose termios or `O_EXCL`-with-mode file creation details
  uniformly. No other runtime dependencies.
- Dev/image deps: none; the Python e2e harness is reused as-is.

## 4. Rust-Specific Architectural Decisions

1. **Ownership replaces `EditorFile*` sharing.** C has `Editor` owning
   `EditorFile*`; Rust `Editor` owns `EditorFile` by value. Output layer
   takes `&Editor`/`&EditorFile` — no clones.
2. **`EditorChange` as an enum** with data-carrying variants
   (`Cursor(Motion) | Line { start: usize, removed_newline: bool } |
   Append(u8)`), exactly mirroring the C union-like struct (the C code
   keeps `start` even for APPEND; render only reads it for LINE — verify
   parity: C's `CHANGE_APPEND` ignores `start`; enum encodes that).
   Note: C sets `change->start = start` even for CHANGE_APPEND, harmless
   because output switches on kind. The Rust enum makes the dead field
   impossible.
3. **`Key` enum** replaces the >0xFF int trick (`key.h` values above
   0xFF exist only so a `char` can't collide). Casting a large int to
   signed char in C wraps via `(char)key`; the enum removes that class of
   bug.
4. **EOF vs error**: `scan()` returns `Option<Key>` where the read error
   is surfaced as `io::Result`; main maps: `Err` → status 1, `None` →
   `[EOF]` path, `Some(k)` → dispatch.
5. **Error handling**: `Result<T, EditorError>` with `Display` producing
   the exact `error: ...` lines; `FileioError` carries the failing path
   where the C code prints it inline.
6. **Raw mode struct** `RawMode` with `enable`/`disable`/`Drop` guard
   semantics but explicit `off()` call parity with C (main calls
   `rawmode_off` explicitly before saving, so a guard's Drop is
   unnecessary duplication — keep explicit API, same call order).
7. **Signals**: `libc::signal`-based handlers writing the saved termios
   and `_exit(128+n)`, mirroring C. (Alternative: `signal-hook` crate —
   rejected to keep deps minimal.) The saved termios is stored in a
   `static` guarded setup, populated before the handlers are armed,
   exactly like the C code; no data structures are touched in the
   handler beyond the restore, matching the async-signal-safe contract.
8. **Rendering**: take `&EditorEvents`-free approach — output fns take
   exactly what the C fns take (`&EditorFile`, cursor, motion/change).
   Escape sequences byte-for-byte: `\033[J`, `\033[<n>C/D/A/B`, `\r\n`.
9. **No async, no arenas, no unsafe beyond the two unavoidable libc
   calls** (termios sigaction restore; everything else via std).
10. **Toolchain**: rustc 1.85.1 available; `cargo-check`/`cargo-test`
    work. `cargo clippy`/`cargo fmt` subcommands are not installed on
    this machine — noted as environment gap; code style kept
    `rustfmt`-conventional by hand.

## 5. Migration Roadmap

Phase 0 — analysis (this document). ✔

Phase 1 — foundation
- [x] Chore: initialize Rust project structure (`Cargo.toml`, `src/`, `.gitignore` additions; keep C untouched)

Phase 2 — core leaf modules upward (dependency order: fileio → editor → terminal → input → output → main)
- [x] `feat: load files into the editor buffer in Rust` (open_file: existing/new, mode capture)
- [x] `feat: add positional insert and remove primitives to the file layer`
- [x] `feat: save files atomically from the Rust file layer`
- [x] `feat: implement the editor state and cursor motions in Rust`
- [x] `feat: decode key presses from stdin in Rust`
- [x] `feat: dispatch keys through the Rust editor`
- [x] `feat: switch the terminal between raw and saved modes in Rust`
- [x] `feat: render editor changes with escape codes in Rust`
- [x] `feat: wire the Rust editor main loop`

Phase 3 — parity
- [x] `fix: treat the erase byte 0x7f as backspace like the original` (found by the pty diff)
- [x] `test: port the remaining unit cases from the C suite`
- [x] `test: point the pty suite at the Rust binary` (via `OLDVIM_BIN`; committed with the fix)
- [x] Behavior comparison runs against the C binary: pty suite PASS (8/8) on debug and release;
      byte-level screen-output diffs MATCH for join-lines, up/down clamping, insert-in-middle,
      backspace-at-start, plain typing and EOF; SIGINT/SIGTERM/SIGHUP exit statuses identical
      (130/143/129, file untouched).

Phase 4 — documentation
- [x] `docs: describe building and running the Rust editor`
- [ ] (done) C sources removed from the working tree once the port was verified;
      the original is preserved at the `c-original` tag and throughout history.

## 6. Risk register

- mkstemp parity / kept-temp semantics: resolved, mkstemp is used directly.
- Raw-mode + signal restore is the only unsafe-touching area: kept in a
  single small module (`src/io/terminal.rs`).
- The `[EOF]` and non-interactive paths kept the same exit codes: verified.
- Locale/UTF-8: the editor stays byte-oriented; verified by the pty diffs.

## 7. Intentional implementation differences (Rust idioms, same behavior)

1. **No NUL sentinel.** The C buffer keeps `data[len] == '\0'`; only C
   string helpers needed it. The Rust `Vec<u8>` has none, and every
   consumer slices by `len` — save writes `data[..len]` exactly like the
   C `write_all(ef->data, ef->len)`.
2. **Allocation failure is not an error path.** C checked every malloc
   and printed `error: cannot allocate memory ...`. Rust aborts on
   allocation failure; there is nothing to recover into. The messages
   are dropped with this documented reason.
3. **`ACTION_ERROR` became `Result::Err`.** `handle_key` returns
   `Result<EditorAction, EditorError>`; the C `EditorAction::Error` +
   status plumbing is the same control flow expressed idiomatically.
4. **`editor_row_column`'s optional outputs collapsed** into one
   `(row, column)` tuple — the C NULL-pointer parameters were a calling
   convention, not behavior; both outputs were always computed at single
   scan cost in practice.
5. **`Key` values >0xff are gone.** The C constants above byte range
   existed to survive an `int`-to-`char` cast; the Rust enum removes the
   cast. `scan()` also handles byte 0x7f itself (some of that was
   discovered by the pty diff, see the fixed commit).
6. **Signal-data statics are atomics.** `saved_termios` lives behind
   `AtomicPtr` (leaked box, i.e. a static); `rawmode_enabled` is an
   `AtomicBool` — same lifecycle, race-free by construction.
7. **`cargo fmt`/`cargo clippy`** are not installed on the development
   machine (`rustc 1.85.1` from source tarball), so `cargo check` +
   `cargo test` are the verified gates; the remaining checks should run
   where the components are available.
