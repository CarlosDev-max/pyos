"""http_kernel — pyos hace un GET HTTP de verdad, con su propio cliente TCP.

Requiere un servidor HTTP alcanzable desde QEMU. Para probarlo contra algo
que corra en tu propia máquina (sin depender de internet), levantá un
servidor simple en el host:

    python3 -m http.server 8000

Y arrancá QEMU mapeando una IP libre del rango interno de QEMU (10.0.2.2
ya está tomada por el gateway, así que usamos 10.0.2.100) hacia ese
servidor del host con `guestfwd`:

    qemu-system-i386 -cdrom http.iso \
        -chardev socket,id=hf1,host=127.0.0.1,port=8000 \
        -netdev user,id=n0,guestfwd=tcp:10.0.2.100:80-chardev:hf1 \
        -device rtl8139,netdev=n0 \
        -serial stdio -display none

Simulalo sin compilar nada (sin red real, devuelve una respuesta de juguete):
    pyos simulate examples/http_kernel/kernel.py
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos - HTTP GET real con el cliente TCP propio\n")
    pyos.draw("------------------------------------------------\n")

    if pyos.net_init() == 0:
        pyos.draw("no se encontro una NIC rtl8139\n")
        pyos.halt()

    pyos.net_status()
    pyos.draw("\nGET http://10.0.2.100:80/ ...\n\n")

    resp = pyos.http_get("10.0.2.100", "/")
    pyos.draw(resp)

    pyos.draw("\n\nlisto.\n")
    pyos.halt()
