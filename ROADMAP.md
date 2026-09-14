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

## ✅ Fase 3 — Memoria real (completa)
- Heap propio con **free-list real**: `pyos_alloc`/`pyos_free` con
  coalescing de bloques adyacentes y estadísticas (ya no es bump-only)
- Paginación identity map de 16 MiB (page tables de 32 bits, bit PG de CR0)
- `pyos_int_to_str` y las concatenaciones de strings pasan por el heap nuevo;
  `kheap_area` de 1 MiB en `.bss`
- Excepciones de la CPU ya se loguean con eip/cs/err desde la Fase 1; con
  paginación activa, un acceso inválido se ve como #PF con su CR2

## ✅ Fase 4 — Filesystem y persistencia (completa)
- ISO9660, solo lectura, del **CD booteado**: GRUB inyecta `/boot/data.iso`
  como módulo multiboot y el kernel lo parsea desde memoria (determinista en
  QEMU; se descartó ATAPI por frágil)
- `pyos.iso_read('archivo')`, `pyos.iso_ls()` y `pyos.iso_free()`
- Driver ATA PIO (28-bit LBA, primario+secundario) en `ata.c` y filesystem
  MYOSFS v1 de escritura sobre disco IDE en `myfs.c` — quedan listos en el
  kernel aunque la demo de arranque usa el CD por módulo

## ✅ Fase 5 — Multitarea (completa)
- Timer PIT canal 0 (IRQ0) a 100 Hz — ya no está enmascarado
- Scheduler **preemptivo** round-robin: quantum de 1 tick, procesos
  suspendidos/retomados por el mismo frame de interrupción (`int_regs_t`)
- `pyos.spawn(nombre, funcion)`, `pyos.sleep(ms)`, `pyos.ps()`,
  `pyos.exit_task()`, `pyos.uptime()`, `pyos.ticks()`
- `sleep()` es un bloqueo real: lanza `int $0x40` (yield por software), el
  switch ocurre en `irq_common` y el proceso retoma exactamente donde durmió
- Proceso `idle` permanente (`hlt`) que corre cuando no hay nada listo;
  demo `examples/tasks_kernel/` con contador + parpadeo + uptime real
- GDT plana propia (cs=0x08/ds=0x10) instalada en `boot.asm` — el iret de la
  multitarea necesita selectores predecibles

## ✅ Extras (fuera de la numeración de fases, ya completos)
- **Biblioteca estándar** (~90 funciones nuevas) sobre lo que ya daban las
  fases 0-5: `math.c` (matemática entera: potencia, factorial, fibonacci,
  primos, gcd/lcm, etc.), `str2.c` (manipulación de strings sobre el heap:
  upper/lower/trim/repeat/pad/replace, parseo, búsqueda), `vga2.c`
  (dibujo directo sobre el framebuffer: gotoxy, color, líneas, cajas,
  rectángulos) y `misc.c` (info de la máquina: CPUID, RAM, tiempo)
- **`pyos new`**: 5 plantillas listas para arrancar un proyecto
  (`basic`, `math`, `strings`, `shell`, `multitask`, `graphics`)
- **Backend `--target=linux-init`**: además de `iso` (bare metal puro),
  `pyos build` puede generar un binario estático para usar como `/init`
  de Linux (PID 1) — la alternativa "más fácil" que se dejó anotada desde
  el principio del proyecto, para quien no necesite ir a bare metal

## 🔜 Fase 6 — Red (opcional/ambicioso)
- Driver de una tarjeta de red simple (rtl8139 o virtio-net, bien soportadas
  por QEMU para poder seguir probando sin hardware real)
- Pila TCP/IP mínima (ARP + IP + UDP alcanza para muchas demos)

## 🔜 Fase 7 — Pulido para "OS real"
- Empaquetar un instalador (copiar la ISO a un disco real, no solo bootear
  desde CD/USB)
- Explorar UEFI además de BIOS/multiboot (más cercano al hardware moderno)
- Documentación de usuario final, no solo de desarrollador

## 🔜 Fase 8 — Gráficos 3D (el cierre del roadmap)
Ser realista acá importa: no hay forma de escribir un driver de GPU real
(NVIDIA/AMD/Intel) a mano — son miles de páginas de specs cerradas, y eso
queda totalmente fuera del alcance de este proyecto. El camino que sí es
viable y 100% real:

- Salir del VGA texto (0xB8000) a un framebuffer lineal de verdad: VESA VBE
  (llamada a BIOS en modo real antes de saltar a protegido) o virtio-gpu de
  QEMU (spec abierta, sirve para probar sin hardware físico)
- Rasterizador de software en C: matrices 4x4, proyección de vértices,
  rasterización de triángulos con z-buffer — todo corre en la CPU, no hay
  aceleración por hardware real
- API en Python vía el transpiler: algo como `pyos.mesh(...)`,
  `pyos.rotate(x, y, z)`, `pyos.render_frame()`
- Depende de la Fase 3 (memoria real, para los buffers de vértices/frame) y
  se beneficia de la Fase 5 (multitarea, para no bloquear teclado/red
  mientras se renderiza)
- Meta razonable y honesta: un cubo girando con sombreado básico (Gouraud),
  no un motor de juegos — pero sería un OS propio con gráficos 3D reales,
  corriendo en hardware real sin Linux debajo

---

Cada fase, al cerrarse, debe dejar: código compilando y booteando de
verdad en QEMU (no simulado), tests en `tests/` que lo prueben, y un
ejemplo en `examples/` que lo muestre funcionando.
