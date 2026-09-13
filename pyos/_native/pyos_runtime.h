/* pyos_runtime.h — API que el código transpilado (generated.c) puede llamar.
 * Esta es la superficie real disponible para el Python del usuario: si una
 * función no está acá, no existe en el kernel.
 */
#ifndef PYOS_RUNTIME_H
#define PYOS_RUNTIME_H

void pyos_clear(void);
void pyos_putc(char c);
void pyos_draw(const char* s);
void pyos_log(const char* s);
void pyos_log_char(char c);
void pyos_halt(void);
void pyos_reboot(void);
void pyos_kb_init(void);
int pyos_readline(void);
int pyos_kb_char(int i);

#endif
