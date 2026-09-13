# Roadmap de pyos — fases hacia un OS real

Cada fase agrega una capa real de sistema operativo (no simulada) sobre la
anterior. El orden importa: cada una depende de que la previa esté sólida
(no tiene sentido un filesystem sin memoria dinámica confiable, ni
multitarea sin poder reservar y liberar memoria por proceso).

## ✅ Fase 0 — Arranque real (completa)
- Bootloader multiboot (ensamblador + GRUB), sin Linux debajo
- Kernel en 1MB, linker script propio
- VGA texto (0xB8000) + puerto serie COM1 para debug
- `pyos.transpiler`: funciones, if/while/for, aritmética, comparaciones
- CLI: `pyos build / simulate / doctor`

## ✅ Fase 1 — Interrupciones y teclado (completa)
- IDT (256 vectores) + remapeo del PIC 8259
- Driver de teclado PS/2 real (IRQ1), scancode set 1, Shift + Caps Lock
- `pyos.readline()` con echo y Backspace, `pyos.kbchar()`
- `pyos.reboot()` (reset vía controller 8042)

## ✅ Fase 2 — Heap y strings dinámicos (completa)
- Heap propio (bump allocator, 256 KiB)
- `pyos.line()` + comparación `==`/`!=` de strings (`pyos_streq`)
- Concatenación (`a + b` entre strings) y `str(x)` para convertir int→str
- Bonus: `pyos.beep()` (PC speaker real vía PIT) y `pyos.random_int()` (LCG con semilla de RDTSC)
- CP437: los acentos (á, é, í, ó, ú, ñ) ya se ven bien en la VGA

## 🔜 Fase 3 — Memoria real
- Reemplazar el bump allocator por un allocator con `free()` de verdad
  (free-list o buddy allocator simple)
- Paginación (page tables de 32 bits, activar el bit PG de CR0)
- Protección de memoria básica: separar código/datos del kernel de lo que
  el usuario transpile, detectar accesos inválidos (page fault ya se loguea
  desde la Fase 1, falta *hacer algo* con eso más que frenar)

## 🔜 Fase 4 — Filesystem y persistencia
- Leer el propio CD-ROM booteado (ISO9660, solo lectura) para que `pyos`
  pueda cargar archivos empaquetados en la ISO
- Un filesystem simple de escritura sobre un disco virtual (FAT16 mínimo, o
  uno propio) para persistencia real entre reinicios
- API en Python: `pyos.fopen/fread/fwrite` o similar

## 🔜 Fase 5 — Multitarea
- Habilitar el timer (PIT, IRQ0 — hoy está enmascarado a propósito)
- Scheduler cooperativo primero (yield explícito), preemptivo después
- Contextos de CPU por tarea (guardar/restaurar registros vía el struct
  `int_regs_t` que ya existe en `runtime.c`)
- API en Python: `pyos.spawn(func)` para lanzar una función como "proceso"

## 🔜 Fase 6 — Red (opcional/ambicioso)
- Driver de una tarjeta de red simple (rtl8139 o virtio-net, bien soportadas
  por QEMU para poder seguir probando sin hardware real)
- Pila TCP/IP mínima (ARP + IP + UDP alcanza para muchas demos)

## 🔜 Fase 7 — Pulido para "OS real"
- Empaquetar un instalador (copiar la ISO a un disco real, no solo bootear
  desde CD/USB)
- Explorar UEFI además de BIOS/multiboot (más cercano al hardware moderno)
- Documentación de usuario final, no solo de desarrollador

---

Cada fase, al cerrarse, debe dejar: código compilando y booteando de
verdad en QEMU (no simulado), tests en `tests/` que lo prueben, y un
ejemplo en `examples/` que lo muestre funcionando.
