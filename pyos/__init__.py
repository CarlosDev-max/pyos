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


def fsinit() -> int:
    """Monta el disco (MYOSFS) o lo formatea si está vacío. En el kernel real
    lee/escribe el disco ATA; en simulación solo habilita un FS en memoria."""
    global _fs_files
    _fs_files = {}
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

_sim_procs = {}          # nombre -> {"thread", "done"}
_sim_next_pid = 2        # el pid 1 es el proceso inicial
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
        try:
            fn()
        except SystemExit:
            pass
        finally:
            _sim_procs[n]["done"] = True

    _sim_procs[n] = {"thread": threading.Thread(target=_body, daemon=True),
                     "done": False}
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
