# Oldvim

Oldvim is a minimal terminal editor: it prints the file you opened, and
it edits one byte at a time in raw mode until you quit or save.

The current implementation is pure Rust (`src/`). The original C
implementation that guided the port is preserved in the repository
history and tagged as `c-original`.

## Building

```sh
cargo build          # dev build
cargo build --release
cargo test           # unit tests, 33 checks
cargo run -- text.rst
```

The binary is `target/<profile>/oldvim`.

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
src/main.rs        wiring: open, raw mode, draw, key loop, save
src/fileio.rs      the buffer, loading and the atomic save
src/editor.rs      cursor motions and key dispatch, no I/O
src/io/input.rs    key decoding
src/io/output.rs   escape-code rendering
src/io/terminal.rs raw mode and signal restoration
tests/test_editor.py  pty end-to-end suite
docs/MIGRATION.md  architecture, behavior map and migration history
```

## Tests

```sh
cargo test                  # unit tests
python3 tests/test_editor.py  # end-to-end through a pty
```

The pty suite drives `target/debug/oldvim`; `OLDVIM_BIN` points it at
any other build.
