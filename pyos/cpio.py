"""pyos.cpio — generador de archivos cpio en formato "newc"/SVR4.

Solo se necesita para `pyos build --target=linux-init`: empaqueta el binario
init (estático, sin libc) en un initramfs mínimo que un kernel Linux descom-
prime y ejecuta como PID 1. No usa herramientas externas (ni cpio ni gzip):
todo es Python puro.
"""

from __future__ import annotations

import gzip


def cpio_newc_entry(path: str, data: bytes, mode: int = 0o100755, ino: int = 1) -> bytes:
    """Codifica una entrada del formato newc (header fijo de 110 bytes)."""
    header = struct_pack(
        b"070701",
        ino,
        mode,
        0,            # uid
        0,            # gid
        1,            # nlink
        0,            # mtime
        len(data),
        0,            # devmajor
        0,            # devminor
        0,            # rdevmajor
        0,            # rdevminor
        len(path) + 1,  # c_namesize (incluye el '\0')
        0,            # c_check
    )
    name = path.encode("utf-8") + b"\0"
    out = header + name
    out += b"\0" * ((-len(name)) & 3)          # pad name a 4
    out += data
    out += b"\0" * ((-len(data)) & 3)          # pad data a 4
    return out


def struct_pack(magic: bytes, ino: int, mode: int, uid: int, gid: int,
                nlink: int, mtime: int, filesize: int, devmajor: int,
                devminor: int, rdevmajor: int, rdevminor: int,
                namesize: int, check: int) -> bytes:
    hexfields = (ino, mode, uid, gid, nlink, mtime, filesize,
                 devmajor, devminor, rdevmajor, rdevminor, namesize, check)
    return b"".join([magic] + [b"%08X" % f for f in hexfields])


def build_initramfs(files: dict[str, bytes]) -> bytes:
    """Empaqueta {ruta: contenido} en un archivo cpio newc con su trailer."""
    out = b""
    for i, (path, data) in enumerate(files.items(), start=1):
        out += cpio_newc_entry(path, data, mode=0o100755, ino=i)
    out += cpio_newc_entry("TRAILER!!!", b"", mode=0, ino=0)
    return out


def build_initramfs_gz(files: dict[str, bytes]) -> bytes:
    return gzip.compress(build_initramfs(files))