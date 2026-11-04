// The editing state that lives above the file: an open buffer, the
// cursor as a byte offset, and the four motion commands. This module
// never touches the terminal; it reports motions and changes that the
// caller renders.

use crate::fileio::{EditorFile, OpenError, PrimitiveError, SaveError};
use std::path::Path;

/// An open buffer plus the editing state that lives above the file.
#[derive(Debug)]
pub struct Editor {
    pub file: EditorFile,
    pub cursor: usize,
    pub dirty: bool,
}

/// Why handle_key gave up. The session ends with status 1 and the
/// message text of the underlying file layer.
#[derive(Debug)]
pub enum EditorError {
    Open(OpenError),
    Save(SaveError),
    Buffer(PrimitiveError),
}

impl std::fmt::Display for EditorError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            EditorError::Open(err) => write!(f, "{err}"),
            EditorError::Save(err) => write!(f, "{err}"),
            EditorError::Buffer(err) => write!(f, "{err}"),
        }
    }
}

impl std::error::Error for EditorError {}

/* What a movement did, so the caller can render it without knowing the
 * buffer layout. NONE means the cursor did not move. */
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CursorMotion {
    None,
    Left,
    Right,
    PreviousLine,
    NextLine,
    Up,
    Down,
}

/* What the caller has to do after a key was handled. */
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EditorAction {
    Continue,
    Quit,
    Error,
}

/* The part of the buffer that has to be repainted after a change. */
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum EditorChange {
    #[default]
    None,
    Cursor(CursorMotion),
    /* The line beginning at start was edited. */
    Line { start: usize, removed_newline: bool },
    /* A byte was echoed past the end of the buffer. */
    Append { byte: u8 },
}

impl Editor {
    /* Load the path and start at the end of the file. */
    pub fn open(path: &Path) -> Result<Editor, OpenError> {
        let file = EditorFile::open(path)?;

        Ok(Editor {
            cursor: file.len(),
            dirty: false,
            file,
        })
    }
}

/* Index of the first byte of the line that contains pos. */
pub fn line_start_index(data: &[u8], pos: usize) -> usize {
    let mut start = pos;

    while start > 0 && data[start - 1] != b'\n' {
        start -= 1;
    }

    start
}

/* Index of the newline that ends the line containing pos, or len. */
pub fn line_end_index(data: &[u8], len: usize, pos: usize) -> usize {
    let mut end = pos;

    while end < len && data[end] != b'\n' {
        end += 1;
    }

    end
}

/* 0-based row and byte column of pos. */
pub fn row_column(ef: &EditorFile, pos: usize) -> (usize, usize) {
    let mut row = 0;

    for &byte in &ef.data()[..pos] {
        if byte == b'\n' {
            row += 1;
        }
    }

    (row, pos - line_start_index(ef.data(), pos))
}

/* Move one byte to the left, wrapping at line boundaries. */
pub fn move_left(ed: &mut Editor) -> CursorMotion {
    if ed.cursor == 0 {
        return CursorMotion::None;
    }

    if ed.file.data()[ed.cursor - 1] == b'\n' {
        /* Jump to the end of the previous line. */
        ed.cursor -= 1;
        return CursorMotion::PreviousLine;
    }

    ed.cursor -= 1;
    CursorMotion::Left
}

/* Move one byte to the right, wrapping at line boundaries. */
pub fn move_right(ed: &mut Editor) -> CursorMotion {
    if ed.cursor == ed.file.len() {
        return CursorMotion::None;
    }

    if ed.file.data()[ed.cursor] == b'\n' {
        /* Jump to the beginning of the next line. */
        ed.cursor += 1;
        return CursorMotion::NextLine;
    }

    ed.cursor += 1;
    CursorMotion::Right
}

/* Keep the column when moving up, clamped to the previous line. */
pub fn move_up(ed: &mut Editor) -> CursorMotion {
    let data = ed.file.data();
    let start = line_start_index(data, ed.cursor);

    if start == 0 {
        return CursorMotion::None;
    }

    /* The newline that ends the previous line. */
    let prev_end = start - 1;
    let prev_start = line_start_index(data, prev_end);
    let prev_len = prev_end - prev_start;
    let column = ed.cursor - start;
    let new_column = column.min(prev_len);

    ed.cursor = prev_start + new_column;
    CursorMotion::Up
}

/* Keep the column when moving down, clamped to the next line. */
pub fn move_down(ed: &mut Editor) -> CursorMotion {
    let data = ed.file.data();
    let start = line_start_index(data, ed.cursor);
    let end = line_end_index(data, ed.file.len(), start);

    if end == ed.file.len() {
        /* There is no line below the last one. */
        return CursorMotion::None;
    }

    let next_start = end + 1;
    let next_end = line_end_index(data, ed.file.len(), next_start);
    let next_len = next_end - next_start;
    let column = ed.cursor - start;
    let new_column = column.min(next_len);

    ed.cursor = next_start + new_column;
    CursorMotion::Down
}

#[cfg(test)]
mod tests {
    use super::*;

    /* Editor backed by a private copy of text, like make_editor() in
     * the C suite. */
    fn make_editor(text: &[u8]) -> Editor {
        Editor {
            file: EditorFile::from_parts_for_tests("unit-test.tmp", text),
            cursor: text.len(),
            dirty: false,
        }
    }

    /* ---------------------------------------------------------------- */
    /* line helpers                                                     */
    /* ---------------------------------------------------------------- */

    #[test]
    fn line_start_index_is_the_beginning_of_the_line() {
        let data = b"abc\ndef\n";

        assert_eq!(line_start_index(data, 0), 0);
        assert_eq!(line_start_index(data, 2), 0);
        assert_eq!(line_start_index(data, 3), 0); /* on the newline */
        assert_eq!(line_start_index(data, 4), 4); /* start of the second line */
        assert_eq!(line_start_index(data, 7), 4);
        assert_eq!(line_start_index(data, 8), 8); /* EOF after a newline */
        assert_eq!(line_start_index(b"", 0), 0);
    }

    #[test]
    fn line_end_index_hits_the_newline_or_len() {
        let data = b"abc\ndef\n";
        let no_newline = b"abc\ndef";

        assert_eq!(line_end_index(data, 8, 0), 3);
        assert_eq!(line_end_index(data, 8, 4), 7);
        assert_eq!(line_end_index(data, 8, 8), 8);
        assert_eq!(line_end_index(no_newline, 7, 0), 3);
        assert_eq!(line_end_index(no_newline, 7, 4), 7);
        assert_eq!(line_end_index(b"", 0, 0), 0);
    }

    #[test]
    fn row_column_counts_rows_and_columns() {
        let ef = EditorFile::from_parts_for_tests("x", b"abc\ndef\n");

        assert_eq!(row_column(&ef, 0), (0, 0));
        assert_eq!(row_column(&ef, 6), (1, 2));
        assert_eq!(row_column(&ef, 8), (2, 0));
    }

    /* ---------------------------------------------------------------- */
    /* movement                                                         */
    /* ---------------------------------------------------------------- */

    #[test]
    fn move_left_stops_at_the_start() {
        let mut ed = make_editor(b"ab\ncd");

        ed.cursor = 0;
        assert_eq!(move_left(&mut ed), CursorMotion::None);
        assert_eq!(ed.cursor, 0);

        ed.cursor = 5;
        assert_eq!(move_left(&mut ed), CursorMotion::Left);
        assert_eq!(ed.cursor, 4);

        ed.cursor = 3; /* start of the second line */
        assert_eq!(move_left(&mut ed), CursorMotion::PreviousLine);
        assert_eq!(ed.cursor, 2); /* end of the first line */
    }

    #[test]
    fn move_right_wraps_onto_the_next_line() {
        let mut ed = make_editor(b"ab\ncd");

        ed.cursor = 5;
        assert_eq!(move_right(&mut ed), CursorMotion::None);
        assert_eq!(ed.cursor, 5);

        ed.cursor = 0;
        assert_eq!(move_right(&mut ed), CursorMotion::Right);
        assert_eq!(ed.cursor, 1);

        ed.cursor = 2; /* on the newline */
        assert_eq!(move_right(&mut ed), CursorMotion::NextLine);
        assert_eq!(ed.cursor, 3);
    }

    #[test]
    fn move_up_clamps_the_column() {
        let mut ed = make_editor(b"abcde\nxy\n");

        ed.cursor = 9; /* empty last line */
        assert_eq!(move_up(&mut ed), CursorMotion::Up);
        assert_eq!(ed.cursor, 6); /* start of "xy" */

        ed.cursor = 8; /* end of "xy" */
        assert_eq!(move_up(&mut ed), CursorMotion::Up);
        assert_eq!(ed.cursor, 2); /* clamped to length of "abcde" */

        ed.cursor = 0;
        assert_eq!(move_up(&mut ed), CursorMotion::None);
        assert_eq!(ed.cursor, 0);
    }

    #[test]
    fn move_down_clamps_the_column() {
        let mut ed = make_editor(b"abcde\nxy\n");

        ed.cursor = 0;
        assert_eq!(move_down(&mut ed), CursorMotion::Down);
        assert_eq!(ed.cursor, 6);

        ed.cursor = 5; /* end of "abcde" */
        assert_eq!(move_down(&mut ed), CursorMotion::Down);
        assert_eq!(ed.cursor, 8); /* clamped to "xy" */

        ed.cursor = 9; /* empty last line, nothing below */
        assert_eq!(move_down(&mut ed), CursorMotion::None);
        assert_eq!(ed.cursor, 9);
    }
}
