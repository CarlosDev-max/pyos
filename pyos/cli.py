"""pyos.cli — comando `pyos`."""

from __future__ import annotations

import argparse
import runpy
import sys

from .build import BuildError, build, check_toolchain


def _cmd_build(args: argparse.Namespace) -> int:
    try:
        build(
            args.source,
            output=args.output,
            keep_work_dir=args.keep_work_dir,
            work_dir=args.work_dir,
        )
    except BuildError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


def _cmd_doctor(_args: argparse.Namespace) -> int:
    missing = check_toolchain()
    if not missing:
        print("OK: nasm, gcc, grub-mkrescue y xorriso están instalados.")
        return 0
    print("Faltan herramientas para compilar un kernel real:")
    for tool in missing:
        print(f"  - {tool}")
    print("\nEn Ubuntu/Debian: sudo apt-get install nasm xorriso grub-pc-bin "
          "grub-common mtools gcc")
    return 1


def _cmd_simulate(args: argparse.Namespace) -> int:
    """Corre el kernel.py bajo CPython normal (modo simulación), sin compilar
    nada — útil para probar la lógica antes de generar la ISO real."""
    runpy.run_path(args.source, run_name="__main__")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="pyos", description=(
        "Framework para escribir sistemas operativos reales en Python: "
        "transpila a C, compila con un bootloader multiboot2 y genera una "
        "ISO booteable de verdad."
    ))
    sub = parser.add_subparsers(dest="command", required=True)

    p_build = sub.add_parser("build", help="Compila un kernel.py a una ISO booteable")
    p_build.add_argument("source", help="Archivo .py escrito con la API de pyos")
    p_build.add_argument("-o", "--output", default="pyos.iso", help="Ruta de la ISO de salida")
    p_build.add_argument("--keep-work-dir", action="store_true",
                          help="No borrar los archivos intermedios (boot.o, kernel.elf, etc.)")
    p_build.add_argument("--work-dir", default=None,
                          help="Directorio de trabajo a usar (por defecto, uno temporal)")
    p_build.set_defaults(func=_cmd_build)

    p_doctor = sub.add_parser("doctor", help="Chequea que el toolchain de compilación esté instalado")
    p_doctor.set_defaults(func=_cmd_doctor)

    p_sim = sub.add_parser("simulate", help="Corre el kernel.py con CPython normal, sin compilar")
    p_sim.add_argument("source", help="Archivo .py a simular")
    p_sim.set_defaults(func=_cmd_simulate)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
