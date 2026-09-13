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


def test_keyboard_calls():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    n = pyos.readline()\n"
        "    pyos.putc(pyos.kbchar(0))\n"
        "    for i in range(n):\n"
        "        pyos.log_char(pyos.kbchar(i))\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "int n = pyos_readline();" in c
    assert "pyos_putc(pyos_kb_char(0));" in c
    assert "pyos_log_char(pyos_kb_char(i));" in c
    assert 'for (int i = 0; i < n; i += 1)' in c


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


def test_ord_folds_to_constant():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    c = pyos.kbchar(0)\n"
        "    if c == ord('h'):\n"
        "        pyos.draw(\"h\")\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "== 104" in c
    assert "ord(" not in c


def test_ord_rejects_non_char_literal():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.kbchar(ord('abc'))\n"
    )
    with pytest.raises(TranspileError, match="un carácter"):
        transpile(src)


def test_reboot_and_break_continue():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    i = 0\n"
        "    while True:\n"
        "        i = i + 1\n"
        "        if i == 3:\n"
        "            continue\n"
        "        if i == 5:\n"
        "            break\n"
        "    pyos.reboot()\n"
    )
    c = transpile(src)
    assert "while (1)" in c
    assert "continue;" in c
    assert "break;" in c
    assert "pyos_reboot();" in c


def test_shell_kernel_transpiles():
    from pathlib import Path
    shell = Path(__file__).parent.parent / "examples" / "shell_kernel" / "kernel.py"
    c = Transpiler().transpile(
        shell.read_text(encoding="utf-8"), filename="shell_kernel/kernel.py"
    )
    assert "void pyos_entry(void)" in c
    assert "pyos_readline();" in c
    assert "pyos_reboot();" in c
    assert "pyos_halt();" in c
    assert "pyos_fs_init()" in c          # v0.6: FS montado al boot
    assert "pyos_substr" in c             # y el truco de extraer argumentos
    assert "pyos_iso_read" in c           # lector del CD booteado


def test_string_concat():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    a = \"hola \"\n"
        "    b = a + \"mundo\"\n"
        "    pyos.draw(b)\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "pyos_concat(a, " in c


def test_str_builtin_converts_int():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    x = 5\n"
        "    pyos.draw(str(x))\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "pyos_int_to_str(x)" in c


def test_str_builtin_rejects_str_arg_is_noop():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    a = \"ya soy str\"\n"
        "    pyos.draw(str(a))\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "pyos_int_to_str" not in c


def test_string_equality_uses_streq():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    cmd = pyos.line()\n"
        "    if cmd == \"help\":\n"
        "        pyos.draw(\"ok\")\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "pyos_streq(cmd, \"help\")" in c


def test_string_notequal_negates_streq():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    cmd = pyos.line()\n"
        "    if cmd != \"help\":\n"
        "        pyos.draw(\"no\")\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "!pyos_streq(cmd, \"help\")" in c


def test_string_ordering_comparison_rejected():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    cmd = pyos.line()\n"
        "    if cmd < \"help\":\n"
        "        pyos.draw(\"no\")\n"
        "    pyos.halt()\n"
    )
    with pytest.raises(TranspileError, match="solo se soportan"):
        transpile(src)


def test_string_vs_int_comparison_rejected():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    cmd = pyos.line()\n"
        "    if cmd == 5:\n"
        "        pyos.draw(\"no\")\n"
        "    pyos.halt()\n"
    )
    with pytest.raises(TranspileError, match="no se puede comparar"):
        transpile(src)


def test_beep_and_random_int():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.beep(440, 100)\n"
        "    r = pyos.random_int(10)\n"
        "    pyos.draw(str(r))\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "pyos_beep(440, 100)" in c
    assert "pyos_random_int(10)" in c


def test_accented_string_maps_to_cp437():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.draw(\"informaci\u00f3n\")\n"
        "    pyos.halt()\n"
    )
    c = transpile(src)
    assert "\\242" in c   # 'ó' -> 0xA2 -> octal 242
    assert "informaci\u00f3n".encode("utf-8") not in c.encode("utf-8")


def test_fase5_spawn_sleep_uptime_ticks_ps():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    t = pyos.ticks()\n"
        "    u = pyos.uptime()\n"
        "    pyos.spawn(\"contador\", contador)\n"
        "    pyos.sleep(500)\n"
        "    pyos.ps()\n"
        "    pyos.halt()\n"
        "def contador():\n"
        "    pyos.sleep(200)\n"
    )
    c = transpile(src)
    assert "pyos_ticks()" in c
    assert "pyos_uptime()" in c
    assert 'pyos_spawn("contador", (void (*)(void))contador)' in c
    assert "pyos_sleep(500)" in c
    assert "pyos_ps()" in c


def test_fase5_spawn_forwards_function_expression():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.spawn(\"tarea\", ident)\n"
        "    pyos.halt()\n"
        "def ident():\n"
        "    pass\n"
    )
    c = transpile(src)
    assert 'pyos_spawn("tarea", (void (*)(void))ident)' in c


def test_fase5_spawn_requires_function():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.spawn(\"raro\", 42)\n"
        "    pyos.halt()\n"
    )
    with pytest.raises(TranspileError, match="función definida"):
        transpile(src)


def test_fase5_exit_task():
    src = (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        "    pyos.exit_task()\n"
    )
    c = transpile(src)
    assert "pyos_exit_task()" in c


def _c(body: str) -> str:
    """arma un kernel mínimo con una sola cuerpo de main"""
    return (
        "import pyos\n"
        "@pyos.entry\n"
        "def main():\n"
        + "\n".join("    " + line for line in body.splitlines())
        + "\n    pyos.halt()\n"
    )


# ---------------------------------------------------------------- Tanda A ---
def test_a_abs():
    c = transpile(_c("n = pyos.abs(-7)"))
    assert "pyos_abs(" in c

def test_a_sign():
    c = transpile(_c("n = pyos.sign(-3)"))
    assert "pyos_sign(" in c

def test_a_is_even():
    c = transpile(_c("n = pyos.is_even(10)"))
    assert "pyos_is_even(10)" in c

def test_a_is_odd():
    c = transpile(_c("n = pyos.is_odd(7)"))
    assert "pyos_is_odd(7)" in c

def test_a_pow():
    c = transpile(_c("n = pyos.pow(2, 3)"))
    assert "pyos_pow(2, 3)" in c

def test_a_factorial():
    c = transpile(_c("n = pyos.factorial(5)"))
    assert "pyos_factorial(5)" in c

def test_a_fib():
    c = transpile(_c("n = pyos.fib(10)"))
    assert "pyos_fib(10)" in c

def test_a_gcd():
    c = transpile(_c("n = pyos.gcd(48, 36)"))
    assert "pyos_gcd(48, 36)" in c

def test_a_lcm():
    c = transpile(_c("n = pyos.lcm(4, 6)"))
    assert "pyos_lcm(4, 6)" in c

def test_a_sqrt_int():
    c = transpile(_c("n = pyos.sqrt_int(17)"))
    assert "pyos_sqrt_int(17)" in c

def test_a_is_prime():
    c = transpile(_c("n = pyos.is_prime(17)"))
    assert "pyos_is_prime(17)" in c

def test_a_next_prime():
    c = transpile(_c("n = pyos.next_prime(20)"))
    assert "pyos_next_prime(20)" in c

def test_a_digit_count():
    c = transpile(_c("n = pyos.digit_count(1234)"))
    assert "pyos_digit_count(1234)" in c

def test_a_sum_digits():
    c = transpile(_c("n = pyos.sum_digits(1234)"))
    assert "pyos_sum_digits(1234)" in c

def test_a_reverse_int():
    c = transpile(_c("n = pyos.reverse_int(123)"))
    assert "pyos_reverse_int(123)" in c

def test_a_is_palindrome_int():
    c = transpile(_c("n = pyos.is_palindrome_int(12321)"))
    assert "pyos_is_palindrome_int(12321)" in c

def test_a_min():
    c = transpile(_c("n = pyos.min(3, 5)"))
    assert "pyos_min(3, 5)" in c

def test_a_max():
    c = transpile(_c("n = pyos.max(3, 5)"))
    assert "pyos_max(3, 5)" in c

def test_a_clamp():
    c = transpile(_c("n = pyos.clamp(7, 0, 5)"))
    assert "pyos_clamp(7, 0, 5)" in c

def test_a_is_power_of_two():
    c = transpile(_c("n = pyos.is_power_of_two(16)"))
    assert "pyos_is_power_of_two(16)" in c

def test_a_next_power_of_two():
    c = transpile(_c("n = pyos.next_power_of_two(17)"))
    assert "pyos_next_power_of_two(17)" in c

def test_a_log2_floor():
    c = transpile(_c("n = pyos.log2_floor(100)"))
    assert "pyos_log2_floor(100)" in c

def test_a_rand_range():
    c = transpile(_c("n = pyos.rand_range(1, 6)"))
    assert "pyos_rand_range(1, 6)" in c

def test_a_rand_bool():
    c = transpile(_c("n = pyos.rand_bool()"))
    assert "pyos_rand_bool()" in c

def test_a_roll():
    c = transpile(_c("n = pyos.roll(6)"))
    assert "pyos_roll(6)" in c

def test_a_math_return_type_str():
    c = transpile(_c("s = str(pyos.pow(2, 8))"))
    assert "pyos_int_to_str(pyos_pow(2, 8))" in c


# ---------------------------------------------------------------- Tanda B ---
def test_b_str_len():
    c = transpile(_c('n = pyos.str_len("hola")'))
    assert 'pyos_str_len("hola")' in c

def test_b_str_to_int():
    c = transpile(_c('n = pyos.str_to_int("42")'))
    assert 'pyos_str_to_int("42")' in c

def test_b_str_char_at():
    c = transpile(_c('n = pyos.str_char_at("hola", 1)'))
    assert "pyos_str_char_at(" in c

def test_b_str_contains():
    c = transpile(_c('n = pyos.str_contains("hola", "ol")'))
    assert 'pyos_str_contains("hola", "ol")' in c

def test_b_str_index():
    c = transpile(_c('n = pyos.str_index("hola", "la")'))
    assert 'pyos_str_index("hola", "la")' in c

def test_b_str_count():
    c = transpile(_c('n = pyos.str_count("aaa", "a")'))
    assert 'pyos_str_count("aaa", "a")' in c

def test_b_starts_with():
    c = transpile(_c('n = pyos.starts_with("hola", "ho")'))
    assert 'pyos_starts_with("hola", "ho")' in c

def test_b_ends_with():
    c = transpile(_c('n = pyos.ends_with("hola", "la")'))
    assert 'pyos_ends_with("hola", "la")' in c

def test_b_upper():
    c = transpile(_c('s = pyos.upper("hola")'))
    assert 'pyos_upper("hola")' in c
    assert "const char* s =" in c

def test_b_lower():
    c = transpile(_c('s = pyos.lower("HOLA")'))
    assert 'pyos_lower("HOLA")' in c

def test_b_reverse():
    c = transpile(_c('s = pyos.reverse("hola")'))
    assert 'pyos_reverse("hola")' in c

def test_b_trim():
    c = transpile(_c('s = pyos.trim("  hola  ")'))
    assert 'pyos_trim("  hola  ")' in c

def test_b_repeat():
    c = transpile(_c('s = pyos.repeat("ab", 3)'))
    assert 'pyos_repeat("ab", 3)' in c

def test_b_left():
    c = transpile(_c('s = pyos.left("hola", 2)'))
    assert 'pyos_left("hola", 2)' in c

def test_b_right():
    c = transpile(_c('s = pyos.right("hola", 2)'))
    assert 'pyos_right("hola", 2)' in c

def test_b_pad_left():
    c = transpile(_c('s = pyos.pad_left("7", 3, ord(\'0\'))'))
    assert 'pyos_pad_left("7", 3, 48)' in c

def test_b_pad_right():
    c = transpile(_c('s = pyos.pad_right("7", 3, ord(\'0\'))'))
    assert 'pyos_pad_right("7", 3, 48)' in c

def test_b_replace_char():
    c = transpile(_c('s = pyos.replace_char("a-b", ord(\'-\'), ord(\'_\'))'))
    assert 'pyos_replace_char("a-b", 45, 95)' in c

def test_b_int_to_hex():
    c = transpile(_c('s = pyos.int_to_hex(255)'))
    assert "pyos_int_to_hex(255)" in c

def test_b_int_to_bin():
    c = transpile(_c('s = pyos.int_to_bin(5)'))
    assert "pyos_int_to_bin(5)" in c


# ---------------------------------------------------------------- Tanda C ---
def test_c_gotoxy():
    c = transpile(_c("pyos.gotoxy(10, 5)"))
    assert "pyos_gotoxy(10, 5)" in c

def test_c_get_x():
    c = transpile(_c("n = pyos.get_x()"))
    assert "pyos_get_x()" in c

def test_c_get_y():
    c = transpile(_c("n = pyos.get_y()"))
    assert "pyos_get_y()" in c

def test_c_set_color():
    c = transpile(_c("n = pyos.set_color(15, 0)"))
    assert "pyos_set_color(15, 0)" in c

def test_c_get_color():
    c = transpile(_c("n = pyos.get_color()"))
    assert "pyos_get_color()" in c

def test_c_draw_char():
    c = transpile(_c("pyos.draw_char(1, 2, ord('*'))"))
    assert "pyos_draw_char(1, 2, 42)" in c

def test_c_draw_at():
    c = transpile(_c('pyos.draw_at(0, 0, "hola")'))
    assert 'pyos_draw_at(0, 0, "hola")' in c

def test_c_clr_row():
    c = transpile(_c("n = pyos.clr_row(3)"))
    assert "pyos_clr_row(3)" in c

def test_c_fill_screen():
    c = transpile(_c("pyos.fill_screen(ord(' '))"))
    assert "pyos_fill_screen(32)" in c

def test_c_hline():
    c = transpile(_c("pyos.hline(5, 0, 79, ord('-'))"))
    assert "pyos_hline(5, 0, 79, 45)" in c

def test_c_vline():
    c = transpile(_c("pyos.vline(10, 0, 24, ord('|'))"))
    assert "pyos_vline(10, 0, 24, 124)" in c

def test_c_box():
    c = transpile(_c("pyos.box(0, 0, 5, 5, ord('#'))"))
    assert "pyos_box(0, 0, 5, 5, 35)" in c

def test_c_fill_rect():
    c = transpile(_c("pyos.fill_rect(1, 1, 4, 4, ord('.'))"))
    assert "pyos_fill_rect(1, 1, 4, 4, 46)" in c

def test_c_screen_w():
    c = transpile(_c("n = pyos.screen_w()"))
    assert "pyos_screen_w()" in c

def test_c_screen_h():
    c = transpile(_c("n = pyos.screen_h()"))
    assert "pyos_screen_h()" in c

def test_c_cursor_show():
    c = transpile(_c("pyos.cursor_show(1)"))
    assert "pyos_cursor_show(1)" in c

def test_c_invert_row():
    c = transpile(_c("n = pyos.invert_row(0)"))
    assert "pyos_invert_row(0)" in c


# ---------------------------------------------------------------- Tanda D ---
def test_d_key_available():
    c = transpile(_c("n = pyos.key_available()"))
    assert "pyos_key_available()" in c

def test_d_getc_nowait():
    c = transpile(_c("n = pyos.getc_nowait()"))
    assert "pyos_getc_nowait()" in c

def test_d_getc():
    c = transpile(_c("n = pyos.getc()"))
    assert "pyos_getc()" in c

def test_d_clear_kb():
    c = transpile(_c("n = pyos.clear_kb()"))
    assert "pyos_clear_kb()" in c

def test_d_shift_pressed():
    c = transpile(_c("n = pyos.shift_pressed()"))
    assert "pyos_shift_pressed()" in c

def test_d_caps_active():
    c = transpile(_c("n = pyos.caps_active()"))
    assert "pyos_caps_active()" in c

def test_d_millis():
    c = transpile(_c("n = pyos.millis()"))
    assert "pyos_millis()" in c

def test_d_seconds():
    c = transpile(_c("n = pyos.seconds()"))
    assert "pyos_seconds()" in c

def test_d_getpid():
    c = transpile(_c("n = pyos.getpid()"))
    assert "pyos_getpid()" in c

def test_d_task_count():
    c = transpile(_c("n = pyos.task_count()"))
    assert "pyos_task_count()" in c

def test_d_task_alive():
    c = transpile(_c("n = pyos.task_alive(0)"))
    assert "pyos_task_alive(0)" in c

def test_d_task_name():
    c = transpile(_c('s = pyos.task_name(pyos.getpid())'))
    assert "pyos_task_name(" in c

def test_d_task_state_str():
    c = transpile(_c('s = pyos.task_state_str(pyos.getpid())'))
    assert "pyos_task_state_str(" in c

def test_d_self_name():
    c = transpile(_c("s = pyos.self_name()"))
    assert "pyos_self_name()" in c

def test_d_kill():
    c = transpile(_c("n = pyos.kill(0)"))
    assert "pyos_kill(0)" in c


# ---------------------------------------------------------------- Tanda E ---
def test_e_paging_enabled():
    c = transpile(_c("n = pyos.paging_enabled()"))
    assert "pyos_paging_enabled()" in c

def test_e_mem_total():
    c = transpile(_c("n = pyos.mem_total()"))
    assert "pyos_mem_total()" in c

def test_e_mem_heap_blocks():
    c = transpile(_c("n = pyos.mem_heap_blocks()"))
    assert "pyos_mem_heap_blocks()" in c

def test_e_cpu_vendor():
    c = transpile(_c("s = pyos.cpu_vendor()"))
    assert "pyos_cpu_vendor()" in c
    assert "const char* s =" in c

def test_e_cpu_has_fpu():
    c = transpile(_c("n = pyos.cpu_has_fpu()"))
    assert "pyos_cpu_has_fpu()" in c

def test_e_kernel_base():
    c = transpile(_c("n = pyos.kernel_base()"))
    assert "pyos_kernel_base()" in c

def test_e_iso_count():
    c = transpile(_c("n = pyos.iso_count()"))
    assert "pyos_iso_count()" in c

def test_e_version_string():
    c = transpile(_c("s = pyos.version_string()"))
    assert "pyos_version_string()" in c


# ---------------------------------------------------------------- Tanda F ---
def test_f_toupper_char():
    c = transpile(_c("n = pyos.toupper_char(ord('a'))"))
    assert "pyos_toupper_char(97)" in c

def test_f_tolower_char():
    c = transpile(_c("n = pyos.tolower_char(ord('A'))"))
    assert "pyos_tolower_char(65)" in c

def test_f_is_digit():
    c = transpile(_c("n = pyos.is_digit(ord('5'))"))
    assert "pyos_is_digit(53)" in c

def test_f_is_alpha():
    c = transpile(_c("n = pyos.is_alpha(ord('z'))"))
    assert "pyos_is_alpha(122)" in c

def test_f_rand_str():
    c = transpile(_c("s = pyos.rand_str(8)"))
    assert "pyos_rand_str(8)" in c

def test_f_fs_mounted():
    c = transpile(_c("n = pyos.fs_mounted()"))
    assert "pyos_fs_mounted()" in c

def test_f_fopen_append():
    c = transpile(_c("n = pyos.fopen_append(3, \"mas\")"))
    assert 'pyos_fopen_append(3, "mas")' in c
