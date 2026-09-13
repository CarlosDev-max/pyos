# __MYOS_NAME__ (plantilla: strings)

Mi OS de manipulación de strings con [pyos](https://github.com/CarlosDev-max/pyos).

Demuestra los strings dinámicos reales del heap: `pyos.line`, `pyos.str_len`,
`pyos.upper`, `pyos.lower`, `pyos.reverse`, `pyos.pad_left`, `pyos.starts_with`,
`pyos.str_contains`, `pyos.int_to_hex`, `pyos.int_to_bin`, `pyos.str_to_int`,
`pyos.str_char_at`.

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

Ver el README de pyos para el subconjunto de Python soportado.