"""__MYOS_NAME__ — dibujar con el framebuffer de texto.

Creado con `pyos new __MYOS_SLUG__ --template graphics`. Dibuja cajas,
líneas y un "reloj" de texto usando pyos.gotoxy, pyos.box, pyos.hline,
pyos.vline, pyos.draw_char y pyos.millis, todo sobre la VGA real (0xB8000).

  Simulación:  pyos simulate kernel.py
  Real:        pyos build kernel.py -o __MYOS_SLUG__.iso
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.set_color(14, 0)
    pyos.box(0, 0, 79, 24, 35)
    pyos.hline(2, 4, 75, 45)
    pyos.gotoxy(5, 1)
    pyos.draw("__MYOS_NAME__: arte en VGA")
    x = 10
    while x < 70:
        pyos.vline(x, 5, 18, 61)
        x = x + 6
    pyos.set_color(11, 0)
    pyos.box(6, 21, 40, 23, 35)
    pyos.gotoxy(8, 22)
    pyos.draw("tiempo encendido: ")
    t = 0
    while t < 8:
        pyos.gotoxy(8, 22)
        pyos.draw(str(pyos.seconds()) + " s  ")
        pyos.sleep(250)
        t = t + 1
    pyos.gotoxy(8, 22)
    pyos.draw("listo.             ")
    pyos.set_color(15, 0)
    pyos.gotoxy(5, 23)
    pyos.draw("Presiona una tecla para terminar.")
    pyos.readline()
    pyos.halt()