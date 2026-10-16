#!/usr/bin/env python3
"""Fetch the same pinned Ultralight release as Windows, with the Linux checksum."""
import ctypes
import ctypes.util
import hashlib
import json
import pathlib
import subprocess
import tempfile

VERSION = "2.0.0-beta.2"
SHA256 = "6bd198e4e2a6e56f2cee89b8cd8122309ee7f65f3464b4cbc721fe00f3047dc6"
ROOT = pathlib.Path(__file__).resolve().parents[2] / ".deps" / "ultralight-linux"

def extract(archive, destination):
    lib = ctypes.CDLL(ctypes.util.find_library("archive") or "libarchive.so.13")
    pointer = ctypes.c_void_p
    for name, result, args in [
        ("archive_read_new", pointer, []),
        ("archive_read_support_filter_all", ctypes.c_int, [pointer]),
        ("archive_read_support_format_all", ctypes.c_int, [pointer]),
        ("archive_read_open_filename", ctypes.c_int, [pointer, ctypes.c_char_p, ctypes.c_size_t]),
        ("archive_read_next_header", ctypes.c_int, [pointer, ctypes.POINTER(pointer)]),
        ("archive_entry_pathname", ctypes.c_char_p, [pointer]),
        ("archive_entry_filetype", ctypes.c_int, [pointer]),
        ("archive_read_data", ctypes.c_ssize_t, [pointer, pointer, ctypes.c_size_t]),
        ("archive_read_free", ctypes.c_int, [pointer]),
    ]:
        function = getattr(lib, name)
        function.restype, function.argtypes = result, args
    reader = lib.archive_read_new()
    try:
        lib.archive_read_support_filter_all(reader)
        lib.archive_read_support_format_all(reader)
        if lib.archive_read_open_filename(reader, bytes(archive), 10240) != 0:
            raise RuntimeError("Could not open SDK archive")
        entry, buffer = pointer(), ctypes.create_string_buffer(1024 * 1024)
        while True:
            status = lib.archive_read_next_header(reader, ctypes.byref(entry))
            if status == 1:
                break
            if status != 0:
                raise RuntimeError("Could not read SDK archive")
            path = (destination / lib.archive_entry_pathname(entry).decode()).resolve()
            if not path.is_relative_to(destination.resolve()):
                raise RuntimeError("Unsafe SDK archive path")
            kind = lib.archive_entry_filetype(entry)
            if kind == 0o040000:
                path.mkdir(parents=True, exist_ok=True)
                continue
            if kind != 0o100000:
                raise RuntimeError("Unexpected SDK archive entry")
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("wb") as output:
                while True:
                    size = lib.archive_read_data(reader, buffer, len(buffer))
                    if size < 0:
                        raise RuntimeError("Could not extract SDK")
                    if size == 0:
                        break
                    output.write(buffer.raw[:size])
    finally:
        lib.archive_read_free(reader)

def main():
    if (ROOT / "VERSION.txt").exists():
        if (ROOT / "VERSION.txt").read_text().strip() != VERSION:
            raise RuntimeError("Unexpected existing SDK version; choose the pinned Linux SDK")
        if (ROOT / "include/Ultralight/Ultralight.h").exists() and (ROOT / "bin/libUltralight.so").exists():
            print("Pinned Linux renderer SDK is already available")
            return
    ROOT.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT.parent, prefix="renderer-download-") as temp:
        directory = pathlib.Path(temp)
        metadata = subprocess.check_output(["curl", "-fLsS", "--max-time", "45",
            f"https://ultralig.ht/api/v1/sdk/download?platform=linux-x64&version={VERSION}&format=json"])
        info = json.loads(metadata)
        archive = directory / "sdk.7z"
        subprocess.run(["curl", "-fLsS", "--max-time", "180", info["url"], "-o", str(archive)], check=True)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError("Linux renderer SDK checksum mismatch")
        unpacked = directory / "unpacked"
        unpacked.mkdir()
        extract(archive, unpacked)
        if (unpacked / "VERSION.txt").read_text().strip() != VERSION:
            raise RuntimeError("Unexpected SDK version")
        if ROOT.exists():
            raise RuntimeError("An incomplete SDK already exists; preserve or remove it before retrying")
        unpacked.rename(ROOT)
    print("Pinned Linux renderer SDK downloaded and verified")

if __name__ == "__main__":
    main()
