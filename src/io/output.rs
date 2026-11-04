// Screen output: minimal primitives plus change-driven rendering. The
// editor reports what to repaint; this module produces the escape
// codes. Every function reports write failures, never eats them — the
// caller owns the printed messages.

use std::io::{self, Write};

use crate::editor::{line_start_index, row_column, CursorMotion, Editor, EditorChange};
use crate::fileio::EditorFile;

/* Move the cursor without touching the buffer. */
fn goto_column<W: Write>(out: &mut W, column: usize) -> io::Result<()> {
    if column == 0 {
        return Ok(());
    }

    write!(out, "\x1b[{}C", column)
}

fn move_left<W: Write>(out: &mut W, columns: usize) -> io::Result<()> {
    if columns == 0 {
        return Ok(());
    }

    write!(out, "\x1b[{}D", columns)
}

fn move_right<W: Write>(out: &mut W, columns: usize) -> io::Result<()> {
    goto_column(out, columns)
}

fn move_up<W: Write>(out: &mut W, rows: usize) -> io::Result<()> {
    if rows == 0 {
        return Ok(());
    }

    write!(out, "\x1b[{}A", rows)
}

fn move_down<W: Write>(out: &mut W, rows: usize) -> io::Result<()> {
    if rows == 0 {
        return Ok(());
    }

    write!(out, "\x1b[{}B", rows)
}

fn carriage_return<W: Write>(out: &mut W) -> io::Result<()> {
    out.write_all(b"\r")
}

/* Erase everything below the cursor. */
fn erase_below<W: Write>(out: &mut W) -> io::Result<()> {
    out.write_all(b"\x1b[J")
}

/* Print the whole buffer at startup and flush. */
pub fn screen_write(data: &[u8]) -> io::Result<()> {
    let mut out = io::stdout().lock();

    out.write_all(data).and_then(|()| out.flush())
}

/* Draw the cursor motion that the editor reported. */
pub fn apply_motion(ef: &EditorFile, cursor: usize, motion: CursorMotion) -> io::Result<()> {
    let mut out = io::stdout().lock();

    match motion {
        CursorMotion::None => Ok(()),
        CursorMotion::Left => move_left(&mut out, 1).and_then(|()| out.flush()),
        CursorMotion::Right => move_right(&mut out, 1).and_then(|()| out.flush()),
        CursorMotion::NextLine => out.write_all(b"\r\n").and_then(|()| out.flush()),
        /* The line is one above; the landing column needs the buffer. */
        CursorMotion::PreviousLine
        | CursorMotion::Up
        | CursorMotion::Down => carriage_return_or_column(&mut out, ef, cursor, motion),
    }
}

fn carriage_return_or_column<W: Write>(
    out: &mut W,
    ef: &EditorFile,
    cursor: usize,
    motion: CursorMotion,
) -> io::Result<()> {
    match motion {
        CursorMotion::Up | CursorMotion::PreviousLine => move_up(out, 1)?,
        CursorMotion::Down => move_down(out, 1)?,
        _ => {}
    }

    carriage_return(out)?;
    goto_column(out, row_column(ef, cursor).1)?;
    out.flush()
}

/* Redraw from the start of a line and put the cursor back at cursor. */
pub fn redraw_from(ef: &EditorFile, start: usize, cursor: usize) -> io::Result<()> {
    let data = ef.data();
    let mut out = io::stdout().lock();

    /* Move to the beginning of the line and erase everything below it. */
    carriage_return(&mut out).and_then(|()| erase_below(&mut out))?;

    out.write_all(&data[start..])?;

    /* Count how many lines were printed after the cursor. */
    let mut rows = 0;

    for &byte in &data[cursor..] {
        if byte == b'\n' {
            rows += 1;
        }
    }

    move_up(&mut out, rows).and_then(|()| carriage_return(&mut out))?;

    /* Move right to the column where the cursor lives. */
    let column = cursor - line_start_index(data, cursor);

    goto_column(&mut out, column).and_then(|()| out.flush())
}

/* Repaint whatever the key dispatch reported for this editor state. */
pub fn apply_change(ed: &Editor, change: EditorChange) -> io::Result<()> {
    match change {
        EditorChange::None => Ok(()),
        EditorChange::Cursor(motion) => apply_motion(&ed.file, ed.cursor, motion),
        EditorChange::Line { start, removed_newline } => {
            let mut out = io::stdout().lock();

            if removed_newline {
                /* The cursor moved up a row with the joined lines. */
                move_up(&mut out, 1)?;
            }

            drop(out);
            redraw_from(&ed.file, start, ed.cursor)
        }
        EditorChange::Append { byte } => {
            let mut out = io::stdout().lock();

            out.write_all(&[byte]).and_then(|()| out.flush())
        }
    }
}
