"""keyboard_kernel — prueba real del teclado PS/2 en MYOS.

Arranca mostrando un prompt y vuelca a VGA todo lo que se escribe:
letras, números, espacio, Enter, Backspace, Shift y Caps Lock son
procesados por el driver en el IRQ1 (el teclado real de QEMU).

Probalo sin compilar nada (corre bajo CPython normal, en tu terminal):
    pyos simulate examples/keyboard_kernel/kernel.py

Compilalo de verdad a una ISO booteable y probalo en QEMU:
    pyos build examples/keyboard_kernel/kernel.py -o keyboard.iso
    qemu-system-i386 -cdrom keyboard.iso
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("MYOS Keyboard Test\n")
    pyos.draw("Type something:\n> ")

    n = pyos.readline()

    pyos.draw("\nYou typed: ")
    for i in range(n):
        pyos.putc(pyos.kbchar(i))
    pyos.draw(" (done)\n")

    pyos.log("kb: line received: ")
    for i in range(n):
        pyos.log_char(pyos.kbchar(i))
    pyos.log("\n")

    pyos.halt()