"""__MYOS_NAME__ — mini shell interactiva.

Creado con `pyos new __MYOS_SLUG__ --template shell`. Compara los comandos
como strings de verdad (pyos.line() + ==) y usa información de la máquina
real (pyos.cpu_vendor, pyos.mem_total, pyos.millis).

Comandos: help, info, mem, date, dice, beep, halt.

  Simulación:  pyos simulate kernel.py
  Real:        pyos build kernel.py -o __MYOS_SLUG__.iso
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("__MYOS_NAME__ mini shell. Escribe 'help'.\n\n")
    while True:
        pyos.draw("> ")
        n = pyos.readline()
        if n < 1:
            continue
        cmd = pyos.line()
        if cmd == "help":
            pyos.draw("help - esta ayuda\n")
            pyos.draw("info - cpu y memoria\n")
            pyos.draw("mem  - estado del heap\n")
            pyos.draw("date - tiempo encendido\n")
            pyos.draw("dice - tirar un dado\n")
            pyos.draw("beep - sonido\n")
            pyos.draw("halt - apagar\n")
        elif cmd == "info":
            pyos.draw("cpu: " + pyos.cpu_vendor() + "\n")
            pyos.draw("ram: " + str(pyos.mem_total()) + " MiB\n")
            pyos.draw("paginacion: " + str(pyos.paging_enabled()) + "\n")
        elif cmd == "mem":
            pyos.draw("heap total: " + str(pyos.heap_total()) + " B\n")
            pyos.draw("heap usado: " + str(pyos.heap_used()) + " B\n")
            pyos.draw("bloques: " + str(pyos.mem_heap_blocks()) + "\n")
        elif cmd == "date":
            pyos.draw("encendido: " + str(pyos.seconds()) + " s\n")
        elif cmd == "dice":
            pyos.draw("salio " + str(pyos.roll(6)) + "\n")
        elif cmd == "beep":
            pyos.beep(880, 150)
        elif cmd == "halt":
            pyos.draw("apagando.\n")
            pyos.halt()
        else:
            pyos.draw("no conozco ese comando.\n")