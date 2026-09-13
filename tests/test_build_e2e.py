"""Test end-to-end real: compila los ejemplos a un kernel.elf y verifica con
grub-file que el resultado sea multiboot válido de verdad.
Se salta automáticamente si el toolchain nativo (gcc-multilib, nasm, grub)
no está instalado en la máquina que corre los tests."""

import shutil
import subprocess
from pathlib import Path

import pytest

from pyos.build import build, check_toolchain

EXAMPLES = [
    Path(__file__).parent.parent / "examples" / "hello_kernel" / "kernel.py",
    Path(__file__).parent.parent / "examples" / "keyboard_kernel" / "kernel.py",
    Path(__file__).parent.parent / "examples" / "shell_kernel" / "kernel.py",
]

pytestmark = pytest.mark.skipif(
    bool(check_toolchain()) or shutil.which("grub-file") is None,
    reason="falta el toolchain nativo (gcc-multilib, nasm, grub-mkrescue) o grub-file",
)


@pytest.mark.parametrize("example", EXAMPLES, ids=lambda p: p.parent.name)
def test_kernel_builds_valid_multiboot(example, tmp_path):
    out_iso = tmp_path / "kernel.iso"
    work = tmp_path / "work"
    result = build(example, output=out_iso, work_dir=work, keep_work_dir=True)

    assert result.iso_path.is_file()
    assert result.kernel_elf_path.is_file()

    proc = subprocess.run(
        ["grub-file", "--is-x86-multiboot", str(result.kernel_elf_path)]
    )
    assert proc.returncode == 0, "el ELF generado no es un multiboot válido"


@pytest.mark.skipif(
    bool(check_toolchain(["gcc"])) or shutil.which("grub-file") is None,
    reason="falta gcc (multilib)",
)
def test_linux_init_builds_and_runs(tmp_path):
    """El mismo kernel.py compilado como /init de Linux (i386 estático, sin
    libc) debe correr como un proceso real del sistema host, imprimiendo a
    stdout lo mismo que dibujaría en VGA."""
    hello = Path(__file__).parent.parent / "examples" / "hello_kernel" / "kernel.py"
    out_init = tmp_path / "init"
    result = build(hello, output=out_init, target="linux-init")

    assert out_init.is_file()
    assert result.initrd_path is not None and result.initrd_path.is_file()

    proc = subprocess.run(
        [str(out_init)],
        timeout=15,
        input="", capture_output=True, text=True,
    )
    assert proc.returncode == 0, f"init murió mal: {proc.stderr}"
    assert "contador OK" in proc.stdout
    assert "pyos > listo" in proc.stdout


def test_linux_init_build_rejects_wrong_target(tmp_path):
    hello = Path(__file__).parent.parent / "examples" / "hello_kernel" / "kernel.py"
    with pytest.raises(Exception, match="target inválido"):
        build(hello, output=tmp_path / "x", target="nope")
