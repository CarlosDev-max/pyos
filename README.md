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

- `pyos new directorio [-t PLANTILLA] [--name NOMBRE]` — crea un proyecto nuevo
  desde una plantilla. Plantillas: `basic` (hello world), `math`, `strings`,
  `shell`, `multitask` y `graphics`.
- `pyos build archivo.py -o salida.iso [--target iso|linux-init]` — transpila,
  compila y linkea. Con `--target iso` (default) genera la ISO booteable
  bare-metal real; con `--target linux-init` genera en cambio un binario
  estático para usar como `/init` de Linux (PID 1) — la alternativa para
  quien no necesite ir a bare metal.
- `pyos simulate archivo.py` — corre la lógica bajo CPython normal, sin
  compilar nada (rápido, para iterar).
- `pyos doctor` — chequea que el toolchain (gcc, nasm, grub-mkrescue, xorriso)
  esté instalado.
- `pyos help` (o `pyos` sin argumentos) — muestra esta ayuda.

## Arquitectura

```
pyos/
  __init__.py     → API pública (pyos.entry, pyos.draw, pyos.clear, pyos.log,
                    pyos.halt, pyos.reboot, pyos.readline, pyos.putc,
                    pyos.kbchar, pyos.log_char, pyos.line, pyos.beep,
                    pyos.random_int, pyos.iso_read/iso_ls/iso_free,
                    pyos.spawn, pyos.sleep, pyos.ps, pyos.uptime, pyos.ticks,
                    pyos.exit_task).
                    También corren bajo CPython normal, para el modo
                    simulación.
  transpiler.py   → AST de Python → C (subconjunto restringido, ver abajo)
  build.py        → orquesta nasm/gcc/ld/grub-mkrescue sobre el C generado
  cli.py          → `pyos build|simulate|doctor`
  _native/
    boot.asm      → header multiboot1 + entrypoint real (modo protegido 32-bit)
                    + GDT plana propia + stubs de ISR/IRQ (enmascan estado,
                    llaman al C, iret) + yield por software (int $0x40)
    linker.ld     → coloca el kernel en 1MB, layout de secciones ELF
    runtime.c     → el "libc" del kernel: IDT x86 de 32 bits, remapeo del
                    PIC 8259, driver de teclado PS/2 (IRQ1, scancode set 1,
                    shift/caps lock), buffer de línea estático sin malloc,
                    VGA texto (0xB8000) y puerto serie (COM1), dispatch de
                    IRQ + handler de excepciones
    heap.c        → heap propio con free-list real y coalescing de bloques
    paging.c      → paginación identity map de 16 MiB
    timer.c       → PIT canal 0 (IRQ0) a 100 Hz — el tick de la multitarea
    proc.c        → scheduler round-robin preemptivo: spawn/sleep/ps/exit,
                    proceso idle, contextos por frame de interrupción
    ata.c         → driver ATA PIO (28-bit LBA) para discos IDE
    myfs.c        → filesystem MYOSFS v1 de escritura sobre ATA
    iso9660.c     → lector ISO9660 (solo lectura) del CD/módulo multiboot
    speaker.c     → pyos.beep() real por el PC speaker (PIT canal 2)
    rng.c         → pyos.random_int() — LCG sembrado con RDTSC
    pyos_runtime.h
examples/
  hello_kernel/    → ejemplo real, probado con QEMU
  keyboard_kernel/ → ejemplo real de entrada por teclado PS/2, probado con QEMU
  shell_kernel/    → shell interactiva completa (help, clear, echo, info,
                     beep, random, halt, reboot) con comandos comparados
                     como strings de verdad (pyos.line() + ==)
  tasks_kernel/    → multitarea real (Fase 5): contador + parpadeo + uptime,
                     probado con QEMU
```

Pipeline de `pyos build`:

1. `transpiler.py` parsea tu `.py` con `ast` y genera `generated.c`
2. `nasm` ensambla `boot.asm` → `boot.o`
3. `gcc -m32 -ffreestanding` compila `runtime.c`, `heap.c`, `paging.c`,
   `timer.c`, `proc.c`, `ata.c`, `myfs.c`, `iso9660.c`, `speaker.c`,
   `rng.c` y `generated.c` → objetos
4. `gcc -T linker.ld -nostdlib` linkea todo → `kernel.elf` (multiboot válido,
   verificado con `grub-file --is-x86-multiboot`)
5. Se arma un árbol `isoroot/boot/grub/grub.cfg` apuntando al kernel; si el
   build recibe archivos extra, se empaquetan en `data.iso` y GRUB los carga
   como módulo multiboot
6. `grub-mkrescue` genera la ISO final

## Subconjunto de Python soportado

Esto es lo único que `pyos.transpiler` sabe convertir a C hoy. Cualquier otra
cosa levanta `TranspileError` con la línea exacta, en vez de fallar en
silencio:

- Funciones a nivel de módulo (nada de clases ni closures)
- Una función marcada `@pyos.entry`, sin argumentos — es el punto de arranque
- Tipos: `int` y `str`. Los `str` pueden ser literales, el resultado de
  concatenar dos `str` con `+`, o de convertir un `int` con `str(x)`
- `if` / `elif` / `else`, `while`, `for x in range(...)`, `break`, `continue`
- Operadores en `int`: `+ - * // %`, comparaciones, `and` / `or`, `not`
- En `str`: `+` concatena (heap propio), `==`/`!=` comparan de verdad
  (`pyos_streq`) — no hay `<`/`>` entre strings todavía
- `ord('x')` como constante de compilación; `str(x)` convierte int→str
- Llamadas a `pyos.*`: más de 90 funciones hoy. Las básicas son `draw, clear,
  halt, reboot, log, log_char, putc, readline, kbchar, line, beep,
  random_int, iso_read, iso_ls, iso_free, spawn, sleep, ps, uptime, ticks,
  exit_task`; además hay matemática entera (abs, pow, fib, gcd, is_prime,
  sqrt_int, roll, ...), strings avanzados (upper, lower, reverse, trim, pad,
  int_to_hex, str_contains, ...), VGA (gotoxy, box, hline, vline, draw_at,
  set_color, ...), teclado (key_available, getc, ...), procesos
  (getpid, self_name, task_count, kill, ...) e info de máquina (cpu_vendor,
  mem_total, version_string, ...). Lista completa en
  `pyos/transpiler.py:_RUNTIME_CALLS`.
- Llamadas entre funciones definidas en el mismo archivo (solo con `int`)
- Docstrings de módulo y de función (se ignoran, no rompen la compilación)
- Los acentos españoles comunes (á é í ó ú ñ Ñ ü Ü ¿ ¡) se mapean a CP437
  automáticamente para que se vean bien en la VGA

No soportado (todavía): imports que no sean `pyos`, f-strings, slicing de
strings, listas/dicts, excepciones, generadores, clases, recursión con tipos
mixtos.

## Roadmap

Ver **[ROADMAP.md](ROADMAP.md)** — organizado en fases (arranque →
interrupciones/teclado → heap/strings → memoria real → filesystem →
multitarea → red → pulido), cada una con lo que ya funciona y lo que sigue.

## Licencia

MIT
