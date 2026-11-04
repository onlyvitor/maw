# Oldvim

Oldvim is a minimal terminal editor: it prints the file you opened, and
it edits one byte at a time in raw mode until you quit or save. The
repository carries the original C implementation plus a faithful Rust
reimplementation that reproduces it (see `docs/MIGRATION.md`).

## Building

C implementation:

```sh
make             # builds the ./oldvim binary
make run         # runs it on text.rst
```

Rust implementation:

```sh
cargo build          # dev build
cargo build --release
cargo test           # unit tests, 33 checks
cargo run -- text.rst
```

The Rust binary is `target/<profile>/oldvim`; it behaves identically to
the C binary on every currently testable path.

## Controls

| Key | Action |
| --- | --- |
| printable bytes | insert at cursor |
| Backspace / Ctrl+H | delete the previous byte |
| arrows | move the cursor between bytes and lines |
| Ctrl+S | save the file and keep editing |
| Ctrl+D | quit (pending edits are saved) |

Saving is atomic: the content goes to `<file>.tmp.XXXXXX`, is synced to
disk, and is renamed over the original. If a save step fails, the
temporary file is kept and its name is printed.

Piping stdin into the editor works without a tty as well:

```sh
printf 'ZZ' | oldvim file.txt > /dev/null
```

## Layout

```
main.c / src/main.rs    wiring: open, raw mode, draw, key loop, save
fileio/ , src/fileio.rs the buffer, loading and the atomic save
editor/, src/editor.rs  cursor motions and key dispatch, no I/O
io/input                key decoding
io/output               escape-code rendering
io/terminal             raw mode and signal restoration
tests/unit_test.c       C unit suite
tests/test_editor.py    pty end-to-end suite (build-agnostic via OLDVIM_BIN)
src/*.rs                Rust reimplementation modules
```

## Tests

```sh
make test                                            # C suite
OLDVIM_BIN=target/debug/oldvim python3 tests/test_editor.py   # Rust binary
```

`OLDVIM_BIN` drives the same pty suite at either implementation, which
is how the Rust port was verified side by side against the C editor.
