#!/usr/bin/env python3
"""Acquire pinned Mac SDKs without requiring a third-party archive utility."""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import pathlib
import struct
import tempfile
import urllib.parse
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = ROOT / 'macos/Config/renderer-sdks.json'
LIBRARIES = ('AppCore', 'Ultralight', 'UltralightCore', 'WebCore')


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def validate_sdk(root, arch, entry):
    if (root / 'VERSION.txt').read_text().strip() != entry['sdk_version']:
        raise RuntimeError('Unexpected SDK version')
    if not (root / 'include/Ultralight/Ultralight.h').is_file():
        raise RuntimeError('SDK headers are missing')
    expected_cpu = {'x86_64': 0x01000007, 'arm64': 0x0100000C}[arch]
    for name in LIBRARIES:
        with (root / 'bin' / f'lib{name}.dylib').open('rb') as stream:
            header = stream.read(8)
        if len(header) != 8 or struct.unpack('<II', header) != (0xFEEDFACF, expected_cpu):
            raise RuntimeError(f'{name} does not match the selected Mac architecture')
    for name in ('icudt67l.dat', 'cacert.pem'):
        if not (root / 'resources' / name).is_file():
            raise RuntimeError(f'Missing SDK resource: {name}')


def extract(archive, destination):
    lib = ctypes.CDLL(ctypes.util.find_library('archive') or '/usr/lib/libarchive.dylib')
    pointer = ctypes.c_void_p
    for name, result, args in (
        ('archive_read_new', pointer, []),
        ('archive_read_support_filter_all', ctypes.c_int, [pointer]),
        ('archive_read_support_format_all', ctypes.c_int, [pointer]),
        ('archive_read_open_filename', ctypes.c_int, [pointer, ctypes.c_char_p, ctypes.c_size_t]),
        ('archive_read_next_header', ctypes.c_int, [pointer, ctypes.POINTER(pointer)]),
        ('archive_entry_pathname', ctypes.c_char_p, [pointer]),
        ('archive_entry_filetype', ctypes.c_int, [pointer]),
        ('archive_read_data', ctypes.c_ssize_t, [pointer, pointer, ctypes.c_size_t]),
        ('archive_read_free', ctypes.c_int, [pointer]),
    ):
        function = getattr(lib, name)
        function.restype, function.argtypes = result, args
    reader = lib.archive_read_new()
    try:
        lib.archive_read_support_filter_all(reader)
        lib.archive_read_support_format_all(reader)
        if lib.archive_read_open_filename(reader, bytes(archive), 10240) != 0:
            raise RuntimeError('Could not open SDK archive')
        entry, buffer = pointer(), ctypes.create_string_buffer(1024 * 1024)
        while True:
            status = lib.archive_read_next_header(reader, ctypes.byref(entry))
            if status == 1:
                break
            if status != 0:
                raise RuntimeError('Could not read SDK archive')
            raw = pathlib.Path(lib.archive_entry_pathname(entry).decode('utf-8'))
            if raw.is_absolute() or '..' in raw.parts:
                raise RuntimeError('Unsafe SDK archive path')
            path = destination / raw
            kind = lib.archive_entry_filetype(entry)
            if kind == 0o040000:
                path.mkdir(parents=True, exist_ok=True)
                continue
            if kind != 0o100000:
                raise RuntimeError('Unexpected SDK archive entry')
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('wb') as output:
                while True:
                    size = lib.archive_read_data(reader, buffer, len(buffer))
                    if size < 0:
                        raise RuntimeError('Could not extract SDK')
                    if size == 0:
                        break
                    output.write(buffer.raw[:size])
    finally:
        lib.archive_read_free(reader)


def acquire(arch, check_only=False):
    entry = json.loads(LOCK.read_text())[arch]
    destination = ROOT / '.deps' / ('ultralight-' + entry['platform'])
    marker = destination / '.callayers-sdk.json'
    if destination.exists():
        validate_sdk(destination, arch, entry)
        if not marker.is_file() or json.loads(marker.read_text()) != entry:
            raise RuntimeError(f'Existing SDK has no matching verification record. Preserve or move {destination} before retrying.')
        return destination
    if check_only:
        raise RuntimeError(f'The {arch} renderer SDK has not been acquired')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix='mac-renderer-') as temporary:
        temporary = pathlib.Path(temporary)
        url = 'https://ultralig.ht/api/v1/sdk/download?' + urllib.parse.urlencode({
            'platform': entry['platform'], 'version': entry['version'], 'format': 'json'})
        with urllib.request.urlopen(url, timeout=45) as response:
            metadata = json.load(response)
        if metadata.get('version') != entry['version'] or metadata.get('platform') != entry['platform']:
            raise RuntimeError('SDK download metadata does not match the pinned version')
        download = urllib.parse.urlparse(metadata['url'])
        if download.scheme != 'https' or download.hostname != 'ul-web-downloads.nyc3.digitaloceanspaces.com':
            raise RuntimeError('Unexpected SDK download origin')
        archive = temporary / 'sdk.7z'
        with urllib.request.urlopen(metadata['url'], timeout=180) as response, archive.open('wb') as output:
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                output.write(chunk)
        if sha256(archive) != entry['sha256']:
            raise RuntimeError('SDK archive checksum mismatch')
        unpacked = temporary / 'unpacked'
        unpacked.mkdir()
        extract(archive, unpacked)
        validate_sdk(unpacked, arch, entry)
        (unpacked / marker.name).write_text(json.dumps(entry, indent=2) + '\n')
        unpacked.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', choices=('x86_64', 'arm64'), required=True)
    parser.add_argument('--check', action='store_true', help='Validate an already acquired SDK without network access')
    args = parser.parse_args()
    try:
        print(acquire(args.arch, args.check))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'Renderer SDK: {error}\n')


if __name__ == '__main__':
    main()
