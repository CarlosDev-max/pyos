"""dhcp_kernel — pyos consigue una IP real por DHCP, en vez de la fija.

Hasta ahora pyos arrancaba siempre con 10.0.2.15 (la que asigna por
defecto el modo "user" de QEMU). Esto negocia DHCP de verdad --
DISCOVER -> OFFER -> REQUEST -> ACK -- contra el servidor DHCP real de
la red (SLIRP, en este caso, que también lo trae integrado).

Probalo de verdad:
    pyos build examples/dhcp_kernel/kernel.py -o dhcp.iso
    qemu-system-i386 -cdrom dhcp.iso -netdev user,id=n0 \
        -device rtl8139,netdev=n0 -display none -serial stdio

Simulalo sin compilar nada (IP de juguete fija):
    pyos simulate examples/dhcp_kernel/kernel.py
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos - DHCP real (DISCOVER/OFFER/REQUEST/ACK)\n")
    pyos.draw("------------------------------------------------\n")

    if pyos.net_init() == 0:
        pyos.draw("no se encontro una NIC rtl8139\n")
        pyos.halt()

    pyos.draw("IP antes de DHCP (la fija por defecto): " + pyos.my_ip() + "\n")
    pyos.draw("\nnegociando DHCP...\n")

    if pyos.dhcp_configure(200) == 0:
        pyos.draw("DHCP fallo, me quedo con la IP fija.\n")
    else:
        pyos.draw("DHCP OK!\n")

    pyos.net_status()

    pyos.draw("\nprobando ping al gateway con la IP nueva...\n")
    pyos.ping("10.0.2.2")

    pyos.draw("\nlisto.\n")
    pyos.halt()
