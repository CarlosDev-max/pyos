"""Test end-to-end real: compila examples/hello_kernel a un kernel.elf y
verifica con grub-file que el resultado sea multiboot válido de verdad.
Se salta automáticamente si el toolchain nativo (gcc-multilib, nasm, grub)
no está instalado en la máquina que corre los tests."""

import shutil
import subprocess
from pathlib import Path

import pytest

from pyos.build import build, check_toolchain

EXAMPLE = Path(__file__).parent.parent / "examples" / "hello_kernel" / "kernel.py"

pytestmark = pytest.mark.skipif(
    bool(check_toolchain()) or shutil.which("grub-file") is None,
    reason="falta el toolchain nativo (gcc-multilib, nasm, grub-mkrescue) o grub-file",
)


def test_hello_kernel_builds_valid_multiboot(tmp_path):
    out_iso = tmp_path / "hello.iso"
    work = tmp_path / "work"
    result = build(EXAMPLE, output=out_iso, work_dir=work, keep_work_dir=True)

    assert result.iso_path.is_file()
    assert result.kernel_elf_path.is_file()

    proc = subprocess.run(
        ["grub-file", "--is-x86-multiboot", str(result.kernel_elf_path)]
    )
    assert proc.returncode == 0, "el ELF generado no es un multiboot válido"
