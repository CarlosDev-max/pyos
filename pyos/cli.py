"""pyos.cli — comando `pyos`."""

from __future__ import annotations

import argparse
import re
import runpy
import shutil
import sys
from pathlib import Path

from .build import BuildError, build, check_toolchain

_TEMPLATES_DIR = Path(__file__).parent / "templates"
_TEMPLATES = sorted(d.name for d in _TEMPLATES_DIR.iterdir() if d.is_dir())


def _slugify(name: str) -> str:
    return re.sub(r"[^a-z0-9_-]+", "-", name.lower()).strip("-") or "myos"


def _cmd_new(args: argparse.Namespace) -> int:
    dest = Path(args.directory)
    if dest.exists():
        print(f"error: ya existe: {dest}", file=sys.stderr)
        return 1

    tpl_dir = _TEMPLATES_DIR / args.template
    if not tpl_dir.is_dir():
        print(f"error: plantilla desconocida '{args.template}'. "
              f"Disponibles: {', '.join(_TEMPLATES)}", file=sys.stderr)
        return 1

    name = args.name or dest.name
    slug = _slugify(name)

    dest.mkdir(parents=True)
    try:
        for tpl in sorted(tpl_dir.iterdir()):
            if not tpl.is_file():
                continue
            text = tpl.read_text(encoding="utf-8")
            text = text.replace("__MYOS_NAME__", name).replace("__MYOS_SLUG__", slug)
            (dest / tpl.name).write_text(text, encoding="utf-8")
    except Exception as e:
        shutil.rmtree(dest, ignore_errors=True)
        print(f"error: no se pudo crear el proyecto: {e}", file=sys.stderr)
        return 1

    print(f"Creado {dest}/  (plantilla: {args.template})")
    print(f"  - {dest / 'kernel.py'}   (tu OS, editá este archivo)")
    print(f"  - {dest / 'README.md'}")
    print("\nCorré la lógica en simulación:")
    print(f"  pyos simulate {dest / 'kernel.py'}")
    print("Y compile una ISO real:")
    print(f"  pyos build {dest / 'kernel.py'} -o {slug}.iso")
    return 0


def _cmd_build(args: argparse.Namespace) -> int:
    try:
        build(
            args.source,
            output=args.output,
            target=args.target,
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

    p_new = sub.add_parser("new", help="Crea un proyecto de pyos a partir de una plantilla")
    p_new.add_argument("directory", help="Directorio del nuevo proyecto")
    p_new.add_argument("--name", default=None, help="Nombre del OS (por defecto, el del directorio)")
    p_new.add_argument("--template", "-t", default="basic", choices=_TEMPLATES,
                       help=f"Plantilla a usar: {', '.join(_TEMPLATES)} (default: basic)")
    p_new.set_defaults(func=_cmd_new)

    p_build = sub.add_parser("build", help="Compila un kernel.py a una ISO booteable (o /init de Linux)")
    p_build.add_argument("source", help="Archivo .py escrito con la API de pyos")
    p_build.add_argument("-o", "--output", default="pyos.iso", help="Ruta de la ISO de salida")
    p_build.add_argument("--target", choices=["iso", "linux-init"], default="iso",
                          help="'iso' (default): ISO Multiboot; 'linux-init': /init de Linux (PID 1)")
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
