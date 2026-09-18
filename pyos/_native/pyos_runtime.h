/* pyos_runtime.h — API que el código transpilado (generated.c) puede llamar.
 * Esta es la superficie real disponible para el Python del usuario: si una
 * función no está acá, no existe en el kernel.
 */
#ifndef PYOS_RUNTIME_H
#define PYOS_RUNTIME_H

#include <stdint.h>

void pyos_clear(void);
void pyos_putc(char c);
void pyos_draw(const char* s);
void pyos_log(const char* s);
void pyos_log_char(char c);
void pyos_log_dec(uint32_t n); /* decimal solo por serie, no toca la VGA */
void pyos_putdec(uint32_t n);
void pyos_halt(void);
void pyos_reboot(void);
void pyos_kb_init(void);
int pyos_readline(void);
int pyos_kb_char(int i);
const char* pyos_line(void);

/* heap.c — memoria dinámica (free-list real) + strings dinámicos */
void pyos_heap_init(uint32_t base, uint32_t size);
void* pyos_alloc(uint32_t size);
void pyos_free(void* ptr);
uint32_t pyos_heap_total(void);
uint32_t pyos_heap_used(void);
uint32_t pyos_heap_free(void);
const char* pyos_strdup(const char* s);
const char* pyos_concat(const char* a, const char* b);
const char* pyos_int_to_str(int value);
int pyos_streq(const char* a, const char* b);

/* speaker.c / rng.c */
void pyos_beep(int freq_hz, int ms);
int pyos_random_int(int n);

/* paging.c — memoria virtual (identity map 16 MiB al boot) */
void pyos_paging_init(void);

/* runtime.c */
const char* pyos_substr(int start, int len);

/* ata.c + myfs.c — Fase 4: disco IDE y filesystem MYOSFS */
void pyos_ata_init(void);
int pyos_fs_init(void);
int pyos_fopen(const char* name, int mode);   /* 0=leer, 1=escribir(truncar) */
int pyos_fwrite(int fd, const char* data);
const char* pyos_fread(int fd, int max);
int pyos_fclose(int fd);
int pyos_fexists(const char* name);
int pyos_fdel(const char* name);
int pyos_fls(void);
int pyos_fsize(int fd);
void pyos_fs_status(void);

/* iso9660.c — lectura del volumen ISO9660 que viaja como módulo multiboot */
void pyos_iso_init(void);
int pyos_iso_status(void);
int pyos_iso_ls(void);
const char* pyos_iso_read(const char* name);
void pyos_iso_free(const char* p);

/* timer.c + proc.c — Fase 5: multitarea preemptiva (round-robin, PIT) */
void pyos_scheduler_init(void);
void pyos_idle_stack_setup(void);
void pyos_timer_init(void);
void pyos_spawn(const char* name, void (*fn)(void));
void pyos_exit_task(void);
void pyos_sleep(uint32_t ms);
void pyos_ps(void);
uint32_t pyos_uptime(void);
uint32_t pyos_ticks(void);

/* proc.c — info de procesos (Tanda D). PID = índice en la tabla de slots. */
int pyos_getpid(void);            /* PID del proceso actual; -1 si ninguno */
int pyos_task_count(void);        /* procesos existentes (slots usados) */
int pyos_task_alive(int pid);     /* 1 si existe y no terminó, 0 si no */
const char* pyos_task_name(int pid);      /* copia del nombre; "" si no existe */
const char* pyos_task_state_str(int pid); /* "corriendo"/"listo"/"durmiendo"/"terminado"/"?" */
const char* pyos_self_name(void);         /* copia del nombre del proceso actual */
int pyos_kill(int pid);           /* marca como terminado; 1 si existía, 0 si no */

/* runtime.c — VGA interna expuesta a vga2.c (Tanda C) */
void pyos_vga_get_cursor(int* x, int* y);       /* col/fila actuales */
void pyos_vga_set_cursor(int x, int y);         /* recorta a 0..79 / 0..24 */
int pyos_vga_get_color(void);                   /* atributo de color 0..255 */
void pyos_vga_set_color(int color);             /* nuevo atributo (0..255) */
void pyos_vga_cell_put(int x, int y, uint16_t cell);   /* pinta una celda */
uint16_t pyos_vga_cell_get(int x, int y);       /* lee una celda */

/* runtime.c — teclado y tiempo (Tanda D) */
extern uint32_t pyos_mem_total_mb;              /* RAM total en MiB (multiboot) */
int pyos_key_available(void);                   /* 1 si hay tecla sin leer */
int pyos_getc_nowait(void);                     /* tecla pendiente o -1 */
int pyos_getc(void);                            /* bloquea hasta la próxima tecla (sin echo) */
int pyos_clear_kb(void);                        /* descarta el búfer de teclado */
int pyos_shift_pressed(void);                   /* 1 si Shift está apretado */
int pyos_caps_active(void);                     /* 1 si Caps Lock está activo */

/* heap.c — contador de la free-list (Tanda E) */
uint32_t pyos_mem_heap_blocks(void);              /* bloques libres en la free-list */

/* myfs.c — consultas (Tanda F) */
int pyos_fs_mounted(void);                      /* 1 si MYOSFS está montado */
int pyos_fopen_append(int fd, const char* text);/* agrega al final; bytes, -1 si fd inválido */

/* iso9660.c — archivos de la raíz del CD (Tanda E) */
int pyos_iso_count(void);                       /* archivos en la raíz; 0 si no hay ISO */

/* math.c — Tanda A: matemática entera */
int pyos_abs(int n);                            /* |n|; INT_MIN -> 2147483647 */
int pyos_sign(int n);                           /* 1 / -1 / 0 */
int pyos_is_even(int n);                        /* 0/1 */
int pyos_is_odd(int n);                         /* 0/1 */
int pyos_pow(int a, int b);                     /* a^b, b>=0, 0^0=1; overflow -> -1 */
int pyos_factorial(int n);                      /* n! n<=12; n>12 o n<0 -> -1 */
int pyos_fib(int n);                            /* F(n), n<=40; fuera de rango -> -1 */
int pyos_gcd(int a, int b);                     /* MCD en valor absoluto */
int pyos_lcm(int a, int b);                     /* MCM; 0 si a==0 o b==0; -1 si overflow */
int pyos_sqrt_int(int n);                       /* piso de sqrt; n<=0 -> 0 */
int pyos_is_prime(int n);                       /* 0/1 */
int pyos_next_prime(int n);                     /* próximo primo >= n; -1 si no hay */
int pyos_digit_count(int n);                    /* dígitos decimales; 0 -> 1 */
int pyos_sum_digits(int n);                     /* suma de dígitos del |n| */
int pyos_reverse_int(int n);                    /* dígitos invertidos (con signo); 0 si no entra */
int pyos_is_palindrome_int(int n);              /* 0/1 */
int pyos_min(int a, int b);
int pyos_max(int a, int b);
int pyos_clamp(int n, int lo, int hi);          /* recorta al rango [lo,hi] */
int pyos_is_power_of_two(int n);                /* 0/1 */
int pyos_next_power_of_two(int n);              /* menor potencia de 2 >= n; -1 si no entra */
int pyos_log2_floor(int n);                     /* piso de log2; n<=1 -> 0 */
int pyos_rand_range(int lo, int hi);            /* aleatorio en [lo,hi] */
int pyos_rand_bool(void);                       /* 0 o 1 */
int pyos_roll(int sides);                       /* dado 1..sides; sides<=1 -> 1 */

/* str2.c — Tanda B: strings avanzado sobre el heap */
int pyos_str_len(const char* s);                /* longitud sin el '\0' */
int pyos_str_to_int(const char* s);             /* parsea decimal ('-' inicial); vacío -> 0 */
int pyos_str_char_at(const char* s, int i);     /* código en i; -1 si fuera de rango */
int pyos_str_contains(const char* s, const char* sub);  /* 0/1 */
int pyos_str_index(const char* s, const char* sub);     /* primera posición; -1 si no está */
int pyos_str_count(const char* s, const char* sub);     /* apariciones sin solaparse */
int pyos_starts_with(const char* s, const char* pref);  /* 0/1 */
int pyos_ends_with(const char* s, const char* suf);     /* 0/1 */
const char* pyos_upper(const char* s);          /* copia en mayúsculas */
const char* pyos_lower(const char* s);          /* copia en minúsculas */
const char* pyos_reverse(const char* s);        /* copia invertida */
const char* pyos_trim(const char* s);           /* sin espacios iniciales/finales */
const char* pyos_repeat(const char* s, int n);  /* s * n; n<=0 -> "" */
const char* pyos_left(const char* s, int n);    /* primeros n chars */
const char* pyos_right(const char* s, int n);   /* últimos n chars */
const char* pyos_pad_left(const char* s, int n, int ch); /* rellena a la izquierda */
const char* pyos_pad_right(const char* s, int n, int ch);/* rellena a la derecha */
const char* pyos_replace_char(const char* s, int oldc, int newc);
const char* pyos_int_to_hex(int n);             /* "0x" + hex minúsculas ("0x0" para 0) */
const char* pyos_int_to_bin(int n);             /* "0b" + binario sin ceros ("0b0" para 0) */

/* vga2.c — Tanda C: display sobre el framebuffer VGA texto */
int pyos_gotoxy(int x, int y);                  /* mueve el cursor (se recorta) */
int pyos_get_x(void);                           /* columna actual */
int pyos_get_y(void);                           /* fila actual */
int pyos_set_color(int fg, int bg);             /* atributo = fg | (bg<<4) */
int pyos_get_color(void);                       /* atributo actual */
int pyos_draw_char(int x, int y, int ch);       /* pinta un carácter; 1/0 */
int pyos_draw_at(int x, int y, const char* s);  /* pinta un string sin scrolleo */
int pyos_clr_row(int y);                        /* limpia una fila; 1/0 */
int pyos_fill_screen(int ch);                   /* llena la pantalla */
int pyos_hline(int y, int x1, int x2, int ch);  /* línea horizontal inclusive */
int pyos_vline(int x, int y1, int y2, int ch);  /* línea vertical */
int pyos_box(int x1, int y1, int x2, int y2, int ch);        /* borde; coord inválidas -> 0 */
int pyos_fill_rect(int x1, int y1, int x2, int y2, int ch);  /* rectángulo relleno */
int pyos_screen_w(void);                        /* 80 */
int pyos_screen_h(void);                        /* 25 */
int pyos_cursor_show(int on);                   /* cursor de hardware 1/0 */
int pyos_invert_row(int y);                     /* invierte el atributo de una fila */

/* misc.c — Tanda D (tiempo), E (máquina) y F (chars) */
int pyos_millis(void);                          /* ms desde el boot (ticks*10) */
int pyos_seconds(void);                         /* segundos desde el boot */
int pyos_paging_enabled(void);                  /* 1 (paginación identity activa) */
int pyos_mem_total(void);                       /* RAM total en MiB; 32 si no hay dato */
const char* pyos_cpu_vendor(void);              /* CPUID: "GenuineIntel"/"AuthenticAMD"/"N/A" (alocado) */
int pyos_cpu_has_fpu(void);                     /* CPUID.1:EDX bit 0 */
int pyos_kernel_base(void);                     /* 0x100000 */
const char* pyos_version_string(void);          /* "MYOS v0.7-big" (alocado) */
const char* pyos_rand_str(int n);               /* n chars aleatorios 32..126 */
int pyos_toupper_char(int c);
int pyos_tolower_char(int c);
int pyos_is_digit(int c);                       /* 0/1 */
int pyos_is_alpha(int c);                       /* 0/1 */

/* net.c + rtl8139.c + pci.c — Fase 6: red (rtl8139 + ARP + IP + ICMP) */
int pyos_net_init(void);                        /* busca la NIC por PCI e inicializa */
int pyos_net_ready(void);                       /* 1 si net_init tuvo éxito */
void pyos_net_status(void);                     /* imprime IP/MAC actuales */
const char* pyos_my_ip(void);                   /* "10.0.2.15" (alocado) */
int pyos_scan(void);                            /* barrido ARP de la /24 local */
int pyos_ping(const char* ip);                  /* ICMP echo real, 1=respondio */

#endif