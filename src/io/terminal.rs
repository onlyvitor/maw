// Switching the controlling terminal between raw mode and the saved
// configuration. This is the one place allowed to touch termios and
// signal handlers directly: no editor logic belongs here.

use std::io;
use std::io::IsTerminal;
use std::sync::atomic::{AtomicBool, AtomicPtr, Ordering};

use crate::io::viewport::Size;

/// Signals that must restore the terminal even when the editor dies.
const FATAL_SIGNALS: [libc::c_int; 4] = [
    libc::SIGINT,
    libc::SIGTERM,
    libc::SIGHUP,
    libc::SIGQUIT,
];

/* The saved configuration lives here exactly like the C static: armed
 * before the handlers, and only read by the handler afterwards. The
 * pointer stays allocated for the process lifetime, which is what a
 * static is in the original. */
static RAWMODE_ENABLED: AtomicBool = AtomicBool::new(false);
static SAVED_TERMIOS: AtomicPtr<libc::termios> = AtomicPtr::new(std::ptr::null_mut());

/// Why the terminal could not change. Each variant names the step that
/// failed, like the perror calls of the original.
#[derive(Debug)]
pub enum TerminalError {
    /// "error: cannot get the terminal attributes: <cause>"
    GetAttributes(io::Error),
    /// "error: cannot enable the raw mode: <cause>"
    Enable(io::Error),
    /// "error: cannot restore the terminal attributes: <cause>"
    Restore(io::Error),
}

impl std::fmt::Display for TerminalError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TerminalError::GetAttributes(err) => {
                write!(f, "error: cannot get the terminal attributes: {err}")
            }
            TerminalError::Enable(err) => {
                write!(f, "error: cannot enable the raw mode: {err}")
            }
            TerminalError::Restore(err) => {
                write!(f, "error: cannot restore the terminal attributes: {err}")
            }
        }
    }
}

impl std::error::Error for TerminalError {}

/* Restore the saved configuration and leave with the conventional
 * signal status. tcsetattr and _exit are async-signal-safe. */
extern "C" fn handle_signal(signum: libc::c_int) {
    if RAWMODE_ENABLED.load(Ordering::Relaxed) {
        let saved = SAVED_TERMIOS.load(Ordering::Relaxed);

        if !saved.is_null() {
            unsafe {
                libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, saved);
            }
        }
    }

    unsafe {
        libc::_exit(128 + signum);
    }
}

/* Ask the terminal for its geometry. Falls back to 80x24 when stdout
 * is not a terminal or the kernel reports a zero-sized screen. */
pub fn size() -> Size {
    if !io::stdout().is_terminal() {
        return Size::FALLBACK;
    }

    let mut ws: libc::winsize = unsafe { std::mem::zeroed() };

    let rc = unsafe { libc::ioctl(libc::STDOUT_FILENO, libc::TIOCGWINSZ, &mut ws) };

    if rc == -1 || ws.ws_row == 0 || ws.ws_col == 0 {
        return Size::FALLBACK;
    }

    Size::new(ws.ws_row as usize, ws.ws_col as usize)
}

fn install_signal_handlers() {    let mut failure = None;

    for &signal in &FATAL_SIGNALS {
        let mut action: libc::sigaction = unsafe { std::mem::zeroed() };

        action.sa_sigaction = handle_signal as extern "C" fn(libc::c_int) as usize;
        action.sa_flags = 0;

        unsafe {
            libc::sigemptyset(&mut action.sa_mask);

            if libc::sigaction(signal, &action, std::ptr::null_mut()) == -1 {
                failure.get_or_insert(io::Error::last_os_error());
            }
        }
    }

    /* The original reports the failure but keeps running. */
    if let Some(err) = failure {
        eprintln!("error: cannot install the signal handler: {err}");
    }
}

#[derive(Debug, Default)]
pub struct RawMode {
    enabled: bool,
}

impl RawMode {
    /* Enter raw mode: no line buffering, no echo, no software flow
     * control. A no-op when the editor already ran or stdin is not a
     * tty. */
    pub fn enable(&mut self) -> Result<(), TerminalError> {
        if self.enabled {
            return Ok(());
        }

        if !io::stdin().is_terminal() {
            return Ok(());
        }

        let mut saved: libc::termios = unsafe { std::mem::zeroed() };

        let rc = unsafe { libc::tcgetattr(libc::STDIN_FILENO, &mut saved) };

        if rc == -1 {
            return Err(TerminalError::GetAttributes(io::Error::last_os_error()));
        }

        /* The saved configuration goes where the signal handlers will
         * find it before the non-standard settings are attempted. */
        let mut raw = saved;
        SAVED_TERMIOS.store(Box::into_raw(Box::new(saved)), Ordering::Relaxed);

        install_signal_handlers();

        // disable line buffering and automatic echo
        raw.c_lflag &= !(libc::ICANON | libc::ECHO);

        // disable software flow control so ^S reaches the editor instead
        // of stopping the terminal output
        raw.c_iflag &= !(libc::IXON | libc::IXOFF | libc::IXANY);

        // read one byte at a time, without a timeout
        raw.c_cc[libc::VMIN as usize] = 1;
        raw.c_cc[libc::VTIME as usize] = 0;

        // apply the new settings
        let rc = unsafe {
            libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, &raw)
        };

        if rc == -1 {
            return Err(TerminalError::Enable(io::Error::last_os_error()));
        }

        self.enabled = true;
        RAWMODE_ENABLED.store(true, Ordering::Relaxed);

        Ok(())
    }

    /* Restore the default terminal configuration. */
    pub fn disable(&mut self) -> Result<(), TerminalError> {
        if !self.enabled {
            return Ok(());
        }

        let saved = SAVED_TERMIOS.load(Ordering::Relaxed);

        let rc = unsafe {
            libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, saved)
        };

        if rc == -1 {
            return Err(TerminalError::Restore(io::Error::last_os_error()));
        }

        self.enabled = false;
        RAWMODE_ENABLED.store(false, Ordering::Relaxed);

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /* The tests never own a tty: writer processes in test runners are
     * not terminals, so every switch here is a no-op path. */

    #[test]
    fn raw_mode_leaves_non_tty_stdin_alone() {
        let mut mode = RawMode::default();

        mode.enable().unwrap();
        assert!(!mode.enabled, "stdin is not a tty in the test runner");

        mode.disable().unwrap();
        assert!(!mode.enabled);
    }

    #[test]
    fn size_falls_back_without_a_tty() {
        /* The test runner's stdout is captured, not a terminal. */
        assert_eq!(size(), Size::FALLBACK);
    }

    #[test]
    fn enabling_twice_is_one_switch() {
        let mut mode = RawMode::default();

        mode.enable().unwrap();
        mode.enable().unwrap();
        assert!(!mode.enabled);

        mode.disable().unwrap();
        mode.disable().unwrap();
        assert!(!mode.enabled);
    }
}
