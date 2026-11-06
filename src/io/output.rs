// Screen output for a viewport: the editor reports what changed, this
// module owns the escape codes and the screen geometry. Only the
// visible rows of the buffer are ever written, and ordinary keys
// repaint only the region they disturbed.

use std::io::{self, Write};

use crate::editor::{line_start_index, offset_of_row, row_column, CursorMotion, Editor, EditorChange};
use crate::io::viewport::{Size, Viewport};

/* Move to the top-left cell. */
fn goto_origin<W: Write>(out: &mut W) -> io::Result<()> {
    out.write_all(b"\x1b[H")
}

/* Put the cursor at a 0-based screen row and column. */
fn goto_position<W: Write>(out: &mut W, row: usize, column: usize) -> io::Result<()> {
    write!(out, "\x1b[{};{}H", row + 1, column + 1)
}

/* Erase the current line from the cursor to its end. */
fn erase_line<W: Write>(out: &mut W) -> io::Result<()> {
    out.write_all(b"\x1b[K")
}

/* Erase from the cursor to the bottom of the screen. */
fn erase_below<W: Write>(out: &mut W) -> io::Result<()> {
    out.write_all(b"\x1b[J")
}

/* Write one logical line clipped to `width` bytes and return the offset
 * of the next line, or None when the buffer ends. */
fn write_clipped_line<W: Write>(
    out: &mut W,
    data: &[u8],
    start: usize,
    width: usize,
) -> io::Result<Option<usize>> {
    let len = data.len();
    let mut end = start;

    while end < len && data[end] != b'\n' && end - start < width {
        end += 1;
    }

    out.write_all(&data[start..end])?;

    /* Skip whatever did not fit until the end of the line. */
    let mut next = end;

    while next < len && data[next] != b'\n' {
        next += 1;
    }

    Ok(if next < len { Some(next + 1) } else { None })
}

/* Put the terminal cursor where the editor cursor is. The viewport has
 * already been scrolled to keep that row visible. */
fn position_cursor<W: Write>(
    out: &mut W,
    ed: &Editor,
    viewport: &Viewport,
) -> io::Result<()> {
    let (row, column) = row_column(&ed.file, ed.cursor);
    let screen_row = viewport.screen_row(row);
    let screen_column = column.min(viewport.size().content_width());

    goto_position(out, screen_row, screen_column)
}

/* Repaint every visible row and put the cursor back on screen. */
pub fn screen_draw(ed: &Editor, viewport: &Viewport) -> io::Result<()> {
    let mut out = io::stdout().lock();
    let size = viewport.size();
    let width = size.content_width();
    let data = ed.file.data();

    goto_origin(&mut out)?;

    let mut offset = offset_of_row(data, viewport.top_row());

    for screen_row in 0..size.rows {
        if screen_row > 0 {
            out.write_all(b"\r\n")?;
        }

        erase_line(&mut out)?;

        if let Some(start) = offset {
            offset = write_clipped_line(&mut out, data, start, width)?;
        }
    }

    position_cursor(&mut out, ed, viewport)?;
    out.flush()
}

/* Repaint from the row that contains `start` down to the bottom of the
 * viewport, then put the cursor back. */
fn redraw_from(ed: &Editor, viewport: &Viewport, start: usize) -> io::Result<()> {
    let mut out = io::stdout().lock();
    let size = viewport.size();
    let width = size.content_width();
    let data = ed.file.data();
    let start_row = row_column(&ed.file, start).0;
    let first_screen_row = viewport.screen_row(start_row);

    /* Move to the beginning of the affected row and erase below it. */
    goto_position(&mut out, first_screen_row, 0)?;
    erase_below(&mut out)?;

    let mut offset = Some(start);

    for screen_row in first_screen_row..size.rows {
        if screen_row > first_screen_row {
            out.write_all(b"\r\n")?;
        }

        if let Some(next) = offset {
            offset = write_clipped_line(&mut out, data, next, width)?;
        }
    }

    position_cursor(&mut out, ed, viewport)?;
    out.flush()
}

/* The cursor moved without changing the buffer. */
fn move_cursor(ed: &Editor, viewport: &Viewport) -> io::Result<()> {
    let mut out = io::stdout().lock();

    position_cursor(&mut out, ed, viewport)?;
    out.flush()
}

/* A byte was appended at the end of the buffer. Echo it in place while
 * it fits on screen; the newline and the clipped tail repaint the row. */
fn append_byte(ed: &Editor, viewport: &Viewport, byte: u8) -> io::Result<()> {
    let column = row_column(&ed.file, ed.cursor).1;

    if byte == b'\n' || column > viewport.size().content_width() {
        return redraw_from(
            ed,
            viewport,
            line_start_index(ed.file.data(), ed.cursor),
        );
    }

    let mut out = io::stdout().lock();

    out.write_all(&[byte]).and_then(|()| out.flush())
}

/* Repaint whatever the key dispatch reported for this editor state. */
pub fn apply_change(
    ed: &Editor,
    viewport: &mut Viewport,
    size: Size,
    change: EditorChange,
) -> io::Result<()> {
    let cursor_row = row_column(&ed.file, ed.cursor).0;

    /* A new geometry invalidates every row, so repaint the whole
     * window and make sure the cursor is still in it. */
    if viewport.resize(size) {
        viewport.ensure_visible(cursor_row);
        return screen_draw(ed, viewport);
    }

    /* Scrolling invalidates every row as well. */
    if viewport.ensure_visible(cursor_row) {
        return screen_draw(ed, viewport);
    }

    match change {
        EditorChange::None => Ok(()),
        EditorChange::Cursor(CursorMotion::None) => Ok(()),
        EditorChange::Cursor(_) => move_cursor(ed, viewport),
        EditorChange::Line { start, .. } => redraw_from(ed, viewport, start),
        EditorChange::Append { byte } => append_byte(ed, viewport, byte),
    }
}
