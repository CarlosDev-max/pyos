"""__MYOS_NAME__ — la biblioteca matemática de pyos en acción.

Creado con `pyos new __MYOS_SLUG__ --template math`. Muestra una parte de la
API de matemáticas enteras (pyos/math.c): primos, fibonacci, gcd/lcm,
factorial, raíz cuadrada entera, potencias, suma de dígitos y un dado de 6.

  Simulación:  pyos simulate kernel.py
  Real:        pyos build kernel.py -o __MYOS_SLUG__.iso
"""

import pyos


def contar_primos(limite):
    c = 0
    i = 2
    while i < limite:
        if pyos.is_prime(i):
            c = c + 1
        i = i + 1
    return c


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("== __MYOS_NAME__: matematica ==\n\n")
    pyos.draw("primos < 50: ")
    i = 2
    while i < 50:
        if pyos.is_prime(i):
            pyos.draw(str(i))
            pyos.draw(" ")
        i = i + 1
    pyos.draw("\n")
    pyos.draw("fib(15) = " + str(pyos.fib(15)) + "\n")
    pyos.draw("gcd(48, 36) = " + str(pyos.gcd(48, 36)) + "\n")
    pyos.draw("lcm(4, 6) = " + str(pyos.lcm(4, 6)) + "\n")
    pyos.draw("factorial(10) = " + str(pyos.factorial(10)) + "\n")
    pyos.draw("2^10 = " + str(pyos.pow(2, 10)) + "\n")
    pyos.draw("sqrt_int(12345) = " + str(pyos.sqrt_int(12345)) + "\n")
    pyos.draw("sum_digits(9876) = " + str(pyos.sum_digits(9876)) + "\n")
    pyos.draw("reverse_int(9876) = " + str(pyos.reverse_int(9876)) + "\n")
    pyos.draw("hay " + str(contar_primos(100)) + " primos < 100\n")
    pyos.draw("random_int(100) = " + str(pyos.random_int(100)) + "\n")
    pyos.draw("tirar dado x10: ")
    d = 0
    while d < 10:
        pyos.draw(str(pyos.roll(6)))
        pyos.draw(" ")
        d = d + 1
    pyos.draw("\n\nPresiona una tecla para continuar...\n")
    pyos.readline()
    pyos.halt()