"""
pyos — framework en Python para escribir sistemas operativos reales.

No es un simulador: el código que escribís acá se transpila a C, se compila
con un bootloader multiboot2 real (ensamblador + GRUB) y se empaqueta en una
ISO booteable de verdad (QEMU, VirtualBox, o una PC/VM real vía USB).

Cómo se usa:

    import pyos

    @pyos.entry
    def main():
        pyos.clear()
        pyos.draw("Hello from pyos!\\n")
        pyos.halt()

Estas mismas funciones (draw, clear, halt, log) también corren bajo CPython
normal en tu máquina — así podés simular la lógica de tu OS con
`python kernel.py` antes de compilarla de verdad con `pyos build kernel.py`.
Ver README.md → "Subconjunto soportado" para las reglas exactas de lo que
el compilador (pyos.transpiler) puede convertir a C.
"""

import atexit
import random
import sys
import time
import threading

__version__ = "0.5.0"

__all__ = [
    "entry", "draw", "clear", "halt", "reboot", "log", "log_char",
    "putc", "putdec", "readline", "kbchar", "line", "substr", "beep",
    "random_int", "free", "strdup", "heap_total", "heap_used", "heap_free",
    "fsinit", "fopen", "fwrite", "fread", "fclose", "fexists", "fdel",
    "fls", "fsize", "iso_status", "iso_ls", "iso_read", "iso_free",
    "spawn", "exit_task", "sleep", "ps", "uptime", "ticks",
    # Tanda A — matemática
    "abs", "sign", "is_even", "is_odd", "pow", "factorial", "fib", "gcd",
    "lcm", "sqrt_int", "is_prime", "next_prime", "digit_count", "sum_digits",
    "reverse_int", "is_palindrome_int", "min", "max", "clamp",
    "is_power_of_two", "next_power_of_two", "log2_floor", "rand_range",
    "rand_bool", "roll",
    # Tanda B — strings avanzado
    "str_len", "str_to_int", "str_char_at", "str_contains", "str_index",
    "str_count", "starts_with", "ends_with", "upper", "lower", "reverse",
    "trim", "repeat", "left", "right", "pad_left", "pad_right",
    "replace_char", "int_to_hex", "int_to_bin",
    # Tanda C — display/VGA
    "gotoxy", "get_x", "get_y", "set_color", "get_color", "draw_char",
    "draw_at", "clr_row", "fill_screen", "hline", "vline", "box",
    "fill_rect", "screen_w", "screen_h", "cursor_show", "invert_row",
    # Tanda D — teclado, tiempo y procesos
    "key_available", "getc_nowait", "getc", "clear_kb", "shift_pressed",
    "caps_active", "millis", "seconds", "getpid", "task_count",
    "task_alive", "task_name", "task_state_str", "self_name", "kill",
    # Tanda E — info de la máquina
    "paging_enabled", "mem_total", "mem_heap_blocks", "cpu_vendor",
    "cpu_has_fpu", "kernel_base", "iso_count", "version_string",
    # Tanda F — misceláneos
    "rand_str", "toupper_char", "tolower_char", "is_digit", "is_alpha",
    "fs_mounted", "fopen_append",
]

_last_readline = ""


def entry(func):
    """Marca la función como el punto de entrada del kernel (equivalente a
    kernel_main). Debe existir exactamente una por archivo y no puede recibir
    argumentos.

    En el kernel real, el runtime en C la llama directamente al arrancar.
    En modo simulación (`python kernel.py` o `pyos simulate`), se registra
    para correr automáticamente cuando termina de cargar el módulo — así el
    mismo archivo sirve para las dos cosas sin código extra."""
    func._pyos_entry = True
    if func.__globals__.get("__name__") == "__main__":
        atexit.register(lambda f=func: _run_entry(f))
    return func


def _run_entry(func):
    # Swallows SystemExit raised por halt()/reboot()/EOF de readline, así la
    # simulación termina limpia (sin "Exception ignored in atexit callback").
    try:
        func()
    except SystemExit:
        pass


# ---------------------------------------------------------------------------
# Modo simulación: cuando este módulo corre bajo CPython normal (no dentro
# del kernel compilado), estas funciones imprimen a stdout para que puedas
# probar la lógica de tu OS sin compilar nada todavía.
# Cuando `pyos build` transpila tu archivo, estas llamadas se convierten en
# llamadas directas a las funciones reales de pyos/_native/runtime.c
# (pyos_draw, pyos_clear, pyos_halt, pyos_log) — no pasan por Python en el
# kernel final.
# ---------------------------------------------------------------------------

def draw(text: str) -> None:
    """Escribe texto en pantalla. En simulación: imprime a stdout."""
    sys.stdout.write(str(text))


def clear() -> None:
    """Limpia la pantalla. En simulación: no-op (o podría limpiar la terminal)."""
    pass


def log(text: str) -> None:
    """Log de debug por puerto serie. En simulación: imprime a stderr."""
    sys.stderr.write(str(text) + ("" if str(text).endswith("\n") else "\n"))


def log_char(c: int) -> None:
    """Escribe un solo carácter (código ASCII) al log por puerto serie.
    En simulación: imprime el carácter a stderr."""
    sys.stderr.write(chr(int(c)) if isinstance(c, int) else str(c))


def putc(c: int) -> None:
    """Escribe un solo carácter (código ASCII) en pantalla.
    En simulación: lo imprime a stdout."""
    sys.stdout.write(chr(int(c)) if isinstance(c, int) else str(c))


def readline() -> int:
    """Lee una línea completa desde el teclado, con echo en la pantalla.
    Devuelve la cantidad de caracteres leídos (sin el Enter).

    En el kernel real: bloquea en un loop de hlt hasta recibir Enter por el
    IRQ1 del teclado PS/2, mostrando en VGA lo que se escribe y
    procesando Backspace. En simulación: lee de stdin de la terminal (si la
    entrada llega a EOF —Ctrl-D o pipe terminado— termina el programa)."""
    global _last_readline
    line = sys.stdin.readline()
    if not line:
        raise SystemExit(0)
    _last_readline = line.rstrip("\n").rstrip("\r")
    return len(_last_readline)


def kbchar(i: int) -> int:
    """Devuelve el código ASCII del carácter i (0-based) de la última línea
    leída con readline. -1 si el índice está fuera de rango."""
    global _last_readline
    if i < 0 or i >= len(_last_readline):
        return -1
    return ord(_last_readline[i])


def line() -> str:
    """Devuelve la última línea leída con readline(), como string completo
    (para comparar con == en vez de carácter por carácter)."""
    return _last_readline


def substr(start: int, len_rest: int) -> str:
    """Devuelve porción de la última línea leída: substr(5, 8) toma del
    carácter 5 en adelante, 8 caracteres. En el kernel real es exactamente
    lo mismo (copiado del búfer de readline)."""
    return _last_readline[int(start):int(start) + int(len_rest)]


def beep(freq_hz: int, ms: int) -> None:
    """Suena el PC speaker a freq_hz Hz durante ms milisegundos.
    En simulación: no hay speaker, así que solo lo describe por stderr."""
    sys.stderr.write(f"[beep {freq_hz}Hz {ms}ms]\n")


def random_int(n: int) -> int:
    """Entero pseudoaleatorio en [0, n). En el kernel real usa un LCG
    sembrado con RDTSC; en simulación usa random.randrange (mismo rango,
    generador distinto — no esperes la misma secuencia en los dos lados)."""
    if n <= 0:
        return 0
    return random.randrange(n)


def putdec(n: int) -> None:
    """Imprime el entero n (sin formato, decimal). En simulación: lo imprime
    a stdout. En el kernel real escribe dígito por dígito en el VGA."""
    sys.stdout.write(str(int(n)))


def free(ptr) -> None:
    """Libera un bloque del heap dinámico (el resultado de pyos.strdup o de
    pyos.line() == ... no se debe liberar: solo lo que vino del heap).
    En simulación: no-op (los strings de Python son inmutables y se liberan
    solos)."""
    pass


def strdup(s: str) -> str:
    """Copia un string al heap dinámico y devuelve el nuevo string.
    En el kernel real esto hace exactamente eso (malloc + memcpy); usá
    pyos.free() cuando termines, porque el kernel no tiene GC.
    En simulación solo aproxima el uso del heap (los strings de Python se
    liberan solos, así que free() es un no-op)."""
    global _heap_used
    _heap_used += len(s) + 1
    return str(s)


def heap_total() -> int:
    """Tamaño total del heap dinámico del kernel, en bytes."""
    return 1 << 20


def heap_used() -> int:
    """Bytes ocupados en el heap dinámico (bloques sin liberar)."""
    return _heap_used


def heap_free() -> int:
    """Bytes libres en el heap dinámico."""
    return heap_total() - heap_used()


_heap_used = 0


def halt() -> None:
    """Detiene la CPU para siempre. En simulación: termina el programa de
    forma limpia (lo más parecido a apagar la máquina en una terminal)."""
    raise SystemExit(0)


def reboot() -> None:
    """Reinicia la máquina (pulso de reset por el controller 8042).
    En simulación: termina el programa de forma limpia."""
    raise SystemExit(0)


# ---------------------------------------------------------------------------
# Fase 4 — filesystem: en simulación los "archivos" son strings en memoria
# (mismos comandos que en el kernel real: fopen/fwrite/fread/fls/...).
# ---------------------------------------------------------------------------

_fs_files = {}                     # nombre(str) -> {data, fd}
_fs_next_fd = 1
_fs_mounted = False


def fsinit() -> int:
    """Monta el disco (MYOSFS) o lo formatea si está vacío. En el kernel real
    lee/escribe el disco ATA; en simulación solo habilita un FS en memoria."""
    global _fs_files, _fs_mounted
    _fs_files = {}
    _fs_mounted = True
    return 1


def fopen(name: str, mode: int) -> int:
    """Abre un archivo. mode 0 = lectura, 1 = escritura (crea o trunca).
    Devuelve un fd (entero), o -1 si falla."""
    global _fs_files
    n = str(name)
    if mode == 1:
        _fs_files.setdefault(n, {"data": "", "fd": None})
    if n not in _fs_files:
        return -1
    h = _fs_files[n]
    if mode == 1:
        h["data"] = ""
    if h["fd"] is None:
        h["fd"] = _fs_next_fd
        _fs_next_fd += 1
    return h["fd"]


def fwrite(fd: int, text: str) -> int:
    global _fs_files
    for h in _fs_files.values():
        if h["fd"] == int(fd):
            h["data"] = str(text)
            return len(text)
    return -1


def fread(fd: int, max_len: int) -> str:
    global _fs_files
    for h in _fs_files.values():
        if h["fd"] == int(fd):
            return h["data"][:int(max_len)]
    return ""


def fclose(fd: int) -> int:
    global _fs_files
    for h in _fs_files.values():
        if h["fd"] == int(fd):
            h["fd"] = None
            return 0
    return -1


def fexists(name: str) -> int:
    global _fs_files
    return 1 if str(name) in _fs_files else 0


def fdel(name: str) -> int:
    global _fs_files
    return 0 if _fs_files.pop(str(name), None) is not None else -1


def fls() -> int:
    """Cantidad de archivos en el FS."""
    global _fs_files
    return len(_fs_files)


def fsize(fd: int) -> int:
    global _fs_files
    for h in _fs_files.values():
        if h["fd"] == int(fd):
            return len(h["data"])
    return -1


# ---------------------------------------------------------------------------
# Fase 4 — lectura del CD booteado: en simulación no hay CD; se devuelve el
# mismo contrato que en el kernel (iso_status 1 si "detecta" el archivo,
# iso_read "" si no existe).
# ---------------------------------------------------------------------------

_iso_files = {}


def iso_status() -> int:
    """1 si hay CD ISO9660, 0 si no. En simulación: 0 (no hay CD)."""
    return 0


def iso_ls() -> int:
    """Lista los archivos de la raíz del CD."""
    if not _iso_files:
        return 0
    sys.stdout.write("(simulación: CD virtual)\n")
    for name, data in _iso_files.items():
        sys.stdout.write(f"{name}  ({len(data)} B)\n")
    return len(_iso_files)


def iso_read(name: str) -> str:
    """Lee un archivo del CD a un string del heap (libertad con iso_free).
    En simulación se puede precargar con _iso_preload()."""
    return _iso_files.get(str(name), "")


def iso_free(text: str) -> None:
    """Libera el resultado de iso_read en el kernel real. En simulación no-op."""
    pass


def _iso_preload(files: dict[str, str]) -> None:
    global _iso_files
    _iso_files = {str(k): str(v) for k, v in files.items()}


# ---------------------------------------------------------------------------
# Fase 5 — multitarea: en simulación un hilo Python por proceso, con el mismo
# contrato de API que el kernel real (spawn/sleep/exit_task/ps/uptime/ticks).
# ---------------------------------------------------------------------------

_sim_procs = {}          # nombre -> {"thread", "done", "pid"}
_sim_next_pid = 2        # el pid 1 es el proceso inicial
_sim_self_pid = 1        # pid del proceso actual (hilo en simulación)
_monotonic_start = time.monotonic()


def spawn(name: str, fn) -> int:
    """Crea un proceso que corre `fn` (sin argumentos). En simulación corre en
    un hilo Python aparte; en el kernel real crea un stack propio en el heap
    y lo agrega al scheduler round-robin del PIT."""
    global _sim_next_pid
    n = str(name)
    pid = _sim_next_pid
    _sim_next_pid += 1

    def _body():
        global _sim_self_pid
        _sim_self_pid = pid
        try:
            fn()
        except SystemExit:
            pass
        finally:
            _sim_procs[n]["done"] = True

    _sim_procs[n] = {"thread": threading.Thread(target=_body, daemon=True),
                     "done": False, "pid": pid}
    _sim_procs[n]["thread"].start()
    return pid


def exit_task() -> None:
    """Termina el proceso actual. En simulación: levanta SystemExit para que
    el hilo termine; en el kernel real pasa el proceso a EXITED."""
    sys.stderr.write("[sim] exit_task\n")
    raise SystemExit(0)


def sleep(ms: int) -> None:
    """Suspende el proceso actual por `ms` milisegundos (1 tick = 10 ms en el
    kernel real). En simulación duerme el hilo real."""
    sys.stderr.write(f"[sim] sleep {int(ms)}ms\n")
    time.sleep(int(ms) / 1000.0)


def ps() -> int:
    """Lista los procesos vivos."""
    sys.stdout.write("PID   Nombre\n")
    pid = 1
    sys.stdout.write(f"{pid}    main\n")
    for n, p in _sim_procs.items():
        pid += 1
        state = "termino" if p["done"] else "corriendo"
        sys.stdout.write(f"{pid}    {n} ({state})\n")
    return pid


def uptime() -> int:
    """Tiempo encendido en ticks (1 tick = 10 ms): simula con el reloj real."""
    return int((time.monotonic() - _monotonic_start) * 100)


def ticks() -> int:
    """Contador de ticks del PIT (simulado con el reloj real; mismo significado)."""
    return uptime()


# ---------------------------------------------------------------------------
# Tanda A — matemática entera. Misma semántica que math.c: devuelven -1 (o 0)
# cuando el resultado no existe / no entra en un int de 32 bits.
# ---------------------------------------------------------------------------

def abs(n: int) -> int:
    """Valor absoluto; INT_MIN devuelve 2147483647 (no hay espejo)."""
    n = int(n)
    if n == -2147483648:
        return 2147483647
    return n if n >= 0 else -n


def sign(n: int) -> int:
    n = int(n)
    return 1 if n > 0 else (-1 if n < 0 else 0)


def is_even(n: int) -> int:
    return 1 if (int(n) & 1) == 0 else 0


def is_odd(n: int) -> int:
    return int(n) & 1


def pow(a: int, b: int) -> int:
    """a^b con b>=0 (0^0=1); si el resultado no entra en int, -1."""
    a, b = int(a), int(b)
    if b < 0:
        return -1
    r = 1
    for _ in range(b):
        r *= a
        if r > 2147483647 or r < -2147483648:
            return -1
    return r


def factorial(n: int) -> int:
    """n! con n<=12; fuera de rango, -1."""
    n = int(n)
    if n < 0 or n > 12:
        return -1
    r = 1
    for i in range(2, n + 1):
        r *= i
    return r


def fib(n: int) -> int:
    """F(n) con n<=40; fuera de rango, -1."""
    n = int(n)
    if n < 0 or n > 40:
        return -1
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a


def gcd(a: int, b: int) -> int:
    """Máximo común divisor (en valor absoluto; nunca divide por 0)."""
    a, b = abs(int(a)), abs(int(b))
    while b:
        a, b = b, a % b
    return a


def lcm(a: int, b: int) -> int:
    """Mínimo común múltiplo; 0 si a==0 o b==0; -1 si no entra en int."""
    a, b = int(a), int(b)
    if a == 0 or b == 0:
        return 0
    m = (a // gcd(a, b)) * b
    m = abs(m)
    return m if m <= 2147483647 else -1


def sqrt_int(n: int) -> int:
    """Parte entera de la raíz cuadrada; n<=0 -> 0."""
    n = int(n)
    if n <= 0:
        return 0
    lo, hi = 0, min(n, 46341)
    while lo < hi:
        mid = lo + (hi - lo + 1) // 2
        if mid <= n // mid:
            lo = mid
        else:
            hi = mid - 1
    return lo


def is_prime(n: int) -> int:
    n = int(n)
    if n < 2:
        return 0
    if n % 2 == 0:
        return 1 if n == 2 else 0
    i = 3
    while i <= n // i:
        if n % i == 0:
            return 0
        i += 2
    return 1


def next_prime(n: int) -> int:
    n = max(int(n), 2)
    if n == 2:
        return 2
    if n % 2 == 0:
        n += 1
    while True:
        if is_prime(n):
            return n
        if n > 2147483647 - 2:
            return -1
        n += 2


def digit_count(n: int) -> int:
    v = abs(int(n))
    if v == 0:
        return 1
    return len(str(v))


def sum_digits(n: int) -> int:
    v = abs(int(n))
    s = 0
    while v:
        s += v % 10
        v //= 10
    return s


def reverse_int(n: int) -> int:
    """Número con los dígitos invertidos (conserva el signo); 0 si no entra."""
    n = int(n)
    neg = n < 0
    v = abs(n)
    r = 0
    while v:
        r = r * 10 + (v % 10)
        v //= 10
    if r > 2147483648:
        return 0
    return -r if neg else r


def is_palindrome_int(n: int) -> int:
    return 1 if reverse_int(int(n)) == int(n) else 0


def min(a: int, b: int) -> int:
    return a if a < b else b


def max(a: int, b: int) -> int:
    return a if a > b else b


def clamp(n: int, lo: int, hi: int) -> int:
    n, lo, hi = int(n), int(lo), int(hi)
    if lo > hi:
        lo, hi = hi, lo
    if n < lo:
        return lo
    if n > hi:
        return hi
    return n


def is_power_of_two(n: int) -> int:
    n = int(n)
    return 1 if n > 0 and (n & (n - 1)) == 0 else 0


def next_power_of_two(n: int) -> int:
    n = int(n)
    if n <= 0:
        return 1
    p = 1
    while p < n:
        p <<= 1
    if p > 2147483647:
        return -1
    return p


def log2_floor(n: int) -> int:
    n = int(n)
    if n <= 1:
        return 0
    v, r = n, 0
    while v > 1:
        v >>= 1
        r += 1
    return r


def rand_range(lo: int, hi: int) -> int:
    lo, hi = int(lo), int(hi)
    if hi < lo:
        lo, hi = hi, lo
    span = hi - lo + 1
    if span <= 0 or span > 2147483647:
        return lo
    return lo + random.randrange(span)


def rand_bool() -> int:
    return 1 if random.randrange(2) else 0


def roll(sides: int) -> int:
    sides = int(sides)
    if sides <= 1:
        return 1
    return 1 + random.randrange(sides)


# ---------------------------------------------------------------------------
# Tanda B — strings avanzado. Los strings del simulador son str normales; el
# resultado '' equivale a la cadena vacía del kernel, y no hay falla de alloc.
# ---------------------------------------------------------------------------

def str_len(s: str) -> int:
    return len(str(s))


def str_to_int(s: str) -> int:
    """Parsea decimal (soporta '-' inicial); vacío -> 0; se detiene en el
    primer carácter no dígito. Saturar al rango de int, como el kernel."""
    s = str(s)
    if not s:
        return 0
    i, neg = 0, False
    if s[0] == '-':
        neg, i = True, 1
    acc = 0
    while i < len(s) and s[i].isdigit():
        acc = min(acc * 10 + int(s[i]), 2147483647)
        i += 1
    return -acc if neg else acc


def str_char_at(s: str, i: int) -> int:
    s, i = str(s), int(i)
    if i < 0 or i >= len(s):
        return -1
    return ord(s[i])


def str_contains(s: str, sub: str) -> int:
    return 1 if str(sub) in str(s) else 0


def str_index(s: str, sub: str) -> int:
    s, sub = str(s), str(sub)
    return s.find(sub)  # -1 si no está


def str_count(s: str, sub: str) -> int:
    s, sub = str(s), str(sub)
    if not sub:
        return 0
    n, pos = 0, 0
    while True:
        pos = s.find(sub, pos)
        if pos < 0:
            return n
        n += 1
        pos += len(sub)


def starts_with(s: str, pref: str) -> int:
    return 1 if str(s).startswith(str(pref)) else 0


def ends_with(s: str, suf: str) -> int:
    return 1 if str(s).endswith(str(suf)) else 0


def upper(s: str) -> str:
    return str(s).upper()


def lower(s: str) -> str:
    return str(s).lower()


def reverse(s: str) -> str:
    return str(s)[::-1]


def trim(s: str) -> str:
    return str(s).strip(" ")


def repeat(s: str, n: int) -> str:
    n = int(n)
    if n <= 0:
        return ""
    return str(s) * n


def left(s: str, n: int) -> str:
    n = int(n)
    if n <= 0:
        return ""
    return str(s)[:n]


def right(s: str, n: int) -> str:
    n = int(n)
    if n <= 0:
        return ""
    return str(s)[-n:]


def pad_left(s: str, n: int, ch: int) -> str:
    s, n = str(s), int(n)
    pad = max(n - len(s), 0)
    return chr(int(ch) & 0xFF) * pad + s


def pad_right(s: str, n: int, ch: int) -> str:
    s, n = str(s), int(n)
    pad = max(n - len(s), 0)
    return s + chr(int(ch) & 0xFF) * pad


def replace_char(s: str, old: int, new: int) -> str:
    return str(s).replace(chr(int(old) & 0xFF), chr(int(new) & 0xFF))


def int_to_hex(n: int) -> str:
    """"0x" + hex minúsculas del patrón de 32 bits ("0x0" para 0)."""
    v = int(n) & 0xFFFFFFFF
    return "0x" + format(v, "x")


def int_to_bin(n: int) -> str:
    """"0b" + binario sin ceros a la izquierda ("0b0" para 0)."""
    v = int(n) & 0xFFFFFFFF
    return "0b" + format(v, "b")


# ---------------------------------------------------------------------------
# Tanda C — display/VGA. En simulación no hay VGA real: las funciones de
# dibujo devuelven 1 (o 0 si coord inválida) sin tocar una pantalla.
# ---------------------------------------------------------------------------

_sim_cursor_x = 0
_sim_cursor_y = 0
_sim_color = 0x0F

def gotoxy(x: int, y: int) -> int:
    global _sim_cursor_x, _sim_cursor_y
    _sim_cursor_x = max(0, min(int(x), 79))
    _sim_cursor_y = max(0, min(int(y), 24))
    return 1


def get_x() -> int:
    return _sim_cursor_x


def get_y() -> int:
    return _sim_cursor_y


def set_color(fg: int, bg: int) -> int:
    global _sim_color
    _sim_color = (int(fg) & 0x0F) | ((int(bg) & 0x07) << 4)
    return _sim_color


def get_color() -> int:
    return _sim_color


def draw_char(x: int, y: int, ch: int) -> int:
    if int(x) < 0 or int(x) >= 80 or int(y) < 0 or int(y) >= 25:
        return 0
    return 1


def draw_at(x: int, y: int, s: str) -> int:
    if int(x) < 0 or int(x) >= 80 or int(y) < 0 or int(y) >= 25:
        return 0
    return min(len(str(s)), 80 - int(x))


def clr_row(y: int) -> int:
    return 1 if 0 <= int(y) < 25 else 0


def fill_screen(ch: int) -> int:
    return 1


def hline(y: int, x1: int, x2: int, ch: int) -> int:
    return 1 if 0 <= int(y) < 25 else 0


def vline(x: int, y1: int, y2: int, ch: int) -> int:
    return 1 if 0 <= int(x) < 80 else 0


def box(x1: int, y1: int, x2: int, y2: int, ch: int) -> int:
    x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
    if x1 < 0 or y1 < 0 or x2 >= 80 or y2 >= 25 or x2 < x1 or y2 < y1:
        return 0
    return 1


def fill_rect(x1: int, y1: int, x2: int, y2: int, ch: int) -> int:
    x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
    if x1 < 0 or y1 < 0 or x2 >= 80 or y2 >= 25 or x2 < x1 or y2 < y1:
        return 0
    return 1


def screen_w() -> int:
    return 80


def screen_h() -> int:
    return 25


def cursor_show(on: int) -> int:
    return 1


def invert_row(y: int) -> int:
    return 1 if 0 <= int(y) < 25 else 0


# ---------------------------------------------------------------------------
# Tanda D — teclado no bloqueante, tiempo y procesos.
# ---------------------------------------------------------------------------

def key_available() -> int:
    """En el kernel: hay tecla pendiente sin leer. En simulación la entrada
    del terminal es línea a línea, así que no hay búfer: 0."""
    return 0


def getc_nowait() -> int:
    """Caracter pendiente o -1. En simulación siempre -1 (ver key_available)."""
    return -1


def getc() -> int:
    """Caracter (sin echo). En simulación lee un byte de stdin; EOF termina."""
    ch = sys.stdin.read(1)
    if not ch:
        raise SystemExit(0)
    return ord(ch)


def clear_kb() -> int:
    return 1


def shift_pressed() -> int:
    return 0


def caps_active() -> int:
    return 0


def millis() -> int:
    """Milisegundos desde el boot (ticks*10 en el kernel)."""
    return int((time.monotonic() - _monotonic_start) * 1000)


def seconds() -> int:
    """Segundos desde el boot."""
    return int(time.monotonic() - _monotonic_start)


def getpid() -> int:
    """PID del proceso actual. En simulación: pid 1 = main; los spawn desde 2."""
    return _sim_self_pid


def task_count() -> int:
    """Procesos existentes (kernel cuenta main + idle + spawn; la simulación
    no tiene idle, así que cuenta main + spawn)."""
    return 1 + len(_sim_procs)


def task_alive(pid: int) -> int:
    pid = int(pid)
    if pid == 1:
        return 1
    for p in _sim_procs.values():
        if p["pid"] == pid:
            return 0 if p["done"] else 1
    return 0


def task_name(pid: int) -> str:
    pid = int(pid)
    if pid == 1:
        return "main"
    for n, p in _sim_procs.items():
        if p["pid"] == pid:
            return n
    return ""


def task_state_str(pid: int) -> str:
    pid = int(pid)
    if pid == 1:
        return "corriendo"
    for p in _sim_procs.values():
        if p["pid"] == pid:
            return "terminado" if p["done"] else "corriendo"
    return "?"


def self_name() -> str:
    if _sim_self_pid == 1:
        return "main"
    for n, p in _sim_procs.items():
        if p["pid"] == _sim_self_pid:
            return n
    return ""


def kill(pid: int) -> int:
    """Marca el proceso como terminado; 1 si existía, 0 si no. Si es el pid
    del proceso actual, termina este hilo (como exit_task)."""
    global _sim_self_pid
    pid = int(pid)
    if pid == 1:
        return 0
    for p in _sim_procs.values():
        if p["pid"] == pid:
            p["done"] = True
            if pid == _sim_self_pid:
                raise SystemExit(0)
            return 1
    return 0


# ---------------------------------------------------------------------------
# Tanda E — info de la máquina (aproximada en simulación).
# ---------------------------------------------------------------------------

def paging_enabled() -> int:
    return 1


def mem_total() -> int:
    """RAM total en MiB reportada por multiboot; 32 si no hay dato."""
    return 32


def mem_heap_blocks() -> int:
    """Bloques libres en la free-list del heap. En simulación no hay
    free-list real: se aproxima con 0 (nada liberado aparte del bloque grande)."""
    return 0


def cpu_vendor() -> str:
    """Vendor de CPUID. En simulación depende de la plataforma host; por
    defecto "N/A" (como el kernel sin CPUID)."""
    return "N/A"


def cpu_has_fpu() -> int:
    return 1


def kernel_base() -> int:
    return 0x100000


def iso_count() -> int:
    """Archivos de la raíz del ISO detectado (0 si no hay ISO)."""
    return len(_iso_files) if _iso_files else 0


def version_string() -> str:
    return "MYOS v0.7-big"


# ---------------------------------------------------------------------------
# Tanda F — misceláneos (chars y rand).
# ---------------------------------------------------------------------------

def rand_str(n: int) -> str:
    """n caracteres aleatorios imprimibles (32..126); n<=0 -> ''."""
    n = int(n)
    if n <= 0:
        return ""
    return "".join(chr(random.randrange(32, 127)) for _ in range(n))


def toupper_char(c: int) -> int:
    c = int(c)
    return c + ord('A') - ord('a') if ord('a') <= c <= ord('z') else c


def tolower_char(c: int) -> int:
    c = int(c)
    return c + ord('a') - ord('A') if ord('A') <= c <= ord('Z') else c


def is_digit(c: int) -> int:
    return 1 if ord('0') <= int(c) <= ord('9') else 0


def is_alpha(c: int) -> int:
    c = int(c)
    return 1 if (ord('a') <= c <= ord('z') or ord('A') <= c <= ord('Z')) else 0


def fs_mounted() -> int:
    return 1 if _fs_mounted else 0


def fopen_append(fd: int, s: str) -> int:
    """Escribe `s` al final del archivo del fd; bytes escritos o -1 si el fd
    es inválido. Equivale a fappend en el kernel."""
    fd, s = int(fd), str(s)
    for h in _fs_files.values():
        if h["fd"] == fd:
            h["data"] += s
            return len(s)
    return -1
