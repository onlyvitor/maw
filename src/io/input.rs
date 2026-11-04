// One decoded key press per call, from stdin or another reader.

use std::io;
use std::io::Read;

/// The bytes and control keys the editor understands. In the original,
/// keys above 0xff could not collide with typed bytes; here the enum
/// makes the distinction itself.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Key {
    /// An insertable byte as typed by the user. An unmatched escape
    /// sequence leaves the escape byte in place, like the original.
    Byte(u8),
    /// Ctrl+H is remapped here; the backspace key itself sends 0x7f.
    Backspace,
    /// Ctrl+S, allowed to reach the editor because the terminal layer
    /// turns software flow control off.
    CtrlS,
    /// Ctrl+D quits.
    CtrlD,
    Up,
    Down,
    Left,
    Right,
}

/// Escape byte that introduces a terminal key sequence.
const ESC: u8 = 0x1b;
const CTRL_H: u8 = 0x08;
const KEY_BACKSPACE: u8 = 0x7f;
const CTRL_S: u8 = 0x13;
const CTRL_D: u8 = 0x04;

/* Decode one key press. None means clean end of input, Err a read
 * failure. */
pub fn scan() -> io::Result<Option<Key>> {
    scan_from(&mut io::stdin().lock())
}

pub fn scan_from<R: Read>(input: &mut R) -> io::Result<Option<Key>> {
    let byte = match read_byte(input)? {
        None => return Ok(None),
        Some(byte) => byte,
    };

    match byte {
        /* Like the original: backspace is the erase key or Ctrl+H; the
         * editors sees one key for both. */
        CTRL_H | KEY_BACKSPACE => Ok(Some(Key::Backspace)),
        CTRL_S => Ok(Some(Key::CtrlS)),
        CTRL_D => Ok(Some(Key::CtrlD)),
        ESC => decode_escape(input),
        byte => Ok(Some(Key::Byte(byte))),
    }
}

/* CSI (ESC [) and SS3 (ESC O) both introduce cursor key sequences. */
fn decode_escape<R: Read>(input: &mut R) -> io::Result<Option<Key>> {
    let segment1 = match read_byte(input)? {
        None => return Ok(None),
        Some(byte) => byte,
    };

    if segment1 == b'[' || segment1 == b'O' {
        return match read_byte(input)? {
            None => Ok(None),
            Some(b'A') => Ok(Some(Key::Up)),
            Some(b'B') => Ok(Some(Key::Down)),
            Some(b'C') => Ok(Some(Key::Right)),
            Some(b'D') => Ok(Some(Key::Left)),
            /* Unmatched sequences leave the escape byte itself in the
             * buffer, after dropping the rest of the segment. */
            Some(_) => Ok(Some(Key::Byte(ESC))),
        };
    }

    Ok(Some(Key::Byte(ESC)))
}

fn read_byte<R: Read>(input: &mut R) -> io::Result<Option<u8>> {
    let mut buffer = [0u8; 1];

    let read = input.read(&mut buffer)?;

    if read == 0 {
        Ok(None)
    } else {
        Ok(Some(buffer[0]))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scan(data: &[u8]) -> Option<Key> {
        let mut cursor = io::Cursor::new(data);
        scan_from(&mut cursor).unwrap()
    }

    #[test]
    fn typed_bytes_translate_one_to_one() {
        assert_eq!(scan(b"z"), Some(Key::Byte(b'z')));
        assert_eq!(scan(b"9"), Some(Key::Byte(b'9')));
    }

    #[test]
    fn control_keys_reach_the_editor() {
        assert_eq!(scan(b"\x04"), Some(Key::CtrlD));
        assert_eq!(scan(b"\x13"), Some(Key::CtrlS));
        /* Both erase inputs collapse onto one key. */
        assert_eq!(scan(b"\x08"), Some(Key::Backspace));
        assert_eq!(scan(b"\x7f"), Some(Key::Backspace));
    }

    #[test]
    fn cursor_sequences_decode_in_both_forms() {
        assert_eq!(scan(b"\x1b[A"), Some(Key::Up));
        assert_eq!(scan(b"\x1b[B"), Some(Key::Down));
        assert_eq!(scan(b"\x1b[C"), Some(Key::Right));
        assert_eq!(scan(b"\x1b[D"), Some(Key::Left));
        assert_eq!(scan(b"\x1bOA"), Some(Key::Up));
        assert_eq!(scan(b"\x1bOD"), Some(Key::Left));
    }

    #[test]
    fn unmatched_sequences_leave_the_escape_byte() {
        /* ESC alone falls through to EOF, like the original. */
        assert_eq!(scan(b"\x1b"), None);

        /* ESC followed by an unknown segment: the segment is dropped. */
        assert_eq!(scan(b"\x1bZhello"), Some(Key::Byte(0x1b)));
        assert_eq!(scan(b"\x1b[5~"), Some(Key::Byte(0x1b)));
    }

    #[test]
    fn cut_off_sequences_report_a_clean_eof() {
        assert_eq!(scan(b""), None);
        assert_eq!(scan(&[0x1b, b'[']), None); /* sequence cut off at EOF */
    }

    struct Broken;

    impl Read for Broken {
        fn read(&mut self, _: &mut [u8]) -> io::Result<usize> {
            Err(io::Error::other("broken"))
        }
    }

    #[test]
    fn read_failures_surface_as_errors() {
        let mut broken = Broken;
        assert!(scan_from(&mut broken).is_err());
    }
}
