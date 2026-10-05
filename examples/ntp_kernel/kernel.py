"""ntp_kernel — pyos pregunta la hora real por NTP, a internet de verdad.

Resuelve pool.ntp.org por DNS real (vía el proxy DNS de QEMU) y le manda
un paquete NTP real (RFC 958) para traer la hora UTC actual. Nada de esto
es simulado: el kernel no tiene reloj propio (RTC) todavía -- la única
hora que conoce es la que le contesta la red.

Nota de desarrollo: en el entorno donde se escribió esto, el tráfico
UDP al puerto 123 (NTP) está bloqueado a nivel de red (verificado: ni
siquiera un script de Python corriendo directo en esa máquina, sin QEMU
de por medio, puede recibir respuesta de un servidor NTP real) -- así
que el paquete sale bien formado y le llega al servidor real (confirmado
con captura de paquetes), pero la respuesta nunca puede volver. En una
red sin esa restricción (tu PC, por ejemplo) debería funcionar igual que
el resto de esta fase (DNS, TCP, HTTP) ya probado en vivo.

Probalo de verdad:
    pyos build examples/ntp_kernel/kernel.py -o ntp.iso
    qemu-system-i386 -cdrom ntp.iso -netdev user,id=n0 \
        -device rtl8139,netdev=n0 -display none -serial stdio

Simulalo sin compilar nada (usa la hora de TU máquina, no la de ningún
servidor NTP real):
    pyos simulate examples/ntp_kernel/kernel.py
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos - hora real por NTP\n")
    pyos.draw("---------------------------\n")

    if pyos.net_init() == 0:
        pyos.draw("no se encontro una NIC rtl8139\n")
        pyos.halt()

    pyos.net_status()

    pyos.draw("\nresolviendo pool.ntp.org por DNS...\n")
    ip = pyos.resolve("pool.ntp.org")
    if ip == "":
        pyos.draw("no se pudo resolver (sin salida a internet desde este QEMU)\n")
        pyos.halt()
    pyos.draw("pool.ntp.org -> " + ip + "\n")

    pyos.draw("\npidiendo la hora por NTP...\n")
    hora = pyos.ntp_datetime(ip)
    if hora == "":
        pyos.draw("sin respuesta del servidor NTP\n")
    else:
        pyos.draw("hora real (UTC): " + hora + "\n")

    pyos.draw("\nlisto.\n")
    pyos.halt()
