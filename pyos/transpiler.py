"""
pyos.transpiler — convierte un subconjunto restringido de Python a C.

Por qué existe esto: no hay forma de correr CPython (ni ningún intérprete
completo) sin un sistema operativo debajo — CPython necesita malloc, threads,
manejo de memoria virtual, etc. Como el objetivo de pyos es generar un kernel
que arranca en hardware real (o QEMU) sin Linux debajo, la única opción real
es compilar la lógica del usuario a código nativo. Este módulo hace esa
traducción para un subconjunto deliberadamente chico de Python — lo
suficiente para escribir lógica de kernel simple (drivers, shells, loops de
estado) sin arrastrar todo el runtime de CPython.

Subconjunto soportado (ver README.md para la lista completa y ejemplos):
  - Funciones top-level, sin closures ni clases
  - Tipos: int, str (str como literal, como resultado de concatenar dos
    str con '+', o de convertir un int con str(x))
  - if / elif / else, while, for x in range(...)
  - Operadores: + - * // % en enteros; comparaciones; and / or
  - Comparación == / != entre strings (pyos_streq); + entre strings concatena
  - Llamadas a pyos.draw / pyos.clear / pyos.halt / pyos.reboot / pyos.log /
    pyos.putc / pyos.putdec / pyos.log_char / pyos.readline / pyos.kbchar /
    pyos.line / pyos.beep / pyos.random_int (y memoria: pyos.strdup /
    pyos.free / pyos.heap_total / pyos.heap_used / pyos.heap_free)
  - ord('x') como constante de tiempo de compilación (útil para comparar el
    código ASCII de teclas contra literales)
  - Llamadas a otras funciones definidas en el mismo archivo
  - Una función marcada con @pyos.entry (se convierte en pyos_entry, la que
    el runtime en C invoca desde kernel_main)

Cualquier construcción fuera de esto (clases, imports que no sean pyos,
comprensiones, f-strings, excepciones, generadores, etc.) levanta
TranspileError con la línea exacta del archivo original, en vez de fallar
en silencio o generar C incorrecto.
"""

from __future__ import annotations

import ast
import unicodedata
from dataclasses import dataclass, field


class TranspileError(Exception):
    def __init__(self, msg: str, node: ast.AST | None = None):
        if node is not None:
            msg = f"línea {getattr(node, 'lineno', '?')}: {msg}"
        super().__init__(msg)


# Español -> CP437 (la página de código que usa la VGA en modo texto).
# Cubre los acentos y símbolos más comunes; lo que no está acá se resuelve
# sacándole el acento o, si no se puede, con un '?'.
_CP437_MAP = {
    "á": 0xA0, "é": 0x82, "í": 0xA1, "ó": 0xA2, "ú": 0xA3,
    "ñ": 0xA4, "Ñ": 0xA5, "ü": 0x81, "Ü": 0x9A,
    "¿": 0xA8, "¡": 0xAD, "É": 0x90,
}


_RUNTIME_CALLS = {
    # nombre en pyos.* : (función real en C, tipo que devuelve)
    "draw": ("pyos_draw", "int"),
    "clear": ("pyos_clear", "int"),
    "halt": ("pyos_halt", "int"),
    "log": ("pyos_log", "int"),
    "log_char": ("pyos_log_char", "int"),
    "putc": ("pyos_putc", "int"),
    "putdec": ("pyos_putdec", "int"),       # pyos.putdec(n): imprime un número
    "readline": ("pyos_readline", "int"),
    "kbchar": ("pyos_kb_char", "int"),
    "reboot": ("pyos_reboot", "int"),
    "line": ("pyos_line", "str"),          # la última línea leída, como string
    "beep": ("pyos_beep", "int"),          # pyos.beep(frecuencia_hz, ms)
    "random_int": ("pyos_random_int", "int"),  # pyos.random_int(n) -> 0..n-1
    # Fase 3 — memoria dinámica real (heap.c)
    "free": ("pyos_free", "int"),          # pyos.free(ptr_resultado_de_strdup)
    "strdup": ("pyos_strdup", "str"),      # copia un string al heap
    "heap_total": ("pyos_heap_total", "int"),
    "heap_used": ("pyos_heap_used", "int"),
    "heap_free": ("pyos_heap_free", "int"),
    # Fase 4 — filesystem (myfs.c) + strings
    "substr": ("pyos_substr", "str"),     # pyos.substr(start, len)
    "fsinit": ("pyos_fs_init", "int"),    # alias histórico; usar fs_init()
    "fs_init": ("pyos_fs_init", "int"),    # monta (o formatea) el disco
    "fopen": ("pyos_fopen", "int"),       # pyos.fopen(nombre, 0|1)
    "fwrite": ("pyos_fwrite", "int"),     # pyos.fwrite(fd, "texto")
    "fread": ("pyos_fread", "str"),       # pyos.fread(fd, max) -> str
    "fclose": ("pyos_fclose", "int"),
    "fexists": ("pyos_fexists", "int"),
    "fdel": ("pyos_fdel", "int"),
    "fls": ("pyos_fls", "int"),
    "fsize": ("pyos_fsize", "int"),
    "fs_status": ("pyos_fs_status", "int"),  # imprime el estado del FS montado

    # Fase 6: red (rtl8139 + ARP + IP + ICMP)
    "net_init": ("pyos_net_init", "int"),
    "net_ready": ("pyos_net_ready", "int"),
    "net_status": ("pyos_net_status", "int"),
    "my_ip": ("pyos_my_ip", "str"),
    "scan": ("pyos_scan", "int"),          # barrido ARP de la /24 local
    "ping": ("pyos_ping", "int"),          # pyos.ping("10.0.2.2") -> 1/0
    # Fase 4 — lector del CD booteado (iso9660.c)
    "iso_status": ("pyos_iso_status", "int"),
    "iso_ls": ("pyos_iso_ls", "int"),
    "iso_read": ("pyos_iso_read", "str"),   # pyos.iso_read("archivo") -> str
    "iso_free": ("pyos_iso_free", "int"),   # libera el resultado de iso_read
    # Fase 5 — multitarea preemptiva (timer.c + proc.c)
    "spawn": ("pyos_spawn", "int"),         # pyos.spawn("nombre", funcion)
    "exit_task": ("pyos_exit_task", "int"), # terminó el proceso actual
    "sleep": ("pyos_sleep", "int"),         # pyos.sleep(ms) suspende el proceso
    "ps": ("pyos_ps", "int"),               # lista procesos
    "uptime": ("pyos_uptime", "int"),       # tiempo encendido en ticks
    "ticks": ("pyos_ticks", "int"),         # total de ticks del PIT
    # Tanda A — matemática entera (math.c)
    "abs": ("pyos_abs", "int"),
    "sign": ("pyos_sign", "int"),
    "is_even": ("pyos_is_even", "int"),
    "is_odd": ("pyos_is_odd", "int"),
    "pow": ("pyos_pow", "int"),
    "factorial": ("pyos_factorial", "int"),
    "fib": ("pyos_fib", "int"),
    "gcd": ("pyos_gcd", "int"),
    "lcm": ("pyos_lcm", "int"),
    "sqrt_int": ("pyos_sqrt_int", "int"),
    "is_prime": ("pyos_is_prime", "int"),
    "next_prime": ("pyos_next_prime", "int"),
    "digit_count": ("pyos_digit_count", "int"),
    "sum_digits": ("pyos_sum_digits", "int"),
    "reverse_int": ("pyos_reverse_int", "int"),
    "is_palindrome_int": ("pyos_is_palindrome_int", "int"),
    "min": ("pyos_min", "int"),
    "max": ("pyos_max", "int"),
    "clamp": ("pyos_clamp", "int"),
    "is_power_of_two": ("pyos_is_power_of_two", "int"),
    "next_power_of_two": ("pyos_next_power_of_two", "int"),
    "log2_floor": ("pyos_log2_floor", "int"),
    "rand_range": ("pyos_rand_range", "int"),
    "rand_bool": ("pyos_rand_bool", "int"),
    "roll": ("pyos_roll", "int"),
    # Tanda B — strings avanzado (str2.c)
    "str_len": ("pyos_str_len", "int"),
    "str_to_int": ("pyos_str_to_int", "int"),
    "str_char_at": ("pyos_str_char_at", "int"),
    "str_contains": ("pyos_str_contains", "int"),
    "str_index": ("pyos_str_index", "int"),
    "str_count": ("pyos_str_count", "int"),
    "starts_with": ("pyos_starts_with", "int"),
    "ends_with": ("pyos_ends_with", "int"),
    "upper": ("pyos_upper", "str"),
    "lower": ("pyos_lower", "str"),
    "reverse": ("pyos_reverse", "str"),
    "trim": ("pyos_trim", "str"),
    "repeat": ("pyos_repeat", "str"),
    "left": ("pyos_left", "str"),
    "right": ("pyos_right", "str"),
    "pad_left": ("pyos_pad_left", "str"),
    "pad_right": ("pyos_pad_right", "str"),
    "replace_char": ("pyos_replace_char", "str"),
    "int_to_hex": ("pyos_int_to_hex", "str"),
    "int_to_bin": ("pyos_int_to_bin", "str"),
    # Tanda C — display/VGA (vga2.c)
    "gotoxy": ("pyos_gotoxy", "int"),
    "get_x": ("pyos_get_x", "int"),
    "get_y": ("pyos_get_y", "int"),
    "set_color": ("pyos_set_color", "int"),
    "get_color": ("pyos_get_color", "int"),
    "draw_char": ("pyos_draw_char", "int"),
    "draw_at": ("pyos_draw_at", "int"),
    "clr_row": ("pyos_clr_row", "int"),
    "fill_screen": ("pyos_fill_screen", "int"),
    "hline": ("pyos_hline", "int"),
    "vline": ("pyos_vline", "int"),
    "box": ("pyos_box", "int"),
    "fill_rect": ("pyos_fill_rect", "int"),
    "screen_w": ("pyos_screen_w", "int"),
    "screen_h": ("pyos_screen_h", "int"),
    "cursor_show": ("pyos_cursor_show", "int"),
    "invert_row": ("pyos_invert_row", "int"),
    # Tanda D — teclado, tiempo y procesos (runtime.c / proc.c / misc.c)
    "key_available": ("pyos_key_available", "int"),
    "getc_nowait": ("pyos_getc_nowait", "int"),
    "getc": ("pyos_getc", "int"),
    "clear_kb": ("pyos_clear_kb", "int"),
    "shift_pressed": ("pyos_shift_pressed", "int"),
    "caps_active": ("pyos_caps_active", "int"),
    "millis": ("pyos_millis", "int"),
    "seconds": ("pyos_seconds", "int"),
    "getpid": ("pyos_getpid", "int"),
    "task_count": ("pyos_task_count", "int"),
    "task_alive": ("pyos_task_alive", "int"),
    "task_name": ("pyos_task_name", "str"),
    "task_state_str": ("pyos_task_state_str", "str"),
    "self_name": ("pyos_self_name", "str"),
    "kill": ("pyos_kill", "int"),
    # Tanda E — info de la máquina (misc.c / iso9660.c)
    "paging_enabled": ("pyos_paging_enabled", "int"),
    "mem_total": ("pyos_mem_total", "int"),
    "mem_heap_blocks": ("pyos_mem_heap_blocks", "int"),
    "cpu_vendor": ("pyos_cpu_vendor", "str"),
    "cpu_has_fpu": ("pyos_cpu_has_fpu", "int"),
    "kernel_base": ("pyos_kernel_base", "int"),
    "iso_count": ("pyos_iso_count", "int"),
    "version_string": ("pyos_version_string", "str"),
    # Tanda F — misceláneos de bonus
    "rand_str": ("pyos_rand_str", "str"),
    "toupper_char": ("pyos_toupper_char", "int"),
    "tolower_char": ("pyos_tolower_char", "int"),
    "is_digit": ("pyos_is_digit", "int"),
    "is_alpha": ("pyos_is_alpha", "int"),
    "fs_mounted": ("pyos_fs_mounted", "int"),
    "fopen_append": ("pyos_fopen_append", "int"),
}


@dataclass
class _FuncScope:
    name: str
    var_types: dict = field(default_factory=dict)  # nombre -> "int" | "str"
    is_entry: bool = False


class Transpiler:
    """Uso:
        c_source = Transpiler().transpile(python_source, filename="kernel.py")
    """

    def __init__(self):
        self._entry_name: str | None = None
        self._known_funcs: set[str] = set()

    def transpile(self, source: str, filename: str = "<kernel>") -> str:
        try:
            tree = ast.parse(source, filename=filename)
        except SyntaxError as e:
            raise TranspileError(f"error de sintaxis Python: {e}") from e

        funcs = [n for n in tree.body if isinstance(n, ast.FunctionDef)]
        if not funcs:
            raise TranspileError("el archivo no define ninguna función")

        # nombres válidos para llamadas función-a-función
        self._known_funcs = {f.name for f in funcs}

        # encontrar el entrypoint
        entry_funcs = [f for f in funcs if self._has_entry_decorator(f)]
        if len(entry_funcs) == 0:
            raise TranspileError(
                "falta una función marcada con @pyos.entry — ese es el punto "
                "de arranque real del kernel"
            )
        if len(entry_funcs) > 1:
            raise TranspileError(
                f"hay {len(entry_funcs)} funciones con @pyos.entry, tiene que "
                "haber exactamente una"
            )
        self._entry_name = entry_funcs[0].name
        if entry_funcs[0].args.args:
            raise TranspileError(
                "la función @pyos.entry no puede recibir argumentos", entry_funcs[0]
            )

        def _is_module_docstring(i: int, n: ast.stmt) -> bool:
            return (
                i == 0 and isinstance(n, ast.Expr)
                and isinstance(n.value, ast.Constant)
                and isinstance(n.value.value, str)
            )

        other_top_level = [
            n for i, n in enumerate(tree.body)
            if not isinstance(n, ast.FunctionDef)
            and not (isinstance(n, ast.Import) or isinstance(n, ast.ImportFrom))
            and not _is_module_docstring(i, n)
        ]
        if other_top_level:
            raise TranspileError(
                "solo se permiten funciones (y el import de pyos) a nivel de "
                "módulo", other_top_level[0]
            )

        c_funcs = [self._emit_function(f) for f in funcs]

        header = (
            '/* ARCHIVO GENERADO por pyos.transpiler — no editar a mano.\n'
            f' * Fuente original: {filename}\n'
            ' */\n'
            '#include "pyos_runtime.h"\n\n'
        )
        forward_decls = "\n".join(
            f"static {self._signature(f)};"
            for f in funcs if f.name != self._entry_name
        )
        body = "\n\n".join(c_funcs)
        return f"{header}{forward_decls}\n\n{body}\n"

    # -- helpers de forma -----------------------------------------------

    @staticmethod
    def _has_entry_decorator(fn: ast.FunctionDef) -> bool:
        for dec in fn.decorator_list:
            if isinstance(dec, ast.Attribute) and dec.attr == "entry":
                return True
            if isinstance(dec, ast.Name) and dec.id == "entry":
                return True
        return False

    def _signature(self, fn: ast.FunctionDef) -> str:
        if fn.name == self._entry_name:
            return "void pyos_entry(void)"
        params = ", ".join(f"int {a.arg}" for a in fn.args.args)
        return f"int {fn.name}({params or 'void'})"

    # -- emisión de funciones --------------------------------------------

    def _emit_function(self, fn: ast.FunctionDef) -> str:
        scope = _FuncScope(name=fn.name, is_entry=(fn.name == self._entry_name))
        for a in fn.args.args:
            scope.var_types[a.arg] = "int"

        sig = self._signature(fn)
        prefix = "void" if scope.is_entry else "static int"
        # _signature ya devuelve el tipo correcto, reconstruimos con storage class
        c_sig = sig if scope.is_entry else f"static {sig}"

        body = fn.body
        if (body and isinstance(body[0], ast.Expr)
                and isinstance(body[0].value, ast.Constant)
                and isinstance(body[0].value.value, str)):
            body = body[1:]  # ignorar docstring de la función

        lines = [f"{c_sig} {{"]
        for stmt in body:
            lines.extend(self._emit_stmt(stmt, scope, indent=1))
        if not scope.is_entry and not self._ends_in_return(body):
            lines.append("    return 0;")
        lines.append("}")
        return "\n".join(lines)

    @staticmethod
    def _ends_in_return(body: list[ast.stmt]) -> bool:
        return bool(body) and isinstance(body[-1], ast.Return)

    # -- statements --------------------------------------------------------

    def _emit_stmt(self, node: ast.stmt, scope: _FuncScope, indent: int) -> list[str]:
        pad = "    " * indent
        if isinstance(node, ast.Expr):
            return [f"{pad}{self._emit_call_expr(node.value, scope)};"]

        if isinstance(node, ast.Assign):
            if len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
                raise TranspileError("solo se soporta 'x = valor' (un target)", node)
            name = node.targets[0].id
            value_c, value_type = self._emit_expr(node.value, scope)
            existing = scope.var_types.get(name)
            if existing is None:
                scope.var_types[name] = value_type
                ctype = "int" if value_type == "int" else "const char*"
                return [f"{pad}{ctype} {name} = {value_c};"]
            if existing != value_type:
                raise TranspileError(
                    f"'{name}' ya es tipo {existing}, no se puede reasignar a "
                    f"{value_type} (no hay tipado dinámico en el kernel)", node
                )
            return [f"{pad}{name} = {value_c};"]

        if isinstance(node, ast.AugAssign):
            if not isinstance(node.target, ast.Name):
                raise TranspileError("solo se soporta 'x op= valor'", node)
            name = node.target.id
            if scope.var_types.get(name) != "int":
                raise TranspileError("'+=' y similares solo aplican a enteros", node)
            op = self._binop(node.op, node)
            value_c, vtype = self._emit_expr(node.value, scope)
            if vtype != "int":
                raise TranspileError("el operando debe ser entero", node)
            return [f"{pad}{name} = {name} {op} ({value_c});"]

        if isinstance(node, ast.If):
            cond, ctype = self._emit_expr(node.test, scope)
            if ctype != "int":
                raise TranspileError("la condición del if debe ser entera/boolean", node)
            out = [f"{pad}if ({cond}) {{"]
            for s in node.body:
                out.extend(self._emit_stmt(s, scope, indent + 1))
            out.append(f"{pad}}}")
            if node.orelse:
                out[-1] = f"{pad}}} else {{"
                for s in node.orelse:
                    out.extend(self._emit_stmt(s, scope, indent + 1))
                out.append(f"{pad}}}")
            return out

        if isinstance(node, ast.While):
            cond, ctype = self._emit_expr(node.test, scope)
            if ctype != "int":
                raise TranspileError("la condición del while debe ser entera/boolean", node)
            out = [f"{pad}while ({cond}) {{"]
            for s in node.body:
                out.extend(self._emit_stmt(s, scope, indent + 1))
            out.append(f"{pad}}}")
            return out

        if isinstance(node, ast.For):
            out = self._emit_for_range(node, scope, indent)
            if out is not None:
                return out
            raise TranspileError(
                "solo se soporta 'for x in range(...)' (hasta 3 argumentos)", node
            )

        if isinstance(node, ast.Return):
            if scope.is_entry:
                raise TranspileError(
                    "la función @pyos.entry no puede usar 'return' (no hay a "
                    "dónde volver en el kernel)", node
                )
            if node.value is None:
                return [f"{pad}return 0;"]
            val_c, vtype = self._emit_expr(node.value, scope)
            if vtype != "int":
                raise TranspileError("las funciones solo pueden retornar int", node)
            return [f"{pad}return {val_c};"]

        if isinstance(node, ast.Pass):
            return [f"{pad};"]

        if isinstance(node, ast.Break):
            return [f"{pad}break;"]

        if isinstance(node, ast.Continue):
            return [f"{pad}continue;"]

        raise TranspileError(
            f"instrucción no soportada en el kernel: {type(node).__name__}", node
        )

    def _emit_for_range(self, node: ast.For, scope: _FuncScope, indent: int):
        if not isinstance(node.target, ast.Name):
            return None
        if not (isinstance(node.iter, ast.Call) and isinstance(node.iter.func, ast.Name)
                and node.iter.func.id == "range"):
            return None
        args = node.iter.args
        var = node.target.id
        pad = "    " * indent
        if len(args) == 1:
            start_c, stop_c, step_c = "0", self._emit_expr(args[0], scope)[0], "1"
        elif len(args) == 2:
            start_c = self._emit_expr(args[0], scope)[0]
            stop_c = self._emit_expr(args[1], scope)[0]
            step_c = "1"
        elif len(args) == 3:
            start_c = self._emit_expr(args[0], scope)[0]
            stop_c = self._emit_expr(args[1], scope)[0]
            step_c = self._emit_expr(args[2], scope)[0]
        else:
            return None
        scope.var_types.setdefault(var, "int")
        out = [
            f"{pad}for (int {var} = {start_c}; {var} < {stop_c}; {var} += {step_c}) {{"
        ]
        for s in node.body:
            out.extend(self._emit_stmt(s, scope, indent + 1))
        out.append(f"{pad}}}")
        return out

    def _emit_call_expr(self, node: ast.expr, scope: _FuncScope) -> str:
        c, _ = self._emit_expr(node, scope)
        return c

    # -- expressions -------------------------------------------------------

    def _emit_expr(self, node: ast.expr, scope: _FuncScope):
        """Devuelve (código_c, tipo) donde tipo es 'int' o 'str'."""
        if isinstance(node, ast.Constant):
            if isinstance(node.value, bool):
                return ("1" if node.value else "0", "int")
            if isinstance(node.value, int):
                return (str(node.value), "int")
            if isinstance(node.value, str):
                return (self._c_string_literal(node.value), "str")
            raise TranspileError(f"literal no soportado: {node.value!r}", node)

        if isinstance(node, ast.Name):
            if node.id not in scope.var_types:
                raise TranspileError(f"variable '{node.id}' no definida", node)
            return (node.id, scope.var_types[node.id])

        if isinstance(node, ast.BinOp):
            l_c, l_t = self._emit_expr(node.left, scope)
            r_c, r_t = self._emit_expr(node.right, scope)
            if l_t == "str" and r_t == "str" and isinstance(node.op, ast.Add):
                return (f"pyos_concat({l_c}, {r_c})", "str")
            if l_t != "int" or r_t != "int":
                raise TranspileError(
                    "las operaciones aritméticas solo funcionan entre enteros, "
                    "salvo 'a + b' entre dos strings (concatenación)", node
                )
            op = self._binop(node.op, node)
            return (f"({l_c} {op} {r_c})", "int")

        if isinstance(node, ast.BoolOp):
            op = "&&" if isinstance(node.op, ast.And) else "||"
            parts = []
            for v in node.values:
                c, t = self._emit_expr(v, scope)
                if t != "int":
                    raise TranspileError("and/or solo con expresiones enteras", node)
                parts.append(c)
            return ("(" + f" {op} ".join(parts) + ")", "int")

        if isinstance(node, ast.UnaryOp):
            c, t = self._emit_expr(node.operand, scope)
            if isinstance(node.op, ast.Not):
                return (f"(!{c})", "int")
            if isinstance(node.op, ast.USub):
                if t != "int":
                    raise TranspileError("'-' unario solo en enteros", node)
                return (f"(-{c})", "int")
            raise TranspileError("operador unario no soportado", node)

        if isinstance(node, ast.Compare):
            if len(node.ops) != 1 or len(node.comparators) != 1:
                raise TranspileError(
                    "solo se soporta una comparación por expresión (a < b, no "
                    "a < b < c)", node
                )
            l_c, l_t = self._emit_expr(node.left, scope)
            r_c, r_t = self._emit_expr(node.comparators[0], scope)
            op_type = type(node.ops[0])

            if l_t == "str" or r_t == "str":
                if l_t != "str" or r_t != "str":
                    raise TranspileError(
                        "no se puede comparar un string con un entero", node
                    )
                if op_type not in (ast.Eq, ast.NotEq):
                    raise TranspileError(
                        "entre strings solo se soportan '==' y '!=' (no hay "
                        "orden alfabético todavía)", node
                    )
                call = f"pyos_streq({l_c}, {r_c})"
                return (call if op_type is ast.Eq else f"(!{call})", "int")

            if l_t != "int" or r_t != "int":
                raise TranspileError("las comparaciones son solo entre enteros", node)
            cop = {
                ast.Lt: "<", ast.Gt: ">", ast.LtE: "<=", ast.GtE: ">=",
                ast.Eq: "==", ast.NotEq: "!=",
            }.get(op_type)
            if cop is None:
                raise TranspileError("operador de comparación no soportado", node)
            return (f"({l_c} {cop} {r_c})", "int")

        if isinstance(node, ast.Call):
            return self._emit_call(node, scope)

        raise TranspileError(f"expresión no soportada: {type(node).__name__}", node)

    def _emit_call(self, node: ast.Call, scope: _FuncScope):
        if isinstance(node.func, ast.Name) and node.func.id == "ord":
            if len(node.args) != 1:
                raise TranspileError("ord() recibe exactamente un carácter", node)
            arg = node.args[0]
            if not (isinstance(arg, ast.Constant)
                    and isinstance(arg.value, str) and len(arg.value) == 1):
                raise TranspileError(
                    "ord() solo se puede usar con un literal de un carácter "
                    "('a', ' ', '\\n', ...)", node
                )
            return (str(ord(arg.value)), "int")

        if isinstance(node.func, ast.Name) and node.func.id == "str":
            if len(node.args) != 1:
                raise TranspileError("str() recibe exactamente un argumento", node)
            c, t = self._emit_expr(node.args[0], scope)
            if t == "str":
                return (c, "str")  # ya es str, no hace falta convertir
            if t != "int":
                raise TranspileError("str() solo puede convertir enteros", node)
            return (f"pyos_int_to_str({c})", "str")

        if isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name) \
                and node.func.value.id == "pyos":
            fname = node.func.attr
            if fname == "spawn":
                # pyos.spawn("nombre", mi_funcion): el 2º argumento es el
                # nombre de una función definida en este archivo (en C, con su
                # mismo nombre); se pasa como puntero a función.
                if len(node.args) != 2:
                    raise TranspileError(
                        "pyos.spawn(nombre, funcion) recibe exactamente 2 argumentos", node)
                name_c, t = self._emit_expr(node.args[0], scope)
                if t != "str":
                    raise TranspileError("el nombre del proceso debe ser un string", node)
                func_arg = node.args[1]
                if not (isinstance(func_arg, ast.Name)
                        and func_arg.id in self._known_funcs):
                    raise TranspileError(
                        "pyos.spawn espera una función definida en este archivo", node)
                return (f"pyos_spawn({name_c}, (void (*)(void)){func_arg.id})", "int")
            if fname not in _RUNTIME_CALLS:
                raise TranspileError(f"pyos.{fname} no existe en el runtime", node)
            c_name, ret_type = _RUNTIME_CALLS[fname]
            args_c = []
            for a in node.args:
                c, _t = self._emit_expr(a, scope)
                args_c.append(c)
            return (f"{c_name}({', '.join(args_c)})", ret_type)

        if isinstance(node.func, ast.Name) and node.func.id in self._known_funcs:
            args_c = []
            for a in node.args:
                c, t = self._emit_expr(a, scope)
                if t != "int":
                    raise TranspileError(
                        "solo se pueden pasar enteros entre funciones por ahora", node
                    )
                args_c.append(c)
            return (f"{node.func.id}({', '.join(args_c)})", "int")

        raise TranspileError(
            "solo se pueden llamar funciones de pyos.* o funciones definidas "
            "en este mismo archivo", node
        )

    @staticmethod
    def _binop(op: ast.operator, node: ast.AST) -> str:
        mapping = {
            ast.Add: "+", ast.Sub: "-", ast.Mult: "*",
            ast.FloorDiv: "/", ast.Mod: "%",
        }
        c = mapping.get(type(op))
        if c is None:
            raise TranspileError(f"operador no soportado: {type(op).__name__}", node)
        return c

    @staticmethod
    def _c_string_literal(s: str) -> str:
        """Genera el literal de C, mapeando acentos a CP437 — la VGA en modo
        texto no entiende UTF-8, y por defecto los bytes de un carácter
        multi-byte se muestran como cuadraditos sueltos. Los caracteres
        españoles más comunes se mapean 1:1 a su código CP437; cualquier
        otro no-ASCII se pasa por NFKD para sacarle el acento, y si ni así
        entra en ASCII, se reemplaza por '?'."""
        out = ['"']
        for ch in s:
            if ch == "\\":
                out.append("\\\\")
            elif ch == '"':
                out.append('\\"')
            elif ch == "\n":
                out.append("\\n")
            elif ch == "\t":
                out.append("\\t")
            elif ord(ch) < 128:
                out.append(ch)
            else:
                code = _CP437_MAP.get(ch)
                if code is None:
                    base = unicodedata.normalize("NFKD", ch)
                    base = "".join(c for c in base if not unicodedata.combining(c))
                    if base and ord(base[0]) < 128:
                        out.append(base[0])
                        continue
                    code = ord("?")
                out.append(f"\\{code:03o}")  # escape octal de 3 dígitos:
                # evita la ambigüedad de \xHH cuando el próximo carácter del
                # string también es un dígito hexadecimal
        out.append('"')
        return "".join(out)
