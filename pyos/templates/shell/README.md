# __MYOS_NAME__ (plantilla: shell)

Mi shell interactiva con [pyos](https://github.com/CarlosDev-max/pyos).

Comandos: `help`, `info`, `mem`, `date`, `dice`, `beep`, `halt`. Usa
comparación de strings de verdad (`pyos.line()` + `==`), info real de la
máquina (`pyos.cpu_vendor`, `pyos.mem_total`, `pyos.paging_enabled`) y el
heap (`pyos.heap_total/used/free`, `pyos.mem_heap_blocks`).

## Correr la lógica (simulación)

```bash
pip install -e <ruta/pyos>
pyos simulate kernel.py
```

## Compilar la ISO real (Multiboot2)

```bash
pyos build kernel.py -o __MYOS_SLUG__.iso
qemu-system-i386 -cdrom __MYOS_SLUG__.iso
```

Ver el README de pyos para el subconjunto de Python soportado.