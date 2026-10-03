"""server_kernel — pyos sirve HTTP de verdad (TCP server propio).

Esta vez es al revés que http_kernel: en vez de que pyos pida una página,
otra máquina (tu browser, curl) le pide una página A PYOS. Es el mismo
cliente TCP de antes, pero usado como servidor (listen + accept).

Arrancá QEMU exponiendo el puerto 80 del guest en el 8080 de tu máquina:

    qemu-system-i386 -cdrom server.iso \\
        -netdev user,id=n0,hostfwd=tcp:127.0.0.1:8080-:80 \\
        -device rtl8139,netdev=n0 \\
        -serial stdio -display none

Y desde otra terminal de tu PC (no desde dentro de QEMU):
    curl http://127.0.0.1:8080/

Simulalo sin compilar nada (acepta un cliente de juguete):
    pyos simulate examples/server_kernel/kernel.py
"""

import pyos


@pyos.entry
def main():
    PAGE = ("<html><body><h1>Hola desde pyos</h1><p>Esto lo sirvio un kernel "
            "de Python, sin Linux debajo.</p></body></html>")
    pyos.clear()
    pyos.draw("pyos - servidor HTTP propio (TCP server real)\n")
    pyos.draw("-------------------------------------------------\n")

    if pyos.net_init() == 0:
        pyos.draw("no se encontro una NIC rtl8139\n")
        pyos.halt()

    pyos.net_status()
    pyos.tcp_listen(80)
    pyos.draw("escuchando en el puerto 80...\n")

    n = 0
    while n < 3:
        pyos.draw("\nesperando un cliente...\n")
        if pyos.tcp_accept(1000) == 0:
            pyos.draw("(nadie se conecto en el tiempo de espera)\n")
            continue

        cliente = pyos.tcp_peer_ip()
        pyos.draw("cliente conectado: " + cliente + "\n")

        req = pyos.tcp_recv(50)
        pyos.draw("pidio: " + req + "\n")

        body_len = str(len(PAGE))
        resp = ("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: "
                + body_len + "\r\nConnection: close\r\n\r\n" + PAGE)
        pyos.tcp_send(resp)
        pyos.tcp_close()
        pyos.draw("respondido y cerrado.\n")
        n = n + 1

    pyos.draw("\nlisto, atendi " + str(n) + " clientes.\n")
    pyos.halt()
