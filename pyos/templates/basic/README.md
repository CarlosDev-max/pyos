# __MYOS_NAME__

Mi primer OS con [pyos](https://github.com/CarlosDev-max/pyos).

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

## O compilar un /init de Linux (PID 1)

```bash
pyos build kernel.py --target=linux-init -o __MYOS_SLUG__
```

## API disponible

`pyos.draw`, `pyos.clear`, `pyos.putc`, `pyos.putdec`, `pyos.log`,
`pyos.readline`, `pyos.kbchar`, `pyos.line`, `pyos.halt`, `pyos.reboot`,
`pyos.beep`, `pyos.random_int`, `pyos.line`, `pyos.free`, `pyos.strdup`,
`pyos.heap_total`, `pyos.heap_used`, `pyos.heap_free`, `pyos.entry`.

Ver README de pyos para el subconjunto de Python soportado.