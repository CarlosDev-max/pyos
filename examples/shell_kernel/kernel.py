"""shell_kernel — MYOS v0.6: shell con heap y filesystem de verdad.

Ya no hay nada virtual acá: pyos.line() lee la línea por IRQ1 (teclado
PS/2), el heap es un free-list real con free(), el disco IDE se monta con
MYOSFS (y se formatea solo la primera vez), y los archivos se escriben y
leen del disco ATA (persisten entre reinicios).

Probalo sin compilar nada (corre bajo CPython normal, en tu terminal):
    pyos simulate examples/shell_kernel/kernel.py

Compilalo de verdad a una ISO booteable y probalo en QEMU:
    pyos build examples/shell_kernel/kernel.py -o shell.iso
    qemu-system-i386 -cdrom shell.iso -drive file=disk.img,format=raw
"""

import pyos


def cmd_help():
    pyos.draw("help        - muestra esta ayuda\n")
    pyos.draw("clear       - limpia la pantalla\n")
    pyos.draw("echo X      - imprime X\n")
    pyos.draw("info        - informacion del sistema\n")
    pyos.draw("mem         - estado del heap dinamico\n")
    pyos.draw("beep        - hace sonar el PC speaker\n")
    pyos.draw("random      - numero aleatorio entre 0 y 99\n")
    pyos.draw("ls          - lista los archivos del disco\n")
    pyos.draw("write       - escribe 'nota.txt' en el disco\n")
    pyos.draw("read        - muestra 'nota.txt'\n")
    pyos.draw("rm          - borra 'nota.txt'\n")
    pyos.draw("iso         - lee 'greeting.txt' del CD booteado\n")
    pyos.draw("halt        - detiene la CPU\n")
    pyos.draw("reboot      - reinicia la maquina\n")


def cmd_info():
    pyos.draw("MYOS v0.6\n")
    pyos.draw("CPU : x86 32-bit (i386+)\n")
    pyos.draw("RAM : 32 MB\n")
    pyos.draw("VGA : texto 80x25\n")
    pyos.draw("Pag : identity 16 MiB (Fase 3)\n")
    pyos.draw("Disk: ATA PIO + MYOSFS v1 (Fase 4)\n")
    pyos.draw("Disp: COM1 + teclado PS/2 (IRQ1)\n")


def cmd_mem():
    pyos.draw("Heap total: ")
    pyos.putdec(pyos.heap_total())
    pyos.draw(" B\n")
    pyos.draw("Heap usado: ")
    pyos.putdec(pyos.heap_used())
    pyos.draw(" B\n")
    pyos.draw("Heap libre: ")
    pyos.putdec(pyos.heap_free())
    pyos.draw(" B\n")


def cmd_ls():
    n = pyos.fls()
    pyos.draw("(total ")
    pyos.putdec(n)
    pyos.draw(" archivos)\n")


def cmd_write():
    fd = pyos.fopen("nota.txt", 1)
    if fd >= 0:
        n = pyos.fwrite(fd, "saludos desde MYOS\nesto persiste en disco\n")
        pyos.draw("escritos ")
        pyos.putdec(n)
        pyos.draw(" bytes en nota.txt\n")
        pyos.fclose(fd)
    else:
        pyos.draw("error: no se pudo abrir nota.txt\n")


def cmd_read():
    if pyos.fexists("nota.txt") != 0:
        fd = pyos.fopen("nota.txt", 0)
        t = pyos.fread(fd, 256)
        pyos.draw(t)
        if t == "":
            pyos.draw("(archivo vacio)\n")
        pyos.fclose(fd)
    else:
        pyos.draw("no existe nota.txt (use 'write')\n")


def cmd_rm():
    r = pyos.fdel("nota.txt")
    if r == 0:
        pyos.draw("borrado\n")
    else:
        pyos.draw("no existe nota.txt\n")


def cmd_iso():
    if pyos.iso_status() == 0:
        pyos.draw("sin CD ISO9660\n")
        return
    n = pyos.iso_ls()
    pyos.draw("CD: ")
    pyos.putdec(n)
    pyos.draw(" archivos\n")
    t = pyos.iso_read("greeting.txt")
    if t != "":
        pyos.draw("greeting.txt: ")
        pyos.draw(t)
        pyos.draw("\n")
        pyos.iso_free(t)
    else:
        pyos.draw("no esta greeting.txt\n")


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("MYOS v0.6 Interactive Shell\n")
    pyos.draw("Escriba 'help' para ver los comandos.\n\n")

    if pyos.fsinit() == 0:
        pyos.draw("AVISO: no hay disco ATA, el filesystem esta inactivo\n")

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
            pyos.draw("MYOS v0.6 Interactive Shell\n")
        elif cmd == "info":
            cmd_info()
        elif cmd == "mem":
            cmd_mem()
        elif cmd == "beep":
            pyos.draw("beep!\n")
            pyos.beep(880, 150)
        elif cmd == "random":
            r = pyos.random_int(100)
            pyos.draw("numero: " + str(r) + "\n")
        elif cmd == "ls":
            cmd_ls()
        elif cmd == "write":
            cmd_write()
        elif cmd == "read":
            cmd_read()
        elif cmd == "rm":
            cmd_rm()
        elif cmd == "iso":
            cmd_iso()
        elif cmd == "halt":
            pyos.draw("Deteniendo la CPU.\n")
            pyos.halt()
        elif cmd == "reboot":
            pyos.draw("Reiniciando...\n")
            pyos.reboot()
        else:
            # 'echo algo...' — el texto a repetir se saca con substr()
            # (los slices de Python no se soportan todavía, esto es el truco)
            is_echo = pyos.substr(0, 4) == "echo"
            if is_echo:
                i = 5
                while i < n:
                    pyos.putc(pyos.kbchar(i))
                    i = i + 1
                pyos.putc(10)
            else:
                pyos.draw("comando desconocido: use 'help'\n")