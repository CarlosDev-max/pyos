"""__MYOS_NAME__ — mi OS hecho con pyos.

Creado con `pyos new __MYOS_SLUG__`. Este archivo genera un sistema operativo
real (ISO Multiboot2 booteable). Podés correrlo de dos formas:

  Simulación (sin compilar nada):
      pyos simulate kernel.py

  Real (transpila → C → ELF → ISO → QEMU):
      pyos build kernel.py -o __MYOS_SLUG__.iso
      qemu-system-i386 -cdrom __MYOS_SLUG__.iso

También: pyos build kernel.py --target=linux-init -o __MYOS_SLUG__  (PID 1)
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("Bienvenido a __MYOS_NAME__\n")
    pyos.draw("Esto corre en bare metal, sin Linux debajo.\n")
    pyos.draw("Presiona cualquier tecla...\n")
    pyos.readline()
    pyos.draw("Adios.\n")
    pyos.halt()