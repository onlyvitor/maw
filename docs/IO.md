# Input, rendering and save flow

## Input

`src/input/input.c` reads one byte at a time with `read(2)`:

- `Ctrl+H` is remapped to `KEY_BACKSPACE`;
- `ESC [` / `ESC O` followed by `A`, `B`, `C` or `D` becomes an arrow;
- a lone `ESC` followed by a normal key pushes that key back for the
  next call and reports `KEY_ESC`;
- `EINTR` from a pending `SIGWINCH`/`SIGCONT` returns `KEY_RESIZE`;
- end of input returns `INPUT_EOF` and a real read error returns
  `INPUT_ERROR`.

Key constants shared by the editor and the input decoder live in
`src/key.h`. Values above `0xff` are not bytes and cannot collide with
inserted text.

## Editing model

`Editor` keeps a byte cursor and a dirty flag over `EditorFile`. Motions
and backspace move by UTF-8 character boundaries (`src/text/`), not by
raw byte. Vertical motion clamps the display column, expanding tabs at
8-column stops. A backspace removes the whole code point; a newline is
still a single byte and joins lines as before.

## Rendering

`src/render/render.c` chooses the smallest repaint that covers a change:

- a scroll or resize repaints the whole viewport;
- cursor motion only refreshes the status line and repositions the
  cursor;
- inserting inside a line or deleting repaints from the line start;
- appending a printable ASCII byte echoes it in place; control bytes,
  UTF-8 bytes and over-width appends repaint the line.

`escape_write_text` is the only path that writes file content. It
expands tabs, converts control bytes to caret notation (`^[`, `^M`,
`^?`), replaces invalid UTF-8 with `?` and clips to display cells. This
keeps file contents from injecting escape sequences into the terminal.
The status line uses the same function for the file name and cursor
position; the cursor position is the display cell, not the byte offset.

The status line occupies the last screen row in reverse video and shows
the file name, a `*` while dirty, the 1-based row and display column,
and the `^S:save  ^Q:quit` hint.

## Save flow

`fileio_save` writes to `<file>.tmp.XXXXXX` in the same directory,
applies the target permissions, calls `fsync`, renames over the target
and `fsync`s the parent directory. If a step fails, the temporary file
is kept and its path is printed.

The final save only runs when the editor is dirty, so quitting without
edits does not rewrite the file. New files take `0666 & ~umask`;
existing files keep their `0777` permission bits (setuid/setgid/sticky
bits are not copied). The rename does not preserve ownership, ACLs or
extended attributes; that is the trade-off for the atomic replace.
