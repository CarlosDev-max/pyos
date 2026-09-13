# pyos

Framework en Python para escribir sistemas operativos reales.

No es un simulador ni un "OS dentro de un juego": el código que escribís con
la API de `pyos` se **transpila a C**, se compila con un **bootloader
multiboot real** (ensamblador + linker script propio) y se empaqueta en una
**ISO booteable de verdad** — arranca en QEMU, VirtualBox, o una PC/VM real
grabando la ISO en un USB. No hay Linux ni ningún otro OS debajo: lo primero
que ejecuta la CPU después del BIOS/UEFI + GRUB es tu código.

```python
import pyos

@pyos.entry
def main():
    pyos.clear()
    pyos.draw("Hello from pyos!\n")
    pyos.halt()
```

```bash
pyos build kernel.py -o hello.iso
qemu-system-i386 -cdrom hello.iso
```

## Por qué existe esto (y qué límite real tiene)

CPython no puede correr en bare metal: necesita memoria virtual, threads del
sistema operativo, malloc con un heap gestionado por el kernel, etc. — cosas
que simplemente no existen en la CPU recién prendida. Por eso **no existe
"un kernel escrito 100% en CPython"** de forma literal.

El camino real que toma `pyos` es: escribís la lógica de tu kernel en un
**subconjunto restringido de Python** (ver más abajo exactamente cuál), y
`pyos.transpiler` lo convierte a C — que sí se puede compilar directo a
código de máquina sin ningún runtime debajo. Es el mismo principio que usan
proyectos reales de "Python en el kernel" (como los ports de MicroPython a
bare metal): un lenguaje de alto nivel para la lógica, corriendo sobre un
runtime mínimo escrito a mano.

La ventaja de este approach para un MVP: es 100% real hoy (compila, linkea y
bootea — probado con QEMU), no depende de portear un intérprete completo, y
el mismo archivo `.py` te sirve para **simular la lógica bajo CPython normal**
(`pyos simulate kernel.py`) antes de compilarla de verdad.

## Instalación

```bash
pip install -e .
```

Necesitás además, instalado en el sistema (Linux):

```bash
sudo apt-get install gcc gcc-multilib nasm grub-pc-bin grub-common xorriso mtools
```

Corré `pyos doctor` para chequear que esté todo.

Para probar la ISO generada, con QEMU instalado (`sudo apt-get install
qemu-system-x86`):

```bash
qemu-system-i386 -cdrom hello.iso
```

## Comandos

- `pyos build archivo.py -o salida.iso` — transpila, compila, linkea y genera
  la ISO booteable real.
- `pyos simulate archivo.py` — corre la lógica bajo CPython normal, sin
  compilar nada (rápido, para iterar).
- `pyos doctor` — chequea que el toolchain (gcc, nasm, grub-mkrescue, xorriso)
  esté instalado.

## Arquitectura

```
pyos/
  __init__.py     → API pública (pyos.entry, pyos.draw, pyos.clear, pyos.log,
                    pyos.halt, pyos.readline, pyos.putc, pyos.kbchar,
                    pyos.log_char). También corren bajo CPython normal, para
                    el modo simulación.
  transpiler.py   → AST de Python → C (subconjunto restringido, ver abajo)
  build.py        → orquesta nasm/gcc/ld/grub-mkrescue sobre el C generado
  cli.py          → `pyos build|simulate|doctor`
  _native/
    boot.asm      → header multiboot1 + entrypoint real (modo protegido 32-bit)
                    + stubs de ISR/IRQ (enmascan estado, llaman al C, iret)
    linker.ld     → coloca el kernel en 1MB, layout de secciones ELF
    runtime.c     → el "libc" del kernel: IDT x86 de 32 bits, remapeo del
                    PIC 8259, driver de teclado PS/2 (IRQ1, scancode set 1,
                    shift/caps lock), buffer de línea estático sin malloc,
                    VGA texto (0xB8000) y puerto serie (COM1). Estas son las
                    únicas funciones que existen en el sistema.
    pyos_runtime.h
examples/
  hello_kernel/    → ejemplo real, probado con QEMU
  keyboard_kernel/ → ejemplo real de entrada por teclado PS/2, probado con QEMU
  shell_kernel/    → una shell interactiva completa (help, clear, echo, info,
                     halt, reboot) corriendo sobre el teclado PS/2
```

Pipeline de `pyos build`:

1. `transpiler.py` parsea tu `.py` con `ast` y genera `generated.c`
2. `nasm` ensambla `boot.asm` → `boot.o`
3. `gcc -m32 -ffreestanding` compila `runtime.c` + `generated.c` → objetos
4. `gcc -T linker.ld -nostdlib` linkea todo → `kernel.elf` (multiboot válido,
   verificado con `grub-file --is-x86-multiboot`)
5. Se arma un árbol `isoroot/boot/grub/grub.cfg` apuntando al kernel
6. `grub-mkrescue` genera la ISO final

## Subconjunto de Python soportado

Esto es lo único que `pyos.transpiler` sabe convertir a C hoy. Cualquier otra
cosa levanta `TranspileError` con la línea exacta, en vez de fallar en
silencio:

- Funciones a nivel de módulo (nada de clases ni closures)
- Una función marcada `@pyos.entry`, sin argumentos — es el punto de arranque
- Tipos: `int` y `str` (los strings solo como literales — todavía no hay
  heap ni concatenación dinámica de strings)
- `if` / `elif` / `else`, `while`, `for x in range(...)`
- Operadores: `+ - * // %`, comparaciones, `and` / `or`, `not`
- Llamadas a `pyos.draw / pyos.clear / pyos.halt / pyos.reboot / pyos.log /
  pyos.log_char / pyos.putc / pyos.readline / pyos.kbchar`, y a `ord('x')`
  como constante de tiempo de compilación (para comparar el ASCII de teclas)
- Llamadas entre funciones definidas en el mismo archivo (solo con `int`)
- Docstrings de módulo y de función (se ignoran, no rompen la compilación)

No soportado (todavía): imports que no sean `pyos`, f-strings, listas/dicts,
excepciones, generadores, clases, recursión con tipos mixtos, concatenación
de strings en runtime.

## Roadmap

- [ ] CLI: plantillas (`pyos new mi_os`) para arrancar un proyecto
- [x] Driver de teclado (IRQ1, puerto 0x60, scancode set 1) + entrada de
  línea (`pyos.readline`, buffer estático sin malloc)
- [x] Shell interactiva real sobre el teclado
- [ ] Heap básico (`malloc`/`free` mínimo) para permitir strings dinámicos
- [ ] Multiboot2 completo (hoy usamos Multiboot1 por simplicidad/compatibilidad)
- [ ] Backend alternativo: `pyos build --target=linux-init` para generar un
  rootfs Linux mínimo con tu código Python como PID 1 (para quien no
  necesite ir a bare metal)
- [ ] Explorar embeber un runtime tipo MicroPython para soportar un
  subconjunto de Python más amplio sin pasar por transpilación C

## Licencia

MIT
