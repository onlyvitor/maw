// The file layer: an owned buffer plus loading and atomic saving.

use std::ffi::{CString, OsStr};
use std::fs::File;
use std::io;
use std::io::Read;
use std::io::Write;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::MetadataExt;
use std::os::unix::io::FromRawFd;
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
    Read,
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
            OpenError::Read => write!(f, "error: cannot read the file"),
        }
    }
}

impl std::error::Error for OpenError {}

/* Read the whole file into the buffer. */
fn load_content(filename: &Path) -> Result<Vec<u8>, OpenError> {
    let mut content = Vec::new();

    let mut file = File::open(filename)
        .map_err(|err| OpenError::Open {
            path: filename.to_owned(),
            err,
        })?;

    file.read_to_end(&mut content)
        .map_err(|_| OpenError::Read)?;

    Ok(content)
}

impl EditorFile {
    /* Load the file content, or start empty when the file does not exist.
     * The permission mode of an existing file is kept for save_file. */
    pub fn open(filename: &Path) -> Result<EditorFile, OpenError> {
        let (exist, mode) = match std::fs::metadata(filename) {
            Ok(st) => (true, st.mode() & PERMISSION_MASK),
            Err(err) if err.kind() == io::ErrorKind::NotFound => (false, DEFAULT_FILE_MODE),
            Err(err) => {
                return Err(OpenError::Stat {
                    path: filename.to_owned(),
                    err,
                })
            }
        };

        let data = if exist { load_content(filename)? } else { Vec::new() };

        Ok(EditorFile {
            path: filename.to_owned(),
            data,
            mode,
            exist,
        })
    }

    /* Readers for the layers above: the editor state and the renderer. */
    pub fn data(&self) -> &[u8] {
        &self.data
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn mode(&self) -> u32 {
        self.mode
    }

    /* Test constructor: a private buffer with a fixed path, without
     * touching the file system. */
    #[cfg(test)]
    pub fn from_parts_for_tests(path: &str, data: &[u8]) -> EditorFile {
        EditorFile {
            path: PathBuf::from(path),
            data: data.to_vec(),
            mode: 0o644,
            exist: false,
        }
    }

    /* Put bytes at pos, shifting the tail to the right. Error ranges and
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

    /* Save pipeline: create "<path>.tmp.XXXXXX" with mkstemp, write the
     * whole buffer, restore the saved permission mode, sync it to disk,
     * and put the temporary file in place of the edited one. On any
     * failure the temporary file is deliberately kept so the edited
     * content is not lost, and the caller can report where it waits. */
    pub fn save(&mut self) -> Result<(), SaveError> {
        let mut temp = make_temp_path(&self.path)?;

        write_temp_file(self, &mut temp)?;
        replace_file(Path::new(OsStr::from_bytes(temp.to_bytes())), &self.path)?;

        self.exist = true;

        Ok(())
    }
}

/* Smallest temporary suffix; mkstemp fills the X run with random bytes. */
const TMP_SUFFIX: &[u8] = b".tmp.XXXXXX";

fn make_temp_path(path: &Path) -> Result<CString, SaveError> {
    let mut bytes = path.as_os_str().as_bytes().to_vec();

    bytes.extend_from_slice(TMP_SUFFIX);

    /* Path names cannot contain NUL, so this only fails past that limit. */
    CString::new(bytes).map_err(|_| SaveError::Create {
        temp: path.to_owned(),
        err: io::Error::new(io::ErrorKind::InvalidInput, "path contains a NUL byte"),
    })
}

/* Create temp_path and fill it with the buffer, syncing it to disk. */
fn write_temp_file(ef: &EditorFile, temp: &mut CString) -> Result<(), SaveError> {
    let temp_path = PathBuf::from(OsStr::from_bytes(temp.to_bytes()));

    let fd = unsafe { libc::mkstemp(temp.as_ptr() as *mut libc::c_char) };

    if fd < 0 {
        return Err(SaveError::Create {
            temp: temp_path,
            err: io::Error::last_os_error(),
        });
    }

    /* The wrapper owns fd for the whole pipeline. close() is fallible in
     * the original, so it happens by hand at the end, and the File is
     * stripped of ownership without closing fd a second time. */
    let mut file = unsafe { File::from_raw_fd(fd) };

    if let Err(err) = file.write_all(ef.data()) {
        return Err(SaveError::Write { temp: temp_path, err });
    }

    if unsafe { libc::fchmod(fd, ef.mode()) } < 0 {
        return Err(SaveError::Permissions {
            temp: temp_path,
            err: io::Error::last_os_error(),
        });
    }

    if let Err(err) = file.sync_all() {
        return Err(SaveError::Sync { temp: temp_path, err });
    }

    std::mem::forget(file);

    if unsafe { libc::close(fd) } < 0 {
        return Err(SaveError::Close {
            temp: temp_path,
            err: io::Error::last_os_error(),
        });
    }

    Ok(())
}

/* Atomically put the temporary file in place of the edited file. */
fn replace_file(temp: &Path, path: &Path) -> Result<(), SaveError> {
    std::fs::rename(temp, path).map_err(|err| SaveError::Replace {
        temp: temp.to_owned(),
        path: path.to_owned(),
        err,
    })?;

    fsync_parent_dir(path);

    Ok(())
}

/* Best effort: the rename already happened. */
fn fsync_parent_dir(path: &Path) {
    let dir = path.parent().unwrap_or(Path::new("."));

    if let Ok(dir) = File::open(dir) {
        let _ = dir.sync_all();
    }
}

/// Why a save stopped. The temporary file keeps the edited content on
/// every variant, so report where it waits with the step message.
#[derive(Debug)]
pub enum SaveError {
    /// "error: cannot create <path>.tmp.XXXXXX: <cause>"
    Create { temp: PathBuf, err: io::Error },
    /// "error: cannot write the temporary file: <cause>"
    Write { temp: PathBuf, err: io::Error },
    /// "error: cannot set the file permissions: <cause>"
    Permissions { temp: PathBuf, err: io::Error },
    /// "error: cannot sync the temporary file: <cause>"
    Sync { temp: PathBuf, err: io::Error },
    /// "error: cannot close the temporary file: <cause>"
    Close { temp: PathBuf, err: io::Error },
    /// "error: cannot replace <path>: <cause>"
    Replace { temp: PathBuf, path: PathBuf, err: io::Error },
}

impl SaveError {
    /* Where the freshly saved content waits when a step failed. */
    pub fn temp(&self) -> &Path {
        match self {
            SaveError::Create { temp, .. }
            | SaveError::Write { temp, .. }
            | SaveError::Permissions { temp, .. }
            | SaveError::Sync { temp, .. }
            | SaveError::Close { temp, .. }
            | SaveError::Replace { temp, .. } => temp,
        }
    }
}

impl std::fmt::Display for SaveError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            SaveError::Create { temp, err } => {
                write!(f, "error: cannot create {}: {}", temp.display(), err)
            }
            SaveError::Write { err, .. } => {
                write!(f, "error: cannot write the temporary file: {err}")
            }
            SaveError::Permissions { err, .. } => {
                write!(f, "error: cannot set the file permissions: {err}")
            }
            SaveError::Sync { err, .. } => {
                write!(f, "error: cannot sync the temporary file: {err}")
            }
            SaveError::Close { err, .. } => {
                write!(f, "error: cannot close the temporary file: {err}")
            }
            SaveError::Replace { path, err, .. } => {
                write!(f, "error: cannot replace {}: {}", path.display(), err)
            }
        }
    }
}

impl std::error::Error for SaveError {}

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
pub mod tests {
    use super::*;

    /* Exclusive private directory for one test, removed by the caller. */
    pub fn make_temp_dir(tag: &str) -> PathBuf {
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

    fn read_file(path: &Path) -> Vec<u8> {
        let mut content = Vec::new();
        std::fs::File::open(path)
            .unwrap()
            .read_to_end(&mut content)
            .unwrap();
        content
    }

    fn file_mode(path: &Path) -> u32 {
        std::fs::metadata(path).unwrap().mode() & 0o7777
    }

    /* ---------------------------------------------------------------- */
    /* open                                                             */
    /* ---------------------------------------------------------------- */

    #[test]
    fn open_keeps_existing_content_and_mode() {
        let dir = make_temp_dir("existing");
        let path = dir.join("file.txt");

        write_file(&path, b"hello\n", 0o640);

        let ef = EditorFile::open(&owned(&path)).unwrap();

        assert_eq!(ef.len(), 6);
        assert_eq!(ef.data(), b"hello\n");
        assert_eq!(ef.mode(), 0o640);
        assert!(ef.exist);

        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    #[test]
    fn open_starts_empty_for_a_new_file() {
        let dir = make_temp_dir("new");
        let path = dir.join("new.txt");

        let ef = EditorFile::open(&owned(&path)).unwrap();

        assert_eq!(ef.len(), 0);
        assert!(ef.data().is_empty());
        assert!(!ef.exist);
        assert_eq!(ef.mode(), DEFAULT_FILE_MODE);

        std::fs::remove_dir(&dir).unwrap(); /* nothing was created yet */
    }

    #[test]
    fn open_reports_errors_of_broken_paths() {
        /* A directory cannot be read as a file: the open succeeds and the
         * read fails, exactly like the fread/ferror path in the original. */
        let dir = make_temp_dir("dirfail");
        let err = EditorFile::open(&owned(&dir)).unwrap_err();

        assert!(matches!(err, OpenError::Read));
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

        assert!(!ef.exist);
        assert!(ef.data().is_empty());

        std::fs::remove_file(&blocker).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    /* ---------------------------------------------------------------- */
    /* byte primitives                                                  */
    /* ---------------------------------------------------------------- */

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
    fn insert_grows_the_buffer_by_chunks() {
        let mut ef = EditorFile::from_parts_for_tests("unit-test.tmp", b"");

        /* One thousand bytes in a single call, holding 'x' everywhere. */
        ef.insert(0, &[b'x'; 1000]).unwrap();

        assert_eq!(ef.len(), 1000);
        assert_eq!(ef.data()[0], b'x');
        assert_eq!(ef.data()[999], b'x');
        /* Content over capacity boundaries stays intact. */
        assert!(ef.data().iter().all(|&b| b == b'x'));
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

    /* ---------------------------------------------------------------- */
    /* save                                                             */
    /* ---------------------------------------------------------------- */

    #[test]
    fn save_replaces_the_file_in_place() {
        let dir = make_temp_dir("save");
        let path = dir.join("file.txt");

        write_file(&path, b"hello\n", 0o640);

        let mut ef = EditorFile::open(&owned(&path)).unwrap();
        ef.insert(ef.len(), b"!").unwrap();
        ef.save().unwrap();

        assert_eq!(read_file(&path), b"hello\n!");
        assert_eq!(file_mode(&path), 0o640);
        assert!(ef.exist);

        /* The temporary file never lingers on success. */
        let mut leftovers: Vec<String> = std::fs::read_dir(&dir)
            .unwrap()
            .map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned())
            .collect();

        leftovers.sort();

        assert_eq!(leftovers, ["file.txt"]);

        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    #[test]
    fn save_creates_a_missing_file_with_default_mode() {
        let dir = make_temp_dir("create");
        let path = dir.join("new.txt");

        let mut ef = EditorFile::open(&owned(&path)).unwrap();

        ef.insert(0, b"hi").unwrap();
        ef.save().unwrap();

        assert!(ef.exist);
        assert_eq!(read_file(&path), b"hi");
        assert_eq!(file_mode(&path), 0o644);

        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    #[test]
    fn save_after_removal() {
        let dir = make_temp_dir("removal");
        let path = dir.join("remove.txt");

        write_file(&path, b"abc\ndef\n", 0o644);

        let mut ef = EditorFile::open(&owned(&path)).unwrap();

        /* The newline ending the first line goes away. */
        ef.remove(3).unwrap();
        ef.save().unwrap();

        assert_eq!(read_file(&path), b"abcdef\n");

        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&dir).unwrap();
    }

    #[test]
    fn save_identifies_a_missing_parent_directory() {        let dir = make_temp_dir("vanish");

        /* The parent directory disappears before the save: mkstemp fails,
         * and the message points at the template that could not exist. */
        let mut ef = EditorFile::open(&owned(&dir.join("gone/file.txt"))).unwrap();

        ef.insert(0, b"saved").unwrap();
        let err = ef.save().unwrap_err();
        let message = err.to_string();

        assert!(matches!(err, SaveError::Create { .. }), "{message}");
        assert!(
            message.starts_with(&format!("error: cannot create {}/gone/file.txt.tmp.", dir.display())),
            "{message}"
        );

        std::fs::remove_dir_all(&dir).unwrap();
    }
}
