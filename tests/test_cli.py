"""Tests del CLI: `pyos new` (plantilla) y comandos básicos."""

import argparse
from pathlib import Path

from pyos import cli


def _new_args(dest, name=None, template="basic"):
    return argparse.Namespace(directory=dest, name=name, template=template)


def test_new_creates_project_from_template(tmp_path):
    dest = tmp_path / "Mi Primer OS"
    rc = cli._cmd_new(_new_args(dest))

    assert rc == 0
    kernel = dest / "kernel.py"
    readme = dest / "README.md"
    assert kernel.is_file() and readme.is_file()

    text = kernel.read_text(encoding="utf-8")
    assert "Mi Primer OS" in text
    assert "__MYOS_NAME__" not in text
    assert "pyos." in text


def test_new_slugify_name(tmp_path):
    dest = tmp_path / "Mi OS"
    rc = cli._cmd_new(_new_args(dest, name="Mi OS!"))
    assert rc == 0
    assert "# Mi OS!" in (dest / "README.md").read_text(encoding="utf-8")


def test_new_refuses_existing(tmp_path, capsys):
    dest = tmp_path / "ya_existe"
    dest.mkdir()
    rc = cli._cmd_new(_new_args(dest))
    assert rc == 1
    assert "ya existe" in capsys.readouterr().err


def test_new_with_template_multitask(tmp_path):
    dest = tmp_path / "os_multi"
    rc = cli._cmd_new(_new_args(dest, template="multitask"))
    assert rc == 0
    text = (dest / "kernel.py").read_text(encoding="utf-8")
    assert "pyos.spawn" in text
    assert "pyos.ps()" in text


def test_new_with_template_graphics(tmp_path):
    dest = tmp_path / "os_vga"
    rc = cli._cmd_new(_new_args(dest, template="graphics"))
    assert rc == 0
    text = (dest / "kernel.py").read_text(encoding="utf-8")
    assert "pyos.box" in text
    assert "pyos.gotoxy" in text


def test_new_with_unknown_template_is_rejected(tmp_path, capsys):
    dest = tmp_path / "os_raro"
    rc = cli._cmd_new(_new_args(dest, template="noexiste"))
    assert rc == 1
    assert "plantilla desconocida" in capsys.readouterr().err


def test_all_bundled_templates_transpile():
    from pyos.transpiler import Transpiler

    for tpl_dir in cli._TEMPLATES_DIR.iterdir():
        if not tpl_dir.is_dir():
            continue
        kernel = tpl_dir / "kernel.py"
        if not kernel.is_file():
            continue
        src = kernel.read_text(encoding="utf-8")
        c = Transpiler().transpile(src, filename=str(kernel))
        assert "void pyos_entry(void)" in c


def test_build_rejects_bad_target(tmp_path):
    from pyos.build import BuildError, build

    src = tmp_path / "k.py"
    src.write_text("import pyos\n@pyos.entry\ndef main():\n    pyos.halt()\n", encoding="utf-8")
    try:
        build(src, output=tmp_path / "x.iso", target="bogus")
    except BuildError as e:
        assert "target inválido" in str(e)
    else:
        raise AssertionError("debería haber fallado con target inválido")