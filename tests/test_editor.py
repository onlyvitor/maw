#!/usr/bin/env python3
"""End-to-end tests for oldvim, driven through a pty."""
import fcntl
import os
import pty
import select
import shutil
import struct
import sys
import tempfile
import termios
import time

# The built binary; OLDVIM_BIN overrides for out-of-tree builds.
BIN = os.environ.get(
    "OLDVIM_BIN",
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "oldvim",
    ),
)

# The geometry every session starts with. It matches the editor's
# fallback for terminals that report nothing.
ROWS = 24
COLUMNS = 80

UP = b"\x1b[A"
DOWN = b"\x1b[B"
RIGHT = b"\x1b[C"
LEFT = b"\x1b[D"
BACKSPACE = b"\x7f"
SAVE = b"\x13"
QUIT = b"\x04"


def set_winsize(fd, rows=ROWS, columns=COLUMNS):
    fcntl.ioctl(
        fd,
        termios.TIOCSWINSZ,
        struct.pack("HHHH", rows, columns, 0, 0),
    )


def last_frame(data):
    """The output since the last full repaint (each one starts at home)."""
    return data.rsplit(b"\x1b[H", 1)[-1]


class Session:
    def __init__(self, path, rows=ROWS, columns=COLUMNS):
        self.pid, self.fd = pty.fork()

        if self.pid == 0:
            os.execv(BIN, [BIN, path])

        set_winsize(self.fd, rows, columns)
        self.initial = self.drain()

    def drain(self, timeout=0.15):
        out = b""
        end = time.time() + timeout

        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.02)

            if ready:
                try:
                    out += os.read(self.fd, 65536)
                except OSError:
                    break

        return out

    def send(self, data):
        os.write(self.fd, data)
        return self.drain()

    def quit(self):
        out = self.send(QUIT)

        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass

        return out


class Case:
    def __init__(self, tmp):
        self.tmp = tmp

    def path(self, name):
        return os.path.join(self.tmp, name)

    def write(self, name, data):
        path = self.path(name)

        with open(path, "wb") as fp:
            fp.write(data)

        return path

    def read(self, path):
        with open(path, "rb") as fp:
            return fp.read()


def test_insert_at_end(tmp):
    path = Case(tmp).write("a.txt", b"abc\ndef\n")
    session = Session(path)
    session.send(b"Z")
    session.send(SAVE)
    assert Case(tmp).read(path) == b"abc\ndef\nZ", "save must not wait for quit"
    session.quit()


def test_insert_in_the_middle(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"abc\ndef\n")
    session = Session(path)
    session.send(LEFT)
    session.send(LEFT)
    session.send(b"X")
    session.send(SAVE)
    assert case.read(path) == b"abc\ndeXf\n"
    session.quit()


def test_backspace_joins_lines(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"abc\ndef\n")
    session = Session(path)
    for _ in range(4):
        session.send(LEFT)
    session.send(BACKSPACE)
    session.send(SAVE)
    assert case.read(path) == b"abcdef\n"
    session.quit()


def test_vertical_movement_clamps_column(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"abcde\nxy\n")
    session = Session(path)
    session.send(UP)
    session.send(UP)
    for _ in range(5):
        session.send(RIGHT)
    session.send(DOWN)
    session.send(b"Z")
    session.send(SAVE)
    assert case.read(path) == b"abcde\nxyZ\n"
    session.quit()


def test_draws_file_on_startup(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"abc\n")
    session = Session(path)
    assert b"abc" in session.initial, "the file must be drawn after loading"
    session.quit()


def test_opens_on_the_tail_of_a_large_file(tmp):
    case = Case(tmp)
    path = case.write(
        "big.txt", b"".join(f"L{i:02d}\n".encode() for i in range(30))
    )
    session = Session(path)

    frame = last_frame(session.initial)

    assert b"L29" in frame, "the cursor row must be on screen"
    assert b"L00" not in frame, "a large file opens scrolled to its end"
    session.quit()


def test_scrolls_up_to_the_first_line(tmp):
    case = Case(tmp)
    path = case.write(
        "big.txt", b"".join(f"L{i:02d}\n".encode() for i in range(30))
    )
    session = Session(path)

    frame = last_frame(session.send(UP * 40))

    assert b"L00" in frame, "the top of the buffer must be reachable"
    assert b"L24" not in frame, "only one window fits on screen"
    session.quit()


def test_scrolls_back_down_to_the_last_line(tmp):
    case = Case(tmp)
    path = case.write(
        "big.txt", b"".join(f"L{i:02d}\n".encode() for i in range(30))
    )
    session = Session(path)

    session.send(UP * 40)
    frame = last_frame(session.send(DOWN * 40))

    assert b"L29" in frame, "the end of the buffer must be reachable again"
    assert b"L06" not in frame
    session.quit()


def test_typing_past_the_bottom_scrolls_the_window(tmp):
    case = Case(tmp)
    path = case.write(
        "full.txt", b"".join(f"L{i:02d}\n".encode() for i in range(23))
    )
    session = Session(path)

    session.send(b"X")
    frame = last_frame(session.send(b"\n"))

    assert b"L01" in frame, "the window scrolled down one row"
    assert b"L00" not in frame, "the first row left the screen"
    session.quit()


def test_clips_lines_wider_than_the_screen(tmp):
    case = Case(tmp)
    path = case.write("long.txt", b"0123456789" * 10 + b"\n")
    session = Session(path)

    frame = last_frame(session.initial)

    assert frame.count(b"0123456789") == 7, "only 79 cells are painted"
    assert b"0123456789" * 7 + b"012345678" in frame
    session.quit()


def test_resizing_repaints_the_window(tmp):
    case = Case(tmp)
    path = case.write(
        "big.txt", b"".join(f"L{i:02d}\n".encode() for i in range(30))
    )
    session = Session(path)

    set_winsize(session.fd, 10, 40)
    frame = last_frame(session.send(SAVE))

    assert b"L21" in frame, "the smaller window follows the cursor"
    assert b"L20" not in frame, "rows above the new window are not drawn"
    session.quit()


def test_uses_the_alternate_screen(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"abc\n")
    session = Session(path)

    assert b"\x1b[?1049h" in session.initial, "the editor owns a fresh screen"
    assert b"\x1b[?1049l" in session.quit(), "the shell screen comes back"


def test_create_new_file(tmp):
    case = Case(tmp)
    path = case.path("new.txt")
    session = Session(path)
    session.send(b"hi")
    session.send(SAVE)
    assert case.read(path) == b"hi"
    session.quit()


def test_save_then_keep_editing(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"")
    session = Session(path)
    session.send(b"a")
    session.send(SAVE)
    assert case.read(path) == b"a"
    session.send(b"b")
    session.quit()
    assert case.read(path) == b"ab", "quit must save pending edits"


def test_non_interactive_stdin(tmp):
    case = Case(tmp)
    path = case.write("a.txt", b"")
    script = f"printf 'ZZ' | {BIN} {path} >/dev/null"

    assert os.system(script) == 0
    assert case.read(path) == b"ZZ"


def main():
    tests = [
        test_insert_at_end,
        test_insert_in_the_middle,
        test_backspace_joins_lines,
        test_vertical_movement_clamps_column,
        test_draws_file_on_startup,
        test_opens_on_the_tail_of_a_large_file,
        test_scrolls_up_to_the_first_line,
        test_scrolls_back_down_to_the_last_line,
        test_typing_past_the_bottom_scrolls_the_window,
        test_clips_lines_wider_than_the_screen,
        test_resizing_repaints_the_window,
        test_uses_the_alternate_screen,
        test_create_new_file,
        test_save_then_keep_editing,
        test_non_interactive_stdin,
    ]
    failed = 0

    for test in tests:
        tmp = tempfile.mkdtemp(prefix="oldvim-test-")

        try:
            test(tmp)
            print(f"PASS {test.__name__}")
        except Exception as exc:  # noqa: BLE001
            failed += 1
            print(f"FAIL {test.__name__}: {exc}")
        finally:
            shutil.rmtree(tmp)

    if failed:
        print(f"{failed} test(s) failed")
        return 1

    print(f"all {len(tests)} tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
