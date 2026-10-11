"""udp_server_kernel — servidor UDP de eco con cola de mensajes real.

Escucha en el puerto 9000 y le devuelve a cada remitente lo que le mandó
(con un prefijo). A diferencia de pyos.udp_recv() (que espera UNA
respuesta puntual a algo que mandamos nosotros), pyos.udp_listen() arma
una cola FIFO de verdad: si llegan varios mensajes de remitentes
distintos antes de que el kernel.py termine de procesar el anterior, no
se pierde ninguno.

Arrancá QEMU exponiendo el puerto UDP 9000 del guest en el 9000 de tu
máquina:

    qemu-system-i386 -cdrom udp.iso \\
        -netdev user,id=n0,hostfwd=udp:127.0.0.1:9000-:9000 \\
        -device rtl8139,netdev=n0 -serial stdio -display none

Y desde otra terminal de tu PC mandale mensajes (con netcat):
    echo "hola pyos" | nc -u -w1 127.0.0.1 9000

Simulalo sin compilar nada (usa una cola de juguete con 2 mensajes):
    pyos simulate examples/udp_server_kernel/kernel.py
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos - servidor UDP de eco (cola real)\n")
    pyos.draw("---------------------------------------\n")

    if pyos.net_init() == 0:
        pyos.draw("no se encontro una NIC rtl8139\n")
        pyos.halt()

    pyos.net_status()
    pyos.udp_listen(9000)
    pyos.draw("escuchando UDP en el puerto 9000...\n\n")

    atendidos = 0
    while atendidos < 5:
        msg = pyos.udp_recvfrom(1000)
        if msg == "":
            pyos.draw("(nada por ahora, sigo esperando)\n")
            continue

        quien = pyos.udp_last_ip() + ":" + str(pyos.udp_last_port())
        pyos.draw("de " + quien + " llego: " + msg + "\n")
        pyos.draw("   (en cola todavia: " + str(pyos.udp_available()) + ")\n")

        pyos.udp_reply("pyos te escucho: " + msg)
        atendidos = atendidos + 1

    pyos.draw("\nlisto, atendi " + str(atendidos) + " mensajes.\n")
    pyos.halt()
