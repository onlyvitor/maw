#!/usr/bin/env python3
"""End-to-end tests for oldvim, driven through a pty."""
import os
import pty
import select
import shutil
import sys
import tempfile
import time

# The built Rust binary; OLDVIM_BIN overrides for out-of-tree builds.
BIN = os.environ.get(
    "OLDVIM_BIN",
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "target",
        "debug",
        "oldvim",
    ),
)

UP = b"\x1b[A"
DOWN = b"\x1b[B"
RIGHT = b"\x1b[C"
LEFT = b"\x1b[D"
BACKSPACE = b"\x7f"
SAVE = b"\x13"
QUIT = b"\x04"


class Session:
    def __init__(self, path):
        self.pid, self.fd = pty.fork()

        if self.pid == 0:
            os.execv(BIN, [BIN, path])

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
        self.drain()

    def quit(self):
        self.send(QUIT)

        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass


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
