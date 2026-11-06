// The editing state that lives above the file: an open buffer, the
// cursor as a byte offset, and the four motion commands. This module
// never touches the terminal; it reports motions and changes that the
// caller renders.

use crate::fileio::{EditorFile, OpenError, PrimitiveError, SaveError};
use crate::io::input::Key;
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
    Save(SaveError),
    Buffer(PrimitiveError),
}

impl std::fmt::Display for EditorError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
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

/* What the caller has to do after a key was handled. Errors are the
 * Err side of handle_key and end the session with status 1. */
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EditorAction {
    Continue,
    Quit,
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

/* First byte of the given 0-based row, or None past the last row. A
 * trailing newline opens one more empty row at the end of the buffer. */
pub fn offset_of_row(data: &[u8], row: usize) -> Option<usize> {
    if row == 0 {
        return Some(0);
    }

    let mut current = 0;

    for (index, &byte) in data.iter().enumerate() {
        if byte == b'\n' {
            current += 1;

            if current == row {
                return Some(index + 1);
            }
        }
    }

    None
}

/* 0-based row and byte column of pos. */
pub fn row_column(ef: &EditorFile, pos: usize) -> (usize, usize) {    let mut row = 0;

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

/* Apply one decoded key to the editor and describe what to repaint.
 * Errors end the session with status 1, carrying the message text of
 * the file layer. */
pub fn handle_key(
    ed: &mut Editor,
    key: Key,
    change: &mut EditorChange,
) -> Result<EditorAction, EditorError> {
    *change = EditorChange::None;

    if key == Key::CtrlD {
        return Ok(EditorAction::Quit);
    }

    if key == Key::CtrlS {
        ed.file.save().map_err(EditorError::Save)?;
        ed.dirty = false;
        return Ok(EditorAction::Continue);
    }

    let motion = match key {
        Key::Left => move_left(ed),
        Key::Right => move_right(ed),
        Key::Up => move_up(ed),
        Key::Down => move_down(ed),
        Key::Backspace => return handle_backspace_key(ed, change),
        /* Ctrl+D and Ctrl+S were handled before this match. */
        Key::CtrlD | Key::CtrlS => unreachable!("keys handled above"),
        Key::Byte(byte) => return handle_insert_key(ed, byte, change),
    };

    *change = EditorChange::Cursor(motion);
    Ok(EditorAction::Continue)
}

/* Backspace removes the previous byte and may join two lines. */
fn handle_backspace_key(
    ed: &mut Editor,
    change: &mut EditorChange,
) -> Result<EditorAction, EditorError> {
    if ed.cursor == 0 {
        return Ok(EditorAction::Continue);
    }

    let pos = ed.cursor - 1;
    let removed_newline = ed.file.data()[pos] == b'\n';
    let start = line_start_index(ed.file.data(), pos);

    ed.file.remove(pos).map_err(EditorError::Buffer)?;

    ed.cursor = pos;
    ed.dirty = true;

    *change = EditorChange::Line {
        start,
        removed_newline,
    };

    Ok(EditorAction::Continue)
}

/* Typed bytes insert one byte at the cursor. */
fn handle_insert_key(
    ed: &mut Editor,
    byte: u8,
    change: &mut EditorChange,
) -> Result<EditorAction, EditorError> {
    let at_end = ed.cursor == ed.file.len();
    let start = line_start_index(ed.file.data(), ed.cursor);

    ed.file.insert(ed.cursor, &[byte]).map_err(EditorError::Buffer)?;

    ed.cursor += 1;
    ed.dirty = true;

    *change = if at_end {
        EditorChange::Append { byte }
    } else {
        EditorChange::Line { start, removed_newline: false }
    };

    Ok(EditorAction::Continue)
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

    #[test]
    fn offset_of_row_finds_line_starts() {
        let data = b"abc\ndef\n";

        assert_eq!(offset_of_row(data, 0), Some(0));
        assert_eq!(offset_of_row(data, 1), Some(4));
        assert_eq!(offset_of_row(data, 2), Some(8)); /* empty row after the newline */
        assert_eq!(offset_of_row(data, 3), None);

        assert_eq!(offset_of_row(b"", 0), Some(0));
        assert_eq!(offset_of_row(b"", 1), None);
        assert_eq!(offset_of_row(b"abc", 1), None);
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

    /* ---------------------------------------------------------------- */
    /* key handling                                                     */
    /* ---------------------------------------------------------------- */

    fn press(ed: &mut Editor, key: Key) -> (EditorAction, EditorChange) {
        let mut change = EditorChange::None;
        let action = handle_key(ed, key, &mut change).unwrap();
        (action, change)
    }

    #[test]
    fn typed_keys_append_at_the_end_of_the_buffer() {
        let mut ed = make_editor(b"abc");

        let (action, change) = press(&mut ed, Key::Byte(b'X'));

        assert_eq!(action, EditorAction::Continue);
        assert_eq!(change, EditorChange::Append { byte: b'X' });
        assert_eq!(ed.file.data(), b"abcX");
        assert_eq!(ed.cursor, 4);
        assert!(ed.dirty);
    }

    #[test]
    fn typed_keys_in_the_middle_repaint_the_line() {
        let mut ed = make_editor(b"abc");
        ed.cursor = 1;

        let (action, change) = press(&mut ed, Key::Byte(b'X'));

        assert_eq!(action, EditorAction::Continue);
        assert_eq!(change, EditorChange::Line { start: 0, removed_newline: false });
        assert_eq!(ed.file.data(), b"aXbc");
        assert_eq!(ed.cursor, 2);
    }

    #[test]
    fn backspace_joins_lines_at_a_newline() {
        let mut ed = make_editor(b"ab\ncd");
        let mut change = EditorChange::None;

        ed.cursor = 0;
        handle_key(&mut ed, Key::Backspace, &mut change).unwrap();
        assert_eq!(change, EditorChange::None);
        assert_eq!(ed.file.data(), b"ab\ncd");

        /* Joining the two lines. */
        ed.cursor = 3;
        handle_key(&mut ed, Key::Backspace, &mut change).unwrap();

        assert_eq!(
            change,
            EditorChange::Line { start: 0, removed_newline: true }
        );
        assert_eq!(ed.cursor, 2);
        assert_eq!(ed.file.data(), b"abcd");
        assert!(ed.dirty);
    }

    #[test]
    fn arrow_keys_report_a_cursor_change() {
        let mut ed = make_editor(b"ab\ncd");
        ed.cursor = 3;

        let (action, change) = press(&mut ed, Key::Left);

        assert_eq!(action, EditorAction::Continue);
        assert_eq!(change, EditorChange::Cursor(CursorMotion::PreviousLine));
        assert_eq!(ed.cursor, 2);
        assert!(!ed.dirty, "motions do not dirty the buffer");
    }

    #[test]
    fn ctrl_d_quits_without_touching_the_buffer() {
        let mut ed = make_editor(b"");

        let (action, change) = press(&mut ed, Key::CtrlD);

        assert_eq!(action, EditorAction::Quit);
        assert_eq!(change, EditorChange::None);
        assert_eq!(ed.file.len(), 0);
    }

    #[test]
    fn ctrl_s_saves_and_clears_the_dirty_flag() {
        let dir = crate::fileio::tests::make_temp_dir("ctrls");
        let path = dir.join("file.txt");

        let mut ed = Editor::open(&path).unwrap();

        press(&mut ed, Key::Byte(b'!'));
        let (action, change) = press(&mut ed, Key::CtrlS);

        assert_eq!(action, EditorAction::Continue);
        assert_eq!(change, EditorChange::None);
        assert!(!ed.dirty, "the save cleared the dirty flag");

        let mut content = Vec::new();
        use std::io::Read;
        std::fs::File::open(&path)
            .unwrap()
            .read_to_end(&mut content)
            .unwrap();
        assert_eq!(content, b"!");

        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    #[test]
    fn ctrl_s_reports_a_failing_save() {
        let dir = crate::fileio::tests::make_temp_dir("ctrls-broken");

        /* Point the path into a directory that will vanish. */
        let mut ed = Editor::open(&dir.join("gone/file.txt")).unwrap();
        press(&mut ed, Key::Byte(b'x'));

        std::fs::remove_dir_all(&dir).unwrap();

        let mut change = EditorChange::None;
        let err = handle_key(&mut ed, Key::CtrlS, &mut change).unwrap_err();

        assert!(matches!(err, EditorError::Save(_)));
        assert!(
            err.to_string().starts_with("error: cannot create"),
            "unexpected message: {err}"
        );
    }
}
