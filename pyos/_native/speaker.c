/* speaker.c — pyos.beep(freq_hz, ms): sonido real por el PC speaker.
 * Usa el PIT (canal 2) para generar la onda cuadrada a la frecuencia
 * pedida, y el bit 1 de word 0x61 para habilitar el speaker. El delay es
 * un busy-wait calibrado a ojo (no hay timer de milisegundos real todavía
 * — está documentado como aproximado).
 */
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define PIT_FREQ 1193182u

static void speaker_on(uint32_t freq_hz) {
    if (freq_hz == 0) freq_hz = 1;
    uint32_t divisor = PIT_FREQ / freq_hz;
    outb(0x43, 0xB6);                    /* canal 2, modo 3 (onda cuadrada) */
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);               /* prende el gate + el speaker */
}

static void speaker_off(void) {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

/* Delay aproximado, calibrado a ojo para QEMU/hardware real x86 moderno —
 * no es un milisegundo exacto, es "lo bastante audible". */
static void busy_delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 60000; j++) {
            __asm__ volatile ("nop");
        }
    }
}

void pyos_beep(int freq_hz, int ms) {
    if (freq_hz <= 0 || ms <= 0) return;
    speaker_on((uint32_t)freq_hz);
    busy_delay_ms((uint32_t)ms);
    speaker_off();
}
