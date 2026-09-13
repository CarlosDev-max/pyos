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
import sys

__version__ = "0.1.0"

__all__ = [
    "entry", "draw", "clear", "halt", "log", "log_char",
    "putc", "readline", "kbchar",
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
        atexit.register(func)
    return func


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
    procesando Backspace. En simulación: lee de stdin de la terminal."""
    global _last_readline
    line = sys.stdin.readline()
    _last_readline = line.rstrip("\n").rstrip("\r")
    return len(_last_readline)


def kbchar(i: int) -> int:
    """Devuelve el código ASCII del carácter i (0-based) de la última línea
    leída con readline. -1 si el índice está fuera de rango."""
    global _last_readline
    if i < 0 or i >= len(_last_readline):
        return -1
    return ord(_last_readline[i])


def halt() -> None:
    """Detiene la CPU para siempre. En simulación: no-op (el proceso Python
    termina solo, normalmente, cuando main() retorna)."""
    pass
