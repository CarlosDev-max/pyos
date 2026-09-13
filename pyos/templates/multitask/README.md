# __MYOS_NAME__ (plantilla: multitask)

Mi OS multitarea con [pyos](https://github.com/CarlosDev-max/pyos).

`pyos.spawn("nombre", funcion)` lanza procesos que el PIT (IRQ0, 100 Hz)
preempta con round-robin; `pyos.sleep(ms)` los bloquea por tiempo real.
Cada proceso tiene su propio stack en el heap. También usa `pyos.self_name`,
`pyos.task_count`, `pyos.millis` y `pyos.ps`.

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