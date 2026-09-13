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

_REQUIRED_TOOLS = ["gcc", "nasm", "grub-mkrescue", "xorriso"]


class BuildError(Exception):
    pass


@dataclass
class BuildResult:
    iso_path: Path
    kernel_elf_path: Path
    generated_c_path: Path


def check_toolchain() -> list[str]:
    """Devuelve la lista de herramientas requeridas que faltan en el sistema."""
    return [t for t in _REQUIRED_TOOLS if shutil.which(t) is None]


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
    work_dir: str | Path | None = None,
    keep_work_dir: bool = False,
    log=print,
) -> BuildResult:
    """Compila `source_path` (un kernel.py escrito con la API de pyos) a una
    ISO booteable real en `output`."""
    source_path = Path(source_path).resolve()
    output = Path(output).resolve()

    missing = check_toolchain()
    if missing:
        raise BuildError(
            "faltan herramientas del sistema para compilar un kernel real: "
            + ", ".join(missing)
            + ". Instalalas con tu gestor de paquetes (ver README.md)."
        )

    if not source_path.is_file():
        raise BuildError(f"no existe el archivo fuente: {source_path}")

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

        native_c_files = ("runtime.c", "heap.c", "speaker.c", "rng.c")
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
        (grub_dir / "grub.cfg").write_text(
            'set timeout=0\n'
            'set default=0\n'
            'menuentry "pyos" {\n'
            '    multiboot /boot/kernel.elf\n'
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


def _mkworkdir(stem: str) -> Path:
    import tempfile
    return Path(tempfile.mkdtemp(prefix=f"pyos-build-{stem}-"))
