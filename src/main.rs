// Wiring: open the editor, switch the terminal to raw mode, draw the
// file, run the key loop, and save on the way out. The exit status
// follows the original: 0 unless something failed along the path.

mod editor;
mod fileio;
mod io;

use std::io::IsTerminal;
use std::path::Path;
use std::process::ExitCode;

use crate::editor::{handle_key, row_column, Editor, EditorAction, EditorChange, EditorError};
use crate::io::input::scan;
use crate::io::output;
use crate::io::terminal::{self, RawMode};
use crate::io::viewport::Viewport;

fn main() -> ExitCode {
    let mut args = std::env::args();

    let program = args.next().unwrap_or_else(|| "oldvim".to_string());

    let Some(path) = args.next() else {
        eprintln!("Usage: {program} <filename>");
        return ExitCode::FAILURE;
    };

    let mut ed = match Editor::open(Path::new(&path)) {
        Ok(ed) => ed,
        Err(err) => {
            eprintln!("{err}");
            return ExitCode::FAILURE;
        }
    };

    let mut raw = RawMode::default();

    if let Err(err) = raw.enable() {
        eprintln!("{err}");
        return ExitCode::FAILURE;
    }

    let interactive = std::io::stdin().is_terminal();
    let mut status: u8 = 0;
    let mut viewport = Viewport::new(terminal::size());

    if interactive {
        /* Start with the cursor (the end of the buffer) on screen. */
        viewport.ensure_visible(row_column(&ed.file, ed.cursor).0);

        if let Err(err) = output::screen_draw(&ed, &viewport) {
            eprintln!("error: cannot draw the file: {err}");
            status = 1;
        }
    }

    let mut change = EditorChange::None;

    while status == 0 {
        match scan() {
            Err(_) => {
                /* The original reports read errors silently, with
                 * status 1 only. */
                status = 1;
                break;
            }
            Ok(None) => {
                println!("\n[EOF]");
                break;
            }
            Ok(Some(key)) => match handle_key(&mut ed, key, &mut change) {
                Err(err) => {
                    report_editor_error(&err);
                    status = 1;
                    break;
                }
                Ok(EditorAction::Quit) => break,
                Ok(EditorAction::Continue) => {
                    if interactive {
                        let size = terminal::size();

                        if let Err(err) = output::apply_change(&ed, &mut viewport, size, change) {
                            eprintln!("error: cannot draw the change: {err}");
                            status = 1;
                            break;
                        }
                    }
                }
            },
        }
    }

    if let Err(err) = raw.disable() {
        eprintln!("{err}");
        status = 1;
    }

    if status == 0 {
        if let Err(err) = ed.file.save() {
            report_save_error(&err);
            status = 1;
        }
    }

    ExitCode::from(status)
}

fn report_editor_error(err: &EditorError) {
    eprintln!("{err}");

    if let EditorError::Save(save) = err {
        report_save_error(save);
    }
}

/* The message that gives the failed pipeline the temporary file name. */
fn report_save_error(err: &fileio::SaveError) {
    eprintln!("{err}");
    eprintln!(
        "error: the edited content was kept in {}",
        err.temp().display()
    );
}
