"""shell_kernel — MYOS v0.5: shell interactiva con strings dinámicos reales.

A partir de esta versión pyos tiene heap propio (pyos.line() devuelve la
línea completa como string, comparable con == gracias a pyos_streq), así
que los comandos se distinguen por comparación directa en vez de char por
char como en la v0.4.

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
    pyos.draw("echo X   - imprime X\n")
    pyos.draw("info     - informacion del sistema\n")
    pyos.draw("beep     - hace sonar el PC speaker\n")
    pyos.draw("random   - numero aleatorio entre 0 y 99\n")
    pyos.draw("halt     - detiene la CPU\n")
    pyos.draw("reboot   - reinicia la maquina\n")


def cmd_info():
    pyos.draw("MYOS v0.5\n")
    pyos.draw("CPU : x86 32-bit (i386+)\n")
    pyos.draw("RAM : 32 MB\n")
    pyos.draw("VGA : texto 80x25\n")
    pyos.draw("Heap: 256 KiB (bump allocator)\n")
    pyos.draw("Disp: COM1 (log) y teclado PS/2 (IRQ1)\n")


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("MYOS Interactive Shell v0.5\n")
    pyos.draw("Escriba 'help' para ver los comandos.\n\n")

    while True:
        pyos.draw("MYOS> ")
        n = pyos.readline()
        if n < 1:
            continue

        cmd = pyos.line()

        if cmd == "help":
            cmd_help()
        elif cmd == "clear":
            pyos.clear()
            pyos.draw("MYOS Interactive Shell v0.5\n")
        elif cmd == "info":
            cmd_info()
        elif cmd == "beep":
            pyos.draw("beep!\n")
            pyos.beep(880, 150)
        elif cmd == "random":
            r = pyos.random_int(100)
            pyos.draw("numero: " + str(r) + "\n")
        elif cmd == "halt":
            pyos.draw("Deteniendo la CPU.\n")
            pyos.halt()
        elif cmd == "reboot":
            pyos.draw("Reiniciando...\n")
            pyos.reboot()
        else:
            # 'echo algo' — como no hay slicing de strings todavía, el
            # texto a repetir se arma leyendo char por char desde donde
            # termina la palabra 'echo '
            k = 0
            while k < n and pyos.kbchar(k) != ord(" "):
                k = k + 1
            is_echo = (k == 4 and pyos.kbchar(0) == ord("e")
                       and pyos.kbchar(1) == ord("c")
                       and pyos.kbchar(2) == ord("h")
                       and pyos.kbchar(3) == ord("o"))
            if is_echo:
                i = k + 1
                while i < n:
                    pyos.putc(pyos.kbchar(i))
                    i = i + 1
                pyos.putc(10)
            else:
                pyos.draw("comando desconocido: use 'help'\n")
