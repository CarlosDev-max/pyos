"""
pyos.build — toma un archivo Python escrito con la API de pyos y produce una
imagen .iso booteable real, usando un toolchain real (nasm + gcc + ld +
grub-mkrescue). No hay ningún paso simulado acá: cada archivo que se genera
es el binario que efectivamente arranca la máquina.

Requiere en el sistema (Linux): gcc (con soporte -m32), nasm, grub-mkrescue,
xorriso, mtools. `pyos doctor` (ver cli.py) chequea todo esto.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from .transpiler import Transpiler, TranspileError

_NATIVE_DIR = Path(__file__).parent / "_native"
_NATIVE_LINUX_DIR = Path(__file__).parent / "_native_linux"

_REQUIRED_TOOLS = ["gcc", "nasm", "grub-mkrescue", "xorriso"]
_LINUX_TOOLS = ["gcc"]


class BuildError(Exception):
    pass


@dataclass
class BuildResult:
    iso_path: Path
    kernel_elf_path: Path
    generated_c_path: Path
    initrd_path: Path | None = None


def check_toolchain(tools: list[str] | None = None) -> list[str]:
    """Devuelve la lista de herramientas requeridas que faltan en el sistema."""
    tools = tools or _REQUIRED_TOOLS
    return [t for t in tools if shutil.which(t) is None]


def _run(cmd: list[str], cwd: Path, log) -> None:
    log(f"  $ {' '.join(cmd)}")
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise BuildError(
            f"falló: {' '.join(cmd)}\n--- stdout ---\n{proc.stdout}\n"
            f"--- stderr ---\n{proc.stderr}"
        )
    if proc.stdout.strip():
        log(proc.stdout.rstrip())


def build(
    source_path: str | Path,
    output: str | Path = "pyos.iso",
    *,
    target: str = "iso",
    work_dir: str | Path | None = None,
    keep_work_dir: bool = False,
    extra_iso_files: dict[str, bytes] | None = None,
    log=print,
) -> BuildResult:
    """Compila `source_path` (un kernel.py escrito con la API de pyos).

    `target`:
      - "iso" (default): ISO booteable Multiboot para QEMU/PC real
      - "linux-init": binario estático i386 (sin libc) para usar como PID 1
        de un Linux, empaquetado también en un initramfs cpio/gzip.

    `extra_iso_files`: archivos adicionales {ruta_relativa_dentro_de_/boot:
    bytes} que se meten en la ISO, p. ej. {"saludo.txt": b"hola"} para poder
    leerlos desde el kernel con pyos.iso_read (el CD booteado).
    """
    source_path = Path(source_path).resolve()
    output = Path(output).resolve()

    if target not in ("iso", "linux-init"):
        raise BuildError(f"target inválido: {target!r} (usa 'iso' o 'linux-init')")

    if not source_path.is_file():
        raise BuildError(f"no existe el archivo fuente: {source_path}")

    if target == "linux-init":
        return _build_linux_init(
            source_path, output,
            work_dir=work_dir, keep_work_dir=keep_work_dir, log=log,
        )
    return _build_iso(
        source_path, output,
        work_dir=work_dir, keep_work_dir=keep_work_dir,
        extra_iso_files=extra_iso_files, log=log,
    )


def _build_iso(
    source_path: Path,
    output: Path,
    *,
    work_dir: str | Path | None,
    keep_work_dir: bool,
    extra_iso_files: dict[str, bytes] | None,
    log,
) -> BuildResult:
    """Compila a una ISO booteable real (bootloader multiboot + GRUB)."""
    missing = check_toolchain(_REQUIRED_TOOLS)
    if missing:
        raise BuildError(
            "faltan herramientas del sistema para compilar un kernel real: "
            + ", ".join(missing)
            + ". Instalalas con tu gestor de paquetes (ver README.md)."
        )

    source = source_path.read_text(encoding="utf-8")

    log(f"[1/6] Transpilando {source_path.name} → C ...")
    try:
        c_source = Transpiler().transpile(source, filename=str(source_path))
    except TranspileError as e:
        raise BuildError(f"error de transpilación: {e}") from e

    work = Path(work_dir).resolve() if work_dir else Path(
        _mkworkdir(source_path.stem)
    )
    work.mkdir(parents=True, exist_ok=True)
    try:
        generated_c = work / "generated.c"
        generated_c.write_text(c_source, encoding="utf-8")

        native_c_files = ("runtime.c", "heap.c", "paging.c", "speaker.c",
                      "rng.c", "ata.c", "myfs.c", "iso9660.c",
                      "timer.c", "proc.c", "math.c", "str2.c", "vga2.c",
                      "misc.c", "pci.c", "rtl8139.c", "net.c")
        for fname in (*native_c_files, "pyos_runtime.h", "boot.asm", "linker.ld"):
            shutil.copy(_NATIVE_DIR / fname, work / fname)

        log("[2/6] Ensamblando bootloader (boot.asm → boot.o) ...")
        _run(["nasm", "-f", "elf32", "boot.asm", "-o", "boot.o"], work, log)

        log("[3/6] Compilando runtime nativo y generated.c (freestanding, -m32) ...")
        cflags = [
            "-m32", "-ffreestanding", "-fno-pie", "-fno-stack-protector",
            "-fno-asynchronous-unwind-tables",
            "-Wall", "-Wextra", "-O2", "-c",
        ]
        object_files = ["boot.o"]
        for c_file in (*native_c_files, "generated.c"):
            obj = c_file.replace(".c", ".o")
            _run(["gcc", *cflags, c_file, "-o", obj], work, log)
            object_files.append(obj)

        log("[4/6] Linkeando kernel (kernel.elf, multiboot en 1MB) ...")
        _run(
            [
                "gcc", "-m32", "-ffreestanding", "-nostdlib", "-static",
                "-Wl,--build-id=none",
                "-T", "linker.ld",
                *object_files,
                "-o", "kernel.elf", "-lgcc",
            ],
            work, log,
        )

        kernel_elf = work / "kernel.elf"

        log("[5/6] Armando árbol de ISO (GRUB) ...")
        iso_root = work / "isoroot"
        grub_dir = iso_root / "boot" / "grub"
        grub_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy(kernel_elf, iso_root / "boot" / "kernel.elf")

        # El volumen con los archivos que el kernel podrá leer: se empaqueta
        # como un data.iso aparte y GRUB lo carga en memoria como módulo
        # multiboot (el kernel lo lee con pyos.iso_read/pyos.iso_ls).
        extra = extra_iso_files or {}
        if extra:
            data_root = work / "data_root"
            data_root.mkdir(parents=True, exist_ok=True)
            for rel_path, data in extra.items():
                # rutas tipo "boot/foo.txt" se normalizan a la raíz del volumen
                # de datos (que es donde el kernel busca con pyos.iso_read)
                rel_path = rel_path.lstrip("/")
                if rel_path.startswith("boot/"):
                    rel_path = rel_path[len("boot/"):]
                target = data_root / rel_path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                log(f"  extra: {rel_path} ({len(data)} B)")
            data_iso = iso_root / "boot" / "data.iso"
            _run(
                ["xorriso", "-as", "mkisofs", "-R", "-J",
                 "-o", str(data_iso), str(data_root)],
                work, log,
            )

        module_line = "    module /boot/data.iso\n" if extra else ""
        (grub_dir / "grub.cfg").write_text(
            'set timeout=0\n'
            'set default=0\n'
            'menuentry "pyos" {\n'
            '    multiboot /boot/kernel.elf\n'
            + module_line +
            '    boot\n'
            "}\n",
            encoding="utf-8",
        )

        log("[6/6] Generando ISO booteable ...")
        _run(
            ["grub-mkrescue", "-o", str(output), str(iso_root)],
            work, log,
        )

        log(f"\nListo: {output}")
        log(f"Probalo con: qemu-system-i386 -cdrom {output}")
        return BuildResult(
            iso_path=output, kernel_elf_path=kernel_elf, generated_c_path=generated_c
        )
    finally:
        if not keep_work_dir and not work_dir:
            shutil.rmtree(work, ignore_errors=True)


def _build_linux_init(
    source_path: Path,
    output: Path,
    *,
    work_dir: str | Path | None,
    keep_work_dir: bool,
    log,
) -> BuildResult:
    """Compila el mismo kernel.py a un /init estático (ELF i386, sin libc) y a
    un initramfs cpio+gzip listo para `qemu -initrd` o boot directo de Linux.

    Runtime: _native_linux/runtime_linux.c (syscalls int $0x80 directas).
    heap.c y rng.c son los mismos que en la ISO; speaker.c NO (es hardware de
    PC) — un kernel.py que llame pyos.beep() fallará acá en el link.
    """
    missing = check_toolchain(_LINUX_TOOLS)
    if missing:
        raise BuildError(
            "faltan herramientas del sistema para --target=linux-init: "
            + ", ".join(missing)
        )

    source = source_path.read_text(encoding="utf-8")

    log(f"[1/4] Transpilando {source_path.name} → C ...")
    try:
        c_source = Transpiler().transpile(source, filename=str(source_path))
    except TranspileError as e:
        raise BuildError(f"error de transpilación: {e}") from e

    work = Path(work_dir).resolve() if work_dir else Path(
        _mkworkdir(source_path.stem)
    )
    work.mkdir(parents=True, exist_ok=True)
    try:
        generated_c = work / "generated.c"
        generated_c.write_text(c_source, encoding="utf-8")

        for fname in ("pyos_runtime.h",):
            shutil.copy(_NATIVE_DIR / fname, work / fname)
        shutil.copy(_NATIVE_LINUX_DIR / "runtime_linux.c", work / "runtime_linux.c")
        for fname in ("heap.c", "rng.c", "math.c", "str2.c"):
            shutil.copy(_NATIVE_DIR / fname, work / fname)

        log("[2/4] Compilando runtime_linux.c + generated.c (estático, i386, sin libc) ...")
        cflags = [
            "-m32", "-ffreestanding", "-fno-pie", "-fno-stack-protector",
            "-fno-asynchronous-unwind-tables",
            "-Wall", "-Wextra", "-O2",
        ]

        log("[3/4] Linkeando el binario init ...")
        _run(
            ["gcc", *cflags, "-nostdlib",
             "generated.c", "runtime_linux.c", "heap.c", "rng.c",
             "math.c", "str2.c",
             "-o", str(output), "-lgcc"],
            work, log,
        )
        output.chmod(0o755)

        log("[4/4] Empaquetando initramfs (cpio newc + gzip) ...")
        from .cpio import build_initramfs_gz
        initrd = output.with_suffix(".cpio.gz")
        initrd.write_bytes(build_initramfs_gz({"init": output.read_bytes()}))

        log(f"\nListo: {output} (PID 1 de Linux) + {initrd}")
        log(f"Probalo en QEMU con: qemu-system-i386 -kernel vmlinuz "
            f"-initrd {initrd} -append \"rdinit=/init console=ttyS0\" "
            f"-nographic")
        return BuildResult(
            iso_path=output, kernel_elf_path=output, generated_c_path=generated_c,
            initrd_path=initrd,
        )
    finally:
        if not keep_work_dir and not work_dir:
            shutil.rmtree(work, ignore_errors=True)


def _mkworkdir(stem: str) -> Path:
    import tempfile
    return Path(tempfile.mkdtemp(prefix=f"pyos-build-{stem}-"))
