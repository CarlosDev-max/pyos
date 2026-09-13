"""Tests del CLI: `pyos new` (plantilla) y comandos básicos."""

from pathlib import Path

from pyos import cli


def test_new_creates_project_from_template(tmp_path):
    dest = tmp_path / "Mi Primer OS"
    rc = cli._cmd_new(type("A", (), {"directory": dest, "name": None})())

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
    rc = cli._cmd_new(type("A", (), {"directory": dest, "name": "Mi OS!"})())
    assert rc == 0
    assert "# Mi OS!" in (dest / "README.md").read_text(encoding="utf-8")


def test_new_refuses_existing(tmp_path, capsys):
    dest = tmp_path / "ya_existe"
    dest.mkdir()
    rc = cli._cmd_new(type("A", (), {"directory": dest, "name": None})())
    assert rc == 1
    assert "ya existe" in capsys.readouterr().err


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