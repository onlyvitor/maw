// Filled in by the file-layer migration commits (see docs/MIGRATION.md).

use std::fs::File;
use std::io;
use std::io::Read;
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};

/// Keep only the permission bits of the original file.
const PERMISSION_MASK: u32 = 0o7777;
const DEFAULT_FILE_MODE: u32 = 0o644;

/* The whole file content, its path, and what save_file needs to restore
 * the file the way it was found. The content is plain bytes: the editor
 * is byte-oriented, never UTF-8 aware. */
#[derive(Debug)]
pub struct EditorFile {
    path: PathBuf,
    data: Vec<u8>,
    mode: u32,
    exist: bool,
}

/// Why open_file failed. Each variant carries the message text of the
/// original implementation.
#[derive(Debug)]
pub enum OpenError {
    /// "error: cannot stat %s: %s\n"
    Stat { path: PathBuf, err: io::Error },
    /// "error: cannot open %s: %s\n"
    Open { path: PathBuf, err: io::Error },
    /// "error: cannot read the file\n"
    Read { err: io::Error },
}

impl OpenError {
    /// The path the failed operation was about to touch.
    fn path(&self) -> &Path {
        match self {
            OpenError::Stat { path, .. } | OpenError::Open { path, .. } => path,
            OpenError::Read { .. } => Path::new(""),
        }
    }
}

impl std::fmt::Display for OpenError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            OpenError::Stat { path, err } => {
                write!(f, "error: cannot stat {}: {}", path.display(), err)
            }
            OpenError::Open { path, err } => {
                write!(f, "error: cannot open {}: {}", path.display(), err)
            }
            OpenError::Read { .. } => write!(f, "error: cannot read the file"),
        }
    }
}

/* Read the whole file into the buffer. */
fn load_content(filename: &Path) -> Result<Vec<u8>, OpenError> {
    let mut content = Vec::new();

    let mut file = File::open(filename)
        .map_err(|err| OpenError::Open { path: filename.to_owned(), err })?;

    file.read_to_end(&mut content)
        .map_err(|err| OpenError::Read { err })?;

    Ok(content)
}

impl EditorFile {
    /* Load the file content, or start empty when the file does not exist.
     * The permission mode of an existing file is kept for save_file. */
    pub fn open(filename: &Path) -> Result<EditorFile, OpenError> {
        let (exist, mode) = match std::fs::metadata(filename) {
            Ok(st) => (true, st.mode() & PERMISSION_MASK),
            Err(err) if err.kind() == io::ErrorKind::NotFound => (false, DEFAULT_FILE_MODE),
            Err(err) => return Err(OpenError::Stat { path: filename.to_owned(), err }),
        };

        let data = if exist {
            load_content(filename)?
        } else {
            Vec::new()
        };

        Ok(EditorFile { path: filename.to_owned(), data, mode, exist })
    }

    /* Readers for the layers above: the editor state and the renderer. */
    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn data(&self) -> &[u8] {
        &self.data
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    pub fn mode(&self) -> u32 {
        self.mode
    }

    pub fn exist(&self) -> bool {
        self.exist
    }

    /* Put n bytes at pos, shifting the tail to the right. Error ranges and
     * messages mirror insert_bytes in the original file layer. */
    pub fn insert(&mut self, pos: usize, bytes: &[u8]) -> Result<(), PrimitiveError> {
        if pos > self.len() {
            return Err(PrimitiveError::InsertPosition);
        }

        if bytes.is_empty() {
            return Ok(());
        }

        if bytes.len().checked_add(self.len()).is_none() {
            return Err(PrimitiveError::TooLarge);
        }

        /* Vec grows and shifts the tail on its own; the original managed
         * the same geometry with realloc and memmove. */
        self.data.splice(pos..pos, bytes.iter().copied());

        Ok(())
    }

    /* Drop the byte at pos, shifting the tail to the left. */
    pub fn remove(&mut self, pos: usize) -> Result<(), PrimitiveError> {
        if pos >= self.len() {
            return Err(PrimitiveError::RemovePosition);
        }

        self.data.remove(pos);

        Ok(())
    }
}

/// Why a byte primitive refused to run.
#[derive(Debug, PartialEq)]
pub enum PrimitiveError {
    /// "error: insert position is out of range"
    InsertPosition,
    /// "error: the file content would be too large"
    TooLarge,
    /// "error: remove position is out of range"
    RemovePosition,
}

impl std::fmt::Display for PrimitiveError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PrimitiveError::InsertPosition => write!(f, "error: insert position is out of range"),
            PrimitiveError::TooLarge => write!(f, "error: the file content would be too large"),
            PrimitiveError::RemovePosition => write!(f, "error: remove position is out of range"),
        }
    }
}

impl std::error::Error for PrimitiveError {}

#[cfg(test)]
mod tests {
    use super::*;

    /* Exclusive private directory for one test, removed by the caller. */
    fn make_temp_dir(tag: &str) -> PathBuf {
        let dir = std::env::temp_dir().join(format!("oldvim-unit-{}-{}", std::process::id(), tag));
        std::fs::create_dir(&dir).expect("cannot create a temporary directory");
        dir
    }

    fn write_file(path: &Path, data: &[u8], mode: u32) {
        std::fs::write(path, data).expect("cannot write the test file");
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = std::fs::metadata(path).unwrap().permissions();
        permissions.set_mode(mode);
        std::fs::set_permissions(path, permissions).unwrap();
    }

    /* Test the code, not the harness. */
    fn owned(path: &Path) -> PathBuf {
        path.to_owned()
    }

    /* `Editor` in the tests below only needs the file part, so the unit
     * tests build bare EditorFiles like make_editor() does in the C suite. */
    fn make_file(text: &[u8]) -> EditorFile {
        EditorFile {
            path: PathBuf::from("unit-test.tmp"),
            data: text.to_vec(),
            mode: 0o644,
            exist: false,
        }
    }

    #[test]
    fn insert_bytes_everywhere() {
        let mut ef = make_file(b"");

        ef.insert(0, b"hello").unwrap();
        assert_eq!(ef.len(), 5);
        assert_eq!(ef.data(), b"hello");

        /* Insert in the middle. */
        ef.insert(2, b"XY").unwrap();
        assert_eq!(ef.data(), b"heXYllo");

        /* Insert at the end. */
        let end = ef.len();
        ef.insert(end, b"!").unwrap();
        assert_eq!(ef.data(), b"heXYllo!");

        /* Zero bytes keeps the buffer untouched. */
        let len = ef.len();
        ef.insert(0, b"").unwrap();
        assert_eq!(ef.len(), len);
    }

    #[test]
    fn insert_out_of_range() {
        let mut ef = make_file(b"abc");
        assert_eq!(ef.insert(4, b"x"), Err(PrimitiveError::InsertPosition));
    }

    #[test]
    fn remove_byte_at_works() {
        let mut ef = make_file(b"abc");

        ef.remove(1).unwrap();
        assert_eq!(ef.data(), b"ac");
        assert_eq!(ef.len(), 2);

        ef.remove(1).unwrap();
        assert_eq!(ef.data(), b"a");

        ef.remove(0).unwrap();
        assert_eq!(ef.data(), b"");

        assert_eq!(ef.remove(0), Err(PrimitiveError::RemovePosition));
    }

    #[test]
    fn open_keeps_existing_content_and_mode() {
        let dir = make_temp_dir("existing");
        let path = dir.join("file.txt");

        write_file(&path, b"hello\n", 0o640);

        let ef = EditorFile::open(&owned(&path)).unwrap();

        assert_eq!(ef.len(), 6);
        assert_eq!(ef.data(), b"hello\n");
        assert_eq!(ef.mode(), 0o640);
        assert!(ef.exist());

        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    #[test]
    fn open_starts_empty_for_a_new_file() {
        let dir = make_temp_dir("new");
        let path = dir.join("new.txt");

        let ef = EditorFile::open(&owned(&path)).unwrap();

        assert_eq!(ef.len(), 0);
        assert!(ef.is_empty());
        assert!(!ef.exist());
        assert_eq!(ef.mode(), DEFAULT_FILE_MODE);
        assert_eq!(ef.path(), Path::new(&dir).join("new.txt"));

        std::fs::remove_dir(&dir).unwrap(); /* nothing was created yet */
    }

    #[test]
    fn open_reports_errors_of_broken_paths() {
        /* A directory cannot be read as a file: the open succeeds and the
         * read fails, exactly like the fread/ferror path in the original. */
        let dir = make_temp_dir("dirfail");
        let err = EditorFile::open(&owned(&dir)).unwrap_err();

        assert!(matches!(err, OpenError::Read { .. }));
        assert_eq!(err.to_string(), "error: cannot read the file");

        /* A parent name that is a file makes stat fail (ENOTDIR). */
        let blocker = dir.join("blocker");
        std::fs::write(&blocker, b"x").unwrap();
        let path = dir.join("blocker/file.txt");
        let err = EditorFile::open(&owned(&path)).unwrap_err();

        assert!(matches!(err, OpenError::Stat { .. }));
        assert!(
            err.to_string().starts_with(&format!("error: cannot stat {}: ", path.display())),
            "unexpected message: {err}"
        );

        /* A path under a missing directory is just a non-existing file. */
        let missing = dir.join("no-such-dir/file.txt");
        let ef = EditorFile::open(&owned(&missing)).unwrap();

        assert!(!ef.exist());
        assert!(ef.is_empty());

        std::fs::remove_file(&blocker).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }
}

