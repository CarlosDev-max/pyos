"""__MYOS_NAME__ — multitarea preemptiva de verdad.

Creado con `pyos new __MYOS_SLUG__ --template multitask`. Varios procesos
corren en la misma CPU: el PIT (IRQ0, 100 Hz) los preempta con round-robin
y pyos.sleep() los bloquea por milisegundos reales. Cada proceso tiene su
propio stack en el heap.

  Simulación:  pyos simulate kernel.py
  Real:        pyos build kernel.py -o __MYOS_SLUG__.iso
"""

import pyos


def contador():
    v = 0
    while True:
        pyos.sleep(200)
        v = v + 1
        pyos.draw("contador " + pyos.self_name() + ": " + str(v) + "\n")


def parpadeo():
    while True:
        pyos.sleep(400)
        pyos.draw("* " + pyos.self_name() + " *\n")


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("== __MYOS_NAME__: multitarea ==\n\n")
    pyos.spawn("contador", contador)
    pyos.spawn("parpadeo", parpadeo)
    t = 0
    while t < 5:
        pyos.sleep(500)
        t = t + 1
        pyos.draw("tick " + str(t) + " en " + str(pyos.millis()) + " ms\n")
    pyos.draw("\nprocesos: " + str(pyos.task_count()) + "\n")
    pyos.ps()
    pyos.halt()