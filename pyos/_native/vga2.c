/* vga2.c — display avanzado (Tanda C del roadmap) sobre el framebuffer VGA
 * de texto (0xB8000, 80x25).
 *
 * Reutiliza los accesores que runtime.c expone (pyos_vga_*); el cursor de
 * hardware se controla por los puertos 0x3D4/0x3D5. Convenciones:
 *   - coordenadas fuera de pantalla -> la función devuelve 0 y no dibuja
 *   - las funciones de dibujo devuelven 1 si dibujaron, 0 si no
 *   - silencio en el caso feliz.
 */

#include <stdint.h>

#include "pyos_runtime.h"

#define V2_W 80
#define V2_H 25

static inline uint16_t v2_cell(char c, uint8_t color) {
    return (uint16_t)c | (uint16_t)color << 8;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void v2_cursor_shape(int start, int end) {
    outb(0x3D4, 0x0A); outb(0x3D5, (uint8_t)start);
    outb(0x3D4, 0x0B); outb(0x3D5, (uint8_t)end);
}

static void v2_cursor_move(int x, int y) {
    uint16_t pos = (uint16_t)(y * V2_W + x);
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

int pyos_gotoxy(int x, int y) {
    pyos_vga_set_cursor(x, y); /* recorta a 0..79 / 0..24 */
    return 1;
}

int pyos_get_x(void) {
    int x = 0, y = 0;
    pyos_vga_get_cursor(&x, &y);
    return x;
}

int pyos_get_y(void) {
    int x = 0, y = 0;
    pyos_vga_get_cursor(&x, &y);
    return y;
}

int pyos_set_color(int fg, int bg) {
    int attr = (fg & 0x0F) | ((bg & 0x07) << 4);
    pyos_vga_set_color(attr);
    return attr;
}

int pyos_get_color(void) { return pyos_vga_get_color(); }

/* pinta un carácter (código CP437) en posición absoluta, sin mover el cursor */
int pyos_draw_char(int x, int y, int ch) {
    if (x < 0 || x >= V2_W || y < 0 || y >= V2_H) return 0;
    pyos_vga_cell_put(x, y,
        v2_cell((char)(ch & 0xFF), (uint8_t)pyos_vga_get_color()));
    return 1;
}

/* pinta un string en posición absoluta, sin scrolleo (se recorta al borde
 * derecho); devuelve cuántos caracteres dibujó */
int pyos_draw_at(int x, int y, const char* s) {
    if (x < 0 || x >= V2_W || y < 0 || y >= V2_H) return 0;
    if (!s) return 0;
    uint8_t color = (uint8_t)pyos_vga_get_color();
    int n = 0;
    while (s[n] && (x + n) < V2_W) {
        pyos_vga_cell_put(x + n, y, v2_cell(s[n], color));
        n++;
    }
    return n;
}

int pyos_clr_row(int y) {
    if (y < 0 || y >= V2_H) return 0;
    uint8_t color = (uint8_t)pyos_vga_get_color();
    for (int x = 0; x < V2_W; x++)
        pyos_vga_cell_put(x, y, v2_cell(' ', color));
    return 1;
}

int pyos_fill_screen(int ch) {
    uint8_t color = (uint8_t)pyos_vga_get_color();
    for (int y = 0; y < V2_H; y++)
        for (int x = 0; x < V2_W; x++)
            pyos_vga_cell_put(x, y, v2_cell((char)(ch & 0xFF), color));
    return 1;
}

/* línea horizontal inclusive los bordes (los extremos se ordenan) */
int pyos_hline(int y, int x1, int x2, int ch) {
    if (y < 0 || y >= V2_H) return 0;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (x2 < 0 || x1 >= V2_W) return 0;
    if (x1 < 0) x1 = 0;
    if (x2 >= V2_W) x2 = V2_W - 1;
    uint8_t color = (uint8_t)pyos_vga_get_color();
    for (int x = x1; x <= x2; x++)
        pyos_vga_cell_put(x, y, v2_cell((char)(ch & 0xFF), color));
    return 1;
}

int pyos_vline(int x, int y1, int y2, int ch) {
    if (x < 0 || x >= V2_W) return 0;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (y2 < 0 || y1 >= V2_H) return 0;
    if (y1 < 0) y1 = 0;
    if (y2 >= V2_H) y2 = V2_H - 1;
    uint8_t color = (uint8_t)pyos_vga_get_color();
    for (int y = y1; y <= y2; y++)
        pyos_vga_cell_put(x, y, v2_cell((char)(ch & 0xFF), color));
    return 1;
}

/* rectángulo de borde; coordenadas inválidas -> 0 sin dibujar */
int pyos_box(int x1, int y1, int x2, int y2, int ch) {
    if (x1 < 0 || y1 < 0 || x2 >= V2_W || y2 >= V2_H || x2 < x1 || y2 < y1)
        return 0;
    pyos_hline(y1, x1, x2, ch);
    pyos_hline(y2, x1, x2, ch);
    pyos_vline(x1, y1, y2, ch);
    pyos_vline(x2, y1, y2, ch);
    return 1;
}

int pyos_fill_rect(int x1, int y1, int x2, int y2, int ch) {
    if (x1 < 0 || y1 < 0 || x2 >= V2_W || y2 >= V2_H || x2 < x1 || y2 < y1)
        return 0;
    uint8_t color = (uint8_t)pyos_vga_get_color();
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            pyos_vga_cell_put(x, y, v2_cell((char)(ch & 0xFF), color));
    return 1;
}

int pyos_screen_w(void) { return V2_W; }
int pyos_screen_h(void) { return V2_H; }

/* activa/desactiva el cursor de hardware (a la posición actual si se muestra) */
int pyos_cursor_show(int on) {
    if (on) {
        v2_cursor_shape(14, 15);
        int x = 0, y = 0;
        pyos_vga_get_cursor(&x, &y);
        v2_cursor_move(x, y);
    } else {
        v2_cursor_shape(0x20, 0); /* línea de inicio fuera de rango: oculto */
    }
    return 1;
}

/* invierte el atributo de una fila: el primer plano pasa a fondo y viceversa
 * (se conserva el bit de parpadeo) */
int pyos_invert_row(int y) {
    if (y < 0 || y >= V2_H) return 0;
    for (int x = 0; x < V2_W; x++) {
        uint16_t cell = pyos_vga_cell_get(x, y);
        uint8_t old = (uint8_t)((cell >> 8) & 0xFF);
        uint8_t fg = old & 0x0F;
        uint8_t bg = (old >> 4) & 0x07;
        uint8_t keep = old & 0x80;
        uint8_t inv = (uint8_t)((fg << 4) | bg);
        pyos_vga_cell_put(x, y,
            (uint16_t)((cell & 0xFF) | ((uint16_t)(inv | keep) << 8)));
    }
    return 1;
}