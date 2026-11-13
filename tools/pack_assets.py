#!/usr/bin/env python3
"""Packs config/ and assets/ of the source tree into one archive (lib_core/utils/asset_pack.h).

    pack_assets.py SOURCE_ROOT OUTPUT

Layout, little-endian: magic, u64 index size, the index (per file: u16 path length, the
path as UTF-8 under SOURCE_ROOT with '/', u64 offset from the archive's start, u64 size),
then the files' bytes. The archive is left alone when it is newer than every input and lists exactly the same files.
"""

import struct
import sys
from pathlib import Path

MAGIC = b"Z13PACK\x01"
DIRECTORIES = ("config", "assets")


def input_files(root: Path) -> list[Path]:
    files = [p for d in DIRECTORIES for p in sorted((root / d).rglob("*")) if p.is_file()]
    return sorted(files, key=lambda p: p.relative_to(root).as_posix())


def packed_files(output: Path) -> list[tuple[bytes, int]] | None:
    """The (path, size) of every file the archive holds, or None when it is unreadable."""
    with output.open("rb") as archive:
        head = archive.read(len(MAGIC) + 8)
        if len(head) < len(MAGIC) + 8 or not head.startswith(MAGIC):
            return None
        (index_size,) = struct.unpack("<Q", head[len(MAGIC):])
        index = archive.read(index_size)
    entries, position = [], 0
    while position < len(index):
        (length,) = struct.unpack_from("<H", index, position)
        path = index[position + 2:position + 2 + length]
        _, size = struct.unpack_from("<QQ", index, position + 2 + length)
        entries.append((path, size))
        position += 2 + length + 16
    return entries


def is_current(output: Path, root: Path, files: list[Path], script: Path) -> bool:
    """Newer than every input, and holding exactly these files (a deleted input leaves it newer)."""
    if not output.exists():
        return False
    built = output.stat().st_mtime
    if not all(p.stat().st_mtime <= built for p in [script, *files]):
        return False
    wanted = [(p.relative_to(root).as_posix().encode("utf-8"), p.stat().st_size) for p in files]
    return packed_files(output) == wanted


def pack(root: Path, output: Path) -> bool:
    files = input_files(root)
    if is_current(output, root, files, Path(__file__)):
        return False
    paths = [p.relative_to(root).as_posix().encode("utf-8") for p in files]
    index_size = sum(2 + len(path) + 16 for path in paths)
    offset = len(MAGIC) + 8 + index_size
    index = bytearray()
    for path, file in zip(paths, files):
        size = file.stat().st_size
        index += struct.pack("<H", len(path)) + path + struct.pack("<QQ", offset, size)
        offset += size
    temporary = output.with_name(output.name + ".tmp")
    output.parent.mkdir(parents=True, exist_ok=True)
    with temporary.open("wb") as out:
        out.write(MAGIC + struct.pack("<Q", index_size) + index)
        for file in files:
            out.write(file.read_bytes())
    temporary.replace(output)
    return True


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    rebuilt = pack(Path(sys.argv[1]), Path(sys.argv[2]))
    print(f"{sys.argv[2]}: {'packed' if rebuilt else 'up to date'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
