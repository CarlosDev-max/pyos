/* rtl8139.c — driver real de la NIC RTL8139 (Fase 6 del roadmap).
 *
 * Por qué esta tarjeta: es la que emula QEMU con `-device rtl8139` (y una
 * de las más documentadas en OSDev), con registros simples mapeados a I/O
 * ports — no hace falta MMIO ni descriptores complejos como una NIC
 * moderna. DMA sí es real: la tarjeta escribe/lee directo de RAM física,
 * por eso los buffers de este driver son estáticos y el kernel corre con
 * paginación identity-map (la dirección física == la dirección virtual).
 *
 * TX: 4 descriptores (TSAD/TSD) usados round-robin; se espera el bit TOK
 * antes de reusar cada uno. RX: un buffer en anillo de 8KB (+ margen para
 * el modo WRAP) que la tarjeta llena sola vía DMA; el driver solo mueve
 * el puntero de lectura (CAPR) a medida que consume paquetes — sin
 * interrupciones, todo por polling (alcanza de sobra para ARP/ICMP).
 */
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0,%1"::"a"(v),"Nd"(port)); }
static inline void outw(uint16_t port, uint16_t v) { __asm__ volatile ("outw %0,%1"::"a"(v),"Nd"(port)); }
static inline void outl(uint16_t port, uint32_t v) { __asm__ volatile ("outl %0,%1"::"a"(v),"Nd"(port)); }
static inline uint8_t  inb(uint16_t port) { uint8_t r;  __asm__ volatile ("inb %1,%0":"=a"(r):"Nd"(port)); return r; }
static inline uint32_t inl(uint16_t port) { uint32_t r; __asm__ volatile ("inl %1,%0":"=a"(r):"Nd"(port)); return r; }

extern int pci_find(uint16_t vendor, uint16_t device, uint8_t* bus, uint8_t* dev, uint8_t* fn);
extern uint32_t pci_bar(uint8_t bus, uint8_t dev, uint8_t fn, int n);
extern void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t fn);
extern void pyos_log(const char* s);

#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139

#define RX_BUF_SIZE (8192u + 16u + 1500u)  /* margen para el modo WRAP */
#define TX_BUF_SIZE 1600u
#define TX_RING     4u

static uint8_t rx_buffer[RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t tx_buffers[TX_RING][TX_BUF_SIZE] __attribute__((aligned(4)));

static uint32_t io_base = 0;
static uint32_t rx_offset = 0;
static uint32_t tx_cur = 0;
static uint8_t  mac[6];
static int      ready = 0;

int rtl8139_init(void) {
    uint8_t bus, dev, fn;
    if (!pci_find(RTL_VENDOR, RTL_DEVICE, &bus, &dev, &fn)) {
        pyos_log("net: no se encontro una NIC rtl8139 en el bus PCI\n");
        return 0;
    }
    pci_enable_bus_master(bus, dev, fn);

    uint32_t bar0 = pci_bar(bus, dev, fn, 0);
    io_base = bar0 & 0xFFFFFFFCu; /* BAR0 es I/O space; bit0 es el flag, no dirección */

    outb((uint16_t)(io_base + 0x52), 0x00); /* power on (config1) */

    outb((uint16_t)(io_base + 0x37), 0x10); /* reset */
    int spins = 0;
    while ((inb((uint16_t)(io_base + 0x37)) & 0x10) && spins < 100000) spins++;

    for (int i = 0; i < 6; i++) mac[i] = inb((uint16_t)(io_base + i));

    /* rx_buffer ya arranca en cero (.bss), no hace falta limpiarlo a mano
     * -- y hacerlo con __builtin_memset sobre un buffer de este tamaño
     * termina generando una llamada real a memset(), que no existe sin
     * libc (-nostdlib) */
    outl((uint16_t)(io_base + 0x30), (uint32_t)(uintptr_t)rx_buffer); /* RBSTART */

    outw((uint16_t)(io_base + 0x3C), 0x0000); /* IMR: sin interrupciones, todo por polling */

    /* RCR: wrap + acepta broadcast + multicast + coincidencia física */
    outl((uint16_t)(io_base + 0x44), 0x0000008Fu);

    outb((uint16_t)(io_base + 0x37), 0x0C); /* habilita RX (RE) y TX (TE) */

    rx_offset = 0;
    tx_cur = 0;
    ready = 1;
    pyos_log("net: rtl8139 inicializada\n");
    return 1;
}

int rtl8139_ready(void) { return ready; }

void rtl8139_get_mac(uint8_t out[6]) {
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

/* envía un frame Ethernet crudo (ya armado por net.c). Rellena con ceros
 * hasta el mínimo de 60 bytes que exige el estándar — sin esto, algunos
 * switches/receptores reales descartan el frame. */
void rtl8139_send(const uint8_t* data, int len) {
    if (!ready) return;
    if (len > (int)TX_BUF_SIZE) len = (int)TX_BUF_SIZE;
    int padded = len < 60 ? 60 : len;

    uint8_t* buf = tx_buffers[tx_cur];
    for (int i = 0; i < len; i++) buf[i] = data[i];
    for (int i = len; i < padded; i++) buf[i] = 0; /* pad limpio: nada de basura de un envío anterior */

    uint16_t tsad = (uint16_t)(io_base + 0x20 + tx_cur * 4);
    uint16_t tsd  = (uint16_t)(io_base + 0x10 + tx_cur * 4);
    outl(tsad, (uint32_t)(uintptr_t)buf);
    outl(tsd, (uint32_t)padded); /* escribir el tamaño dispara la transmisión */

    /* esperar TOK (bit 0x8000) antes de volver a usar este descriptor */
    int spins = 0;
    while (!(inl(tsd) & 0x8000u) && spins < 200000) spins++;

    tx_cur = (tx_cur + 1) % TX_RING;
}

/* saca UN frame del anillo de recepción, si hay alguno esperando.
 * Devuelve la cantidad de bytes copiados a `out` (sin el CRC final), o 0
 * si no había nada. */
int rtl8139_poll_recv(uint8_t* out, int max) {
    if (!ready) return 0;
    if (inb((uint16_t)(io_base + 0x37)) & 0x01u) return 0; /* CR.BUFE: anillo vacío */

    uint8_t* hdr = rx_buffer + rx_offset;
    uint16_t plen = (uint16_t)(hdr[2] | (hdr[3] << 8));

    if (plen < 4u || plen > 1600u) {
        /* algo se desincronizó: reseteamos el puntero de lectura entero
         * en vez de arriesgar un bucle leyendo basura para siempre */
        rx_offset = 0;
        outw((uint16_t)(io_base + 0x38), 0);
        return 0;
    }

    int datalen = (int)plen - 4; /* sin el CRC de 4 bytes que agrega la NIC */
    int copy = datalen < max ? datalen : max;
    if (copy > 0) {
        for (int i = 0; i < copy; i++) out[i] = hdr[4 + i];
    }

    uint32_t total = (4u + plen + 3u) & ~3u; /* header + payload, alineado a 4 */
    rx_offset += total;
    if (rx_offset >= 8192u) rx_offset -= 8192u;

    outw((uint16_t)(io_base + 0x38), (uint16_t)(rx_offset - 16u)); /* CAPR, con el offset -16 de siempre en esta tarjeta */
    return copy;
}
