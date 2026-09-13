import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("pyos multitarea (Fase 5)\n")
    pyos.draw("tiempo inicial: ")
    pyos.putdec(pyos.ticks())
    pyos.draw(" ticks\n")
    pyos.spawn("contador", contador)
    pyos.spawn("parpadeo", parpadeo)

    esperas = 0
    while esperas < 5:
        pyos.sleep(500)
        pyos.putdec(pyos.uptime())
        pyos.draw(" ticks\n")
        esperas = esperas + 1

    pyos.draw("-- ps --\n")
    pyos.ps()
    pyos.halt()


def contador():
    i = 0
    while True:
        i = i + 1
        pyos.draw("V contador ")
        pyos.putdec(i)
        pyos.draw("\n")
        pyos.sleep(200)


def parpadeo():
    while True:
        pyos.draw("* blink *\n")
        pyos.sleep(400)