/* net.c — Ethernet + ARP + IP + ICMP mínimos, sobre el driver de rtl8139.c
 * (Fase 6 del roadmap).
 *
 * Configuración: IP estática 10.0.2.15/24 — es la IP que le asigna por
 * defecto el modo de red "user" (SLIRP) de QEMU al invitado, así que
 * `qemu-system-i386 ... -netdev user,id=n0 -device rtl8139,netdev=n0`
 * funciona sin tocar nada. Con `-netdev tap` o un bridge a una red real,
 * ajustar MY_IP a algo válido en esa red.
 *
 * No hay DHCP (mucho más complejo que ARP+IP+ICMP a nivel de paquete) ni
 * TCP/UDP todavía — eso queda para una vuelta futura de esta fase. Lo que
 * sí hay es 100% real: se ve con Wireshark en la interfaz de QEMU.
 */
#include <stdint.h>
#include <stddef.h>

extern void pyos_log(const char* s);
extern void pyos_draw(const char* s);
extern void* pyos_alloc(uint32_t size);
extern uint32_t pyos_ticks(void);

extern int  rtl8139_init(void);
extern int  rtl8139_ready(void);
extern void rtl8139_get_mac(uint8_t out[6]);
extern void rtl8139_send(const uint8_t* data, int len);
extern int  rtl8139_poll_recv(uint8_t* out, int max);

static uint8_t my_mac[6];
static uint8_t my_ip[4] = {10, 0, 2, 15};
static int net_ready = 0;

#define ETH_BROADCAST { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
#define ET_ARP 0x0806u
#define ET_IP  0x0800u

/* --------------------------- utilidades --------------------------------- */

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint16_t ip_checksum(const void* data, int len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len == 1) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static int ip_eq(const uint8_t a[4], const uint8_t b[4]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static void draw_ip(const uint8_t ip[4]) {
    static const char digits[] = "0123456789";
    char buf[4]; 
    for (int k = 0; k < 4; k++) {
        int v = ip[k], i = 3;
        char tmp[4];
        int n = 0;
        if (v == 0) tmp[n++] = '0';
        while (v > 0) { tmp[n++] = digits[v % 10]; v /= 10; }
        int j = 0;
        while (n > 0) buf[j++] = tmp[--n];
        buf[j] = 0;
        pyos_draw(buf);
        if (k < 3) pyos_draw(".");
        (void)i;
    }
}

static void draw_mac(const uint8_t mac[6]) {
    static const char hex[] = "0123456789abcdef";
    char buf[3] = {0, 0, 0};
    for (int k = 0; k < 6; k++) {
        buf[0] = hex[mac[k] >> 4];
        buf[1] = hex[mac[k] & 0xF];
        pyos_draw(buf);
        if (k < 5) pyos_draw(":");
    }
}

/* ------------------------------- ARP ------------------------------------- */

#define ARP_CACHE_MAX 32
static uint8_t arp_ip[ARP_CACHE_MAX][4];
static uint8_t arp_mac[ARP_CACHE_MAX][6];
static int arp_n = 0;

static void arp_cache_add(const uint8_t ip[4], const uint8_t mac[6]) {
    for (int i = 0; i < arp_n; i++) {
        if (ip_eq(arp_ip[i], ip)) {
            for (int k = 0; k < 6; k++) arp_mac[i][k] = mac[k];
            return;
        }
    }
    if (arp_n < ARP_CACHE_MAX) {
        for (int k = 0; k < 4; k++) arp_ip[arp_n][k] = ip[k];
        for (int k = 0; k < 6; k++) arp_mac[arp_n][k] = mac[k];
        arp_n++;
    }
}

static void eth_send(const uint8_t dst_mac[6], uint16_t ethertype,
                      const uint8_t* payload, int plen) {
    uint8_t frame[1514];
    for (int i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = my_mac[i];
    wr16(frame + 12, ethertype);
    if (plen > (int)sizeof(frame) - 14) plen = (int)sizeof(frame) - 14;
    for (int i = 0; i < plen; i++) frame[14 + i] = payload[i];
    rtl8139_send(frame, 14 + plen);
}

static void arp_send(uint16_t opcode, const uint8_t dst_mac[6], const uint8_t dst_ip[4]) {
    uint8_t pkt[28];
    wr16(pkt + 0, 1);       /* hw type: Ethernet */
    wr16(pkt + 2, ET_IP);   /* proto type: IPv4 */
    pkt[4] = 6;             /* hw len */
    pkt[5] = 4;             /* proto len */
    wr16(pkt + 6, opcode);
    for (int i = 0; i < 6; i++) pkt[8 + i] = my_mac[i];
    for (int i = 0; i < 4; i++) pkt[14 + i] = my_ip[i];
    for (int i = 0; i < 6; i++) pkt[18 + i] = (opcode == 2) ? dst_mac[i] : 0;
    for (int i = 0; i < 4; i++) pkt[24 + i] = dst_ip[i];

    uint8_t broadcast[6] = ETH_BROADCAST;
    eth_send(opcode == 1 ? broadcast : dst_mac, ET_ARP, pkt, 28);
}

/* -------------------------------- IP/ICMP -------------------------------- */

static uint16_t ip_id_counter = 1;

static void icmp_send(const uint8_t dst_ip[4], const uint8_t dst_mac[6],
                       uint8_t type, uint16_t ident, uint16_t seq,
                       const uint8_t* data, int dlen) {
    uint8_t pkt[20 + 8 + 32];
    if (dlen > 32) dlen = 32;

    uint8_t* ip = pkt;
    ip[0] = 0x45; ip[1] = 0; /* IPv4, header 20B, sin DSCP */
    wr16(ip + 2, (uint16_t)(20 + 8 + dlen));
    wr16(ip + 4, ip_id_counter++);
    wr16(ip + 6, 0);         /* sin fragmentar */
    ip[8] = 64;              /* TTL */
    ip[9] = 1;               /* protocolo ICMP */
    wr16(ip + 10, 0);        /* checksum, se calcula abajo */
    for (int i = 0; i < 4; i++) ip[12 + i] = my_ip[i];
    for (int i = 0; i < 4; i++) ip[16 + i] = dst_ip[i];
    wr16(ip + 10, ip_checksum(ip, 20));

    uint8_t* icmp = pkt + 20;
    icmp[0] = type; icmp[1] = 0; /* code */
    wr16(icmp + 2, 0);           /* checksum, se calcula abajo */
    wr16(icmp + 4, ident);
    wr16(icmp + 6, seq);
    for (int i = 0; i < dlen; i++) icmp[8 + i] = data[i];
    wr16(icmp + 2, ip_checksum(icmp, 8 + dlen));

    eth_send(dst_mac, ET_IP, pkt, 20 + 8 + dlen);
}

/* ------------------------- despacho de lo que llega ----------------------- */

static int  ping_waiting = 0;
static uint16_t ping_ident = 0, ping_seq = 0;
static int  ping_got_reply = 0;

static void handle_frame(const uint8_t* f, int len) {
    if (len < 14) return;
    uint16_t ethertype = rd16(f + 12);
    const uint8_t* payload = f + 14;
    int plen = len - 14;

    if (ethertype == ET_ARP && plen >= 28) {
        uint16_t opcode = rd16(payload + 6);
        uint8_t sender_mac[6], sender_ip[4], target_ip[4];
        for (int i = 0; i < 6; i++) sender_mac[i] = payload[8 + i];
        for (int i = 0; i < 4; i++) sender_ip[i] = payload[14 + i];
        for (int i = 0; i < 4; i++) target_ip[i] = payload[24 + i];

        if (opcode == 2) { /* ARP reply: alguien contestó nuestro scan */
            arp_cache_add(sender_ip, sender_mac);
        } else if (opcode == 1 && ip_eq(target_ip, my_ip)) {
            /* ARP request preguntando por NOSOTROS: contestamos de verdad,
             * así que pyos también responde si algo más de la red lo pinguea */
            arp_send(2, sender_mac, sender_ip);
        }
        return;
    }

    if (ethertype == ET_IP && plen >= 20) {
        const uint8_t* ip = payload;
        int ihl = (ip[0] & 0x0F) * 4;
        uint8_t proto = ip[9];
        uint8_t src_ip[4], dst_ip[4];
        for (int i = 0; i < 4; i++) src_ip[i] = ip[12 + i];
        for (int i = 0; i < 4; i++) dst_ip[i] = ip[16 + i];
        if (proto == 1 && plen >= ihl + 8 && ip_eq(dst_ip, my_ip)) {
            const uint8_t* icmp = ip + ihl;
            uint8_t type = icmp[0];
            uint16_t ident = rd16(icmp + 4);
            uint16_t seq = rd16(icmp + 6);
            if (type == 8) { /* echo request: contestamos con echo reply */
                uint8_t src_mac[6];
                for (int i = 0; i < 6; i++) src_mac[i] = f[6 + i];
                arp_cache_add(src_ip, src_mac);
                int dlen = plen - ihl - 8;
                icmp_send(src_ip, src_mac, 0, ident, seq, icmp + 8, dlen);
            } else if (type == 0 && ping_waiting
                       && ident == ping_ident && seq == ping_seq) {
                ping_got_reply = 1;
            }
        }
    }
}

/* drena todos los frames que haya en el anillo de RX en este instante
 * (sin bloquear) — se llama en bucle desde scan()/ping() con su propio
 * control de tiempo por afuera */
static void net_drain(void) {
    uint8_t buf[1600];
    int n;
    while ((n = rtl8139_poll_recv(buf, sizeof(buf))) > 0) {
        handle_frame(buf, n);
    }
}

/* --------------------------- API expuesta a pyos -------------------------- */

int pyos_net_init(void) {
    if (net_ready) return 1;
    if (!rtl8139_init()) return 0;
    rtl8139_get_mac(my_mac);
    net_ready = 1;
    pyos_log("net: IP estatica ");
    return 1;
}

int pyos_net_ready(void) { return net_ready; }

void pyos_net_status(void) {
    if (!net_ready) { pyos_draw("NET: sin inicializar (llama a pyos.net_init())\n"); return; }
    pyos_draw("NET: rtl8139 lista. IP="); draw_ip(my_ip);
    pyos_draw("  MAC="); draw_mac(my_mac);
    pyos_draw("\n");
}

const char* pyos_my_ip(void) {
    static char buf[16];
    int p = 0;
    for (int k = 0; k < 4; k++) {
        int v = my_ip[k];
        char tmp[4]; int n = 0;
        if (v == 0) tmp[n++] = '0';
        while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
        while (n > 0) buf[p++] = tmp[--n];
        if (k < 3) buf[p++] = '.';
    }
    buf[p] = 0;
    return buf;
}

/* barrido ARP de la /24 local: le pregunta a cada host "quién tiene esta
 * IP" y anota quién contesta. Es la forma real de descubrir vecinos en un
 * segmento Ethernet (lo que hace `nmap -sn` puertas para adentro). */
int pyos_scan(void) {
    if (!net_ready) { pyos_draw("net: llama a pyos.net_init() primero\n"); return 0; }
    arp_n = 0;

    for (int host = 1; host < 255; host++) {
        uint8_t target[4] = { my_ip[0], my_ip[1], my_ip[2], (uint8_t)host };
        if (ip_eq(target, my_ip)) continue;
        uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
        arp_send(1, zero_mac, target);

        uint32_t deadline = pyos_ticks() + 1; /* ~10ms por host a 100Hz */
        while (pyos_ticks() < deadline) net_drain();
    }
    net_drain();

    for (int i = 0; i < arp_n; i++) {
        pyos_draw("host ");
        draw_ip(arp_ip[i]);
        pyos_draw("  mac ");
        draw_mac(arp_mac[i]);
        pyos_draw("\n");
    }
    return arp_n;
}

/* pide por ARP la MAC de dst_ip si no la tenemos, esperando hasta max_ticks */
static int arp_resolve(const uint8_t dst_ip[4], uint8_t out_mac[6], uint32_t max_ticks) {
    for (int i = 0; i < arp_n; i++) {
        if (ip_eq(arp_ip[i], dst_ip)) {
            for (int k = 0; k < 6; k++) out_mac[k] = arp_mac[i][k];
            return 1;
        }
    }
    uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
    arp_send(1, zero_mac, dst_ip);
    uint32_t deadline = pyos_ticks() + max_ticks;
    while (pyos_ticks() < deadline) {
        net_drain();
        for (int i = 0; i < arp_n; i++) {
            if (ip_eq(arp_ip[i], dst_ip)) {
                for (int k = 0; k < 6; k++) out_mac[k] = arp_mac[i][k];
                return 1;
            }
        }
    }
    return 0;
}

/* parsea "A.B.C.D" a 4 bytes; 0.0.0.0 si el formato no es válido */
static void parse_ip(const char* s, uint8_t out[4]) {
    int part = 0, val = 0, seen = 0;
    for (int i = 0; part < 4; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); seen = 1; }
        else if (c == '.' || c == 0) {
            out[part++] = (uint8_t)(seen ? val : 0);
            val = 0; seen = 0;
            if (c == 0) break;
        } else { out[part++] = 0; break; }
    }
    while (part < 4) out[part++] = 0;
}

/* ping real por ICMP echo. Devuelve 1 si hubo respuesta (y loguea cuántos
 * ticks tardó), 0 si no hubo respuesta en ~1 segundo. */
int pyos_ping(const char* ip_str) {
    if (!net_ready) { pyos_draw("net: llama a pyos.net_init() primero\n"); return 0; }

    uint8_t dst_ip[4];
    parse_ip(ip_str, dst_ip);

    uint8_t dst_mac[6];
    if (!arp_resolve(dst_ip, dst_mac, 20)) {
        pyos_draw("ping: sin respuesta ARP (host inalcanzable)\n");
        return 0;
    }

    ping_ident = 0xBEEF;
    ping_seq++;
    ping_waiting = 1;
    ping_got_reply = 0;

    uint32_t t0 = pyos_ticks();
    uint8_t payload[4] = {'p', 'y', 'o', 's'};
    icmp_send(dst_ip, dst_mac, 8, ping_ident, ping_seq, payload, 4);

    uint32_t deadline = t0 + 100; /* ~1s a 100Hz */
    while (!ping_got_reply && pyos_ticks() < deadline) net_drain();
    ping_waiting = 0;

    if (ping_got_reply) {
        uint32_t rtt = pyos_ticks() - t0;
        pyos_draw("pong de "); draw_ip(dst_ip);
        pyos_draw(" en ~"); 
        {
            char tmp[12]; int n = 0; uint32_t v = rtt * 10; /* ticks -> ms aprox */
            if (v == 0) tmp[n++] = '0';
            while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
            char buf[12]; int j = 0;
            while (n > 0) buf[j++] = tmp[--n];
            buf[j] = 0;
            pyos_draw(buf);
        }
        pyos_draw(" ms\n");
        return 1;
    }
    pyos_draw("ping: sin respuesta\n");
    return 0;
}
