"""shell_kernel — MYOS v0.4: una shell interactiva real corriendo en bare metal.

Lee una línea del teclado PS/2 (via IRQ1, con echo en VGA), separa el comando
de sus argumentos a mano (char por char, ya que el subconjunto de pyos no
tiene strings dinámicos todavía) y ejecuta el comando en un bucle infinito.

Probalo sin compilar nada (corre bajo CPython normal, en tu terminal):
    pyos simulate examples/shell_kernel/kernel.py

Compilalo de verdad a una ISO booteable y probalo en QEMU:
    pyos build examples/shell_kernel/kernel.py -o shell.iso
    qemu-system-i386 -cdrom shell.iso
"""

import pyos


def cmd_help():
    pyos.draw("help     - muestra esta ayuda\n")
    pyos.draw("clear    - limpia la pantalla\n")
    pyos.draw("echo     - imprime el texto que le siga\n")
    pyos.draw("info     - información del sistema\n")
    pyos.draw("halt     - detiene la CPU\n")
    pyos.draw("reboot   - reinicia la máquina\n")


def cmd_info():
    pyos.draw("MYOS v0.4\n")
    pyos.draw("CPU : x86 32-bit (i386+)\n")
    pyos.draw("RAM : 32 MB\n")
    pyos.draw("VGA : texto 80x25\n")
    pyos.draw("Disp: COM1 (log) y teclado PS/2 (IRQ1)\n")


def cmd_echo(first, n):
    # first = índice del primer char del argumento, n = largo de la línea
    i = first
    while i < n:
        pyos.putc(pyos.kbchar(i))
        i = i + 1
    pyos.putc(10)


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("MYOS Interactive Shell v0.4\n")
    pyos.draw("Escriba 'help' para ver los comandos.\n\n")

    while True:
        pyos.draw("MYOS> ")
        n = pyos.readline()

        if n < 1:
            continue

        # separar el comando de sus argumentos: el primer "token" termina en
        # el primer espacio (o en el final de la línea)
        k = 0
        while k < n and pyos.kbchar(k) != ord(' '):
            k = k + 1

        if k == 4 \
                and pyos.kbchar(0) == ord('h') \
                and pyos.kbchar(1) == ord('e') \
                and pyos.kbchar(2) == ord('l') \
                and pyos.kbchar(3) == ord('p'):
            cmd_help()
        elif k == 5 \
                and pyos.kbchar(0) == ord('c') \
                and pyos.kbchar(1) == ord('l') \
                and pyos.kbchar(2) == ord('e') \
                and pyos.kbchar(3) == ord('a') \
                and pyos.kbchar(4) == ord('r'):
            pyos.clear()
            pyos.draw("MYOS Interactive Shell v0.4\n")
        elif k == 4 \
                and pyos.kbchar(0) == ord('e') \
                and pyos.kbchar(1) == ord('c') \
                and pyos.kbchar(2) == ord('h') \
                and pyos.kbchar(3) == ord('o'):
            if k < n:
                cmd_echo(k + 1, n)
            else:
                pyos.putc(10)
        elif k == 4 \
                and pyos.kbchar(0) == ord('i') \
                and pyos.kbchar(1) == ord('n') \
                and pyos.kbchar(2) == ord('f') \
                and pyos.kbchar(3) == ord('o'):
            cmd_info()
        elif k == 4 \
                and pyos.kbchar(0) == ord('h') \
                and pyos.kbchar(1) == ord('a') \
                and pyos.kbchar(2) == ord('l') \
                and pyos.kbchar(3) == ord('t'):
            pyos.draw("Deteniendo la CPU.\n")
            pyos.halt()
        elif k == 6 \
                and pyos.kbchar(0) == ord('r') \
                and pyos.kbchar(1) == ord('e') \
                and pyos.kbchar(2) == ord('b') \
                and pyos.kbchar(3) == ord('o') \
                and pyos.kbchar(4) == ord('o') \
                and pyos.kbchar(5) == ord('t'):
            pyos.draw("Reiniciando...\n")
            pyos.reboot()
        else:
            pyos.draw("comando desconocido: use 'help'\n")