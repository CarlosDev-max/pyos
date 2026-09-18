"""network_kernel — demuestra la Fase 6 del roadmap: red real.

Usa un driver propio de rtl8139 (la NIC que emula QEMU) con una pila
Ethernet + ARP + IP + ICMP mínima, 100% real: se ve con Wireshark en la
interfaz de red de QEMU, no es una simulación.

Requiere arrancar QEMU con una NIC rtl8139 conectada, por ejemplo:
    qemu-system-i386 -cdrom network.iso \\
        -netdev user,id=n0 -device rtl8139,netdev=n0 \\
        -serial stdio -display none

Con "-netdev user" (el modo de red por defecto de QEMU), pyos recibe la IP
10.0.2.15 y puede ver al gateway virtual en 10.0.2.2 — es a quien deberían
responder tanto pyos.scan() como pyos.ping("10.0.2.2"). Con -netdev tap o
un bridge a una red real, van a aparecer los demás hosts de esa red.

Simulalo sin compilar nada (sin NIC real, todo es de juguete):
    pyos simulate examples/network_kernel/kernel.py
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos - Fase 6: red real (rtl8139 + ARP + IP + ICMP)\n")
    pyos.draw("-----------------------------------------------------\n")

    if pyos.net_init() == 0:
        pyos.draw("no se encontro una NIC rtl8139 -- revisa el -device de QEMU\n")
        pyos.halt()

    pyos.net_status()

    pyos.draw("\nescaneando la red local (barrido ARP, puede tardar unos segundos)...\n")
    n = pyos.scan()
    pyos.draw("hosts encontrados: " + str(n) + "\n")

    pyos.draw("\nprobando ping al gateway (10.0.2.2)...\n")
    pyos.ping("10.0.2.2")

    pyos.draw("\nlisto. pyos ya tiene red real.\n")
    pyos.halt()
