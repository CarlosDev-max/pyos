"""__MYOS_NAME__ — jugar con strings de verdad.

Creado con `pyos new __MYOS_SLUG__ --template strings`. pyos tiene strings
dinámicos de verdad (heap free-list): leerlos, compararlos y transformarlos.
Este OS pide una palabra y le aplica upper/lower/reverse/pad, busca
substrings y muestra números en hex y binario.

  Simulación:  pyos simulate kernel.py
  Real:        pyos build kernel.py -o __MYOS_SLUG__.iso
"""

import pyos


@pyos.entry
def main():
    pyos.clear()
    pyos.draw("== __MYOS_NAME__: strings ==\n\n")
    pyos.draw("Escribe una palabra: ")
    pyos.readline()
    w = pyos.line()
    pyos.draw("len(" + w + ") = " + str(pyos.str_len(w)) + "\n")
    pyos.draw("mayus: " + pyos.upper(w) + "\n")
    pyos.draw("minus: " + pyos.lower(w) + "\n")
    pyos.draw("al reves: " + pyos.reverse(w) + "\n")
    pyos.draw("con puntos: [" + pyos.pad_left(w, 15, 46) + "]\n")
    if pyos.starts_with(w, "py") != 0:
        pyos.draw("empieza con 'py'\n")
    if pyos.str_contains(w, "os") != 0:
        pyos.draw("contiene 'os'\n")
    pyos.draw("42 en hex = " + pyos.int_to_hex(42) + "\n")
    pyos.draw("42 en bin = " + pyos.int_to_bin(42) + "\n")
    pyos.draw("'42' parseado = " + str(pyos.str_to_int("42")) + "\n")
    pyos.draw("'a' en codigo = " + str(pyos.str_char_at("hola", 0)) + "\n")
    pyos.draw("\nPresiona una tecla para terminar.\n")
    pyos.readline()
    pyos.halt()