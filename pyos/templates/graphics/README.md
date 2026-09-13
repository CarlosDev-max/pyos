# __MYOS_NAME__ (plantilla: graphics)

Mi OS de dibujo VGA con [pyos](https://github.com/CarlosDev-max/pyos).

Dibuja cajas, líneas y un reloj de texto sobre la VGA real (0xB8000) con
`pyos.set_color`, `pyos.box`, `pyos.hline`, `pyos.vline`, `pyos.gotoxy`,
`pyos.draw_char`, `pyos.seconds` y `pyos.sleep`.

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