# __MYOS_NAME__ (plantilla: math)

Mi OS con la biblioteca matemática de [pyos](https://github.com/CarlosDev-max/pyos).

Muestra la API de matemáticas enteras: `pyos.is_prime`, `pyos.fib`,
`pyos.gcd`, `pyos.lcm`, `pyos.factorial`, `pyos.pow`, `pyos.sqrt_int`,
`pyos.sum_digits`, `pyos.reverse_int`, `pyos.random_int`, `pyos.roll`.

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