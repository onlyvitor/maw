// Screen output for a viewport: the editor reports what changed, this
// module owns the escape codes and the screen geometry. Only the
// visible rows of the buffer are ever written.

use std::io::{self, Write};

use crate::editor::{offset_of_row, row_column, Editor, EditorChange};
use crate::io::viewport::Viewport;

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

    /* The cursor is always kept inside the painted window: editing
     * paths call ensure_visible before drawing. Clamp anyway so an
     * off-screen cursor cannot underflow the row arithmetic. */
    let (cursor_row, cursor_column) = row_column(&ed.file, ed.cursor);
    let screen_row = cursor_row
        .saturating_sub(viewport.top_row())
        .min(size.rows.saturating_sub(1));
    let screen_column = cursor_column.min(width);

    goto_position(&mut out, screen_row, screen_column)?;
    out.flush()
}

/* Scroll to keep the cursor on screen, then repaint whatever the key
 * dispatch reported for this editor state. */
pub fn apply_change(
    ed: &Editor,
    viewport: &mut Viewport,
    _change: EditorChange,
) -> io::Result<()> {
    viewport.ensure_visible(row_column(&ed.file, ed.cursor).0);
    screen_draw(ed, viewport)
}
