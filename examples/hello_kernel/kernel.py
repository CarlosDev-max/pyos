"""hello_kernel — el "hola mundo" de pyos.

Probalo sin compilar nada (corre bajo CPython normal, en tu terminal):
    python examples/hello_kernel/kernel.py
    # o: pyos simulate examples/hello_kernel/kernel.py

Compilalo de verdad a una ISO booteable:
    pyos build examples/hello_kernel/kernel.py -o hello.iso
    qemu-system-i386 -cdrom hello.iso
"""

import pyos


def contar_hasta(n):
    i = 0
    while i < n:
        pyos.log("contando...\n")
        i += 1
    return i


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos v0.1 — hello from bare metal\n")
    pyos.draw("------------------------------------\n")

    total = contar_hasta(5)
    if total == 5:
        pyos.draw("contador OK\n")
    else:
        pyos.draw("algo salio mal\n")

    for i in range(3):
        pyos.draw("pyos > listo\n")

    pyos.halt()
