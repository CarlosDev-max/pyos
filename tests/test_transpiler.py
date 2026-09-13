"""Tests de pyos.transpiler — no requieren nasm/gcc/grub, solo Python."""

import pytest

from pyos.transpiler import Transpiler, TranspileError


def transpile(src: str) -> str:
    return Transpiler().transpile(src, filename="<test>")


def test_minimal_entry_required():
    with pytest.raises(TranspileError, match="pyos.entry"):
        transpile("def main():\n    pass\n")


def test_entry_no_args():
    src = "import pyos\n@pyos.entry\ndef main(x):\n    pass\n"
    with pytest.raises(TranspileError, match="no puede recibir argumentos"):
        transpile(src)


def test_only_one_entry():
    src = (
        "import pyos\n"
        "@pyos.entry\ndef a():\n    pass\n"
        "@pyos.entry\ndef b():\n    pass\n"
    )
    with pytest.raises(TranspileError, match="exactamente una"):
        transpile(src)


def test_basic_draw_and_clear():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.clear()\n"
        "    pyos.draw(\"hola\")\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "void pyos_entry(void)" in c
    assert 'pyos_draw("hola")' in c
    assert "pyos_clear()" in c
    assert "pyos_halt()" in c


def test_module_docstring_allowed():
    src = (
        '"""un docstring"""\n'
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "pyos_halt()" in c


def test_function_docstring_ignored():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        '    """doc de la funcion"""\n'
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "doc de la funcion" not in c
    assert "pyos_halt()" in c


def test_int_variable_and_arithmetic():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    x = 1\n"
        "    x = x + 2\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "int x = 1;" in c
    assert "x = (x + 2);" in c


def test_string_reassignment_type_error():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    x = 1\n"
        "    x = \"hola\"\n"
        "    pyos.halt()\n"
    )
    with pytest.raises(TranspileError, match="no se puede reasignar"):
        transpile(src)


def test_if_while_for_range():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    i = 0\n"
        "    while i < 3:\n"
        "        i = i + 1\n"
        "    if i == 3:\n"
        "        pyos.draw(\"ok\")\n"
        "    for j in range(2):\n"
        "        pyos.draw(\"loop\")\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "while ((i < 3))" in c
    assert "if ((i == 3))" in c
    assert "for (int j = 0; j < 2; j += 1)" in c


def test_helper_function_call():
    src = (
        "import pyos\n"
        "def suma(a, b):\n"
        "    return a + b\n"
        "@pyos.entry\n"
        "def main():\n"
        "    x = suma(1, 2)\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "static int suma(int a, int b)" in c
    assert "suma(1, 2)" in c


def test_unsupported_construct_reports_line():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    [x for x in range(3)]\n"
    )
    with pytest.raises(TranspileError, match="línea 4"):
        transpile(src)


def test_unknown_pyos_call_rejected():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.nope()\n"
    )
    with pytest.raises(TranspileError, match="no existe en el runtime"):
        transpile(src)


def test_top_level_statement_rejected():
    src = (
        "import pyos\n"
        "x = 5\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.halt()\n"
    )
    with pytest.raises(TranspileError, match="a nivel de módulo"):
        transpile(src)
