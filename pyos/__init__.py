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

__version__ = "0.5.0"

__all__ = [
    "entry", "draw", "clear", "halt", "reboot", "log", "log_char",
    "putc", "readline", "kbchar", "line", "beep", "random_int",
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


def halt() -> None:
    """Detiene la CPU para siempre. En simulación: termina el programa de
    forma limpia (lo más parecido a apagar la máquina en una terminal)."""
    raise SystemExit(0)


def reboot() -> None:
    """Reinicia la máquina (pulso de reset por el controller 8042).
    En simulación: termina el programa de forma limpia."""
    raise SystemExit(0)
