/* udp.c — UDP genérico + DNS (Fase 6, segunda vuelta).
 *
 * DNS por qué: es el protocolo real más simple que necesita UDP para ser
 * útil (una consulta, una respuesta, sin estado) — y es lo que hace falta
 * para que pyos.http_get(host, ...) pueda recibir un nombre en vez de una
 * IP. Server usado: 10.0.2.3, el proxy DNS que trae por defecto el modo
 * de red "user" de QEMU (reenvía al resolver real de la máquina host).
 */
#include <stdint.h>

extern void pyos_draw(const char* s);
extern void* pyos_alloc(uint32_t size);
extern uint32_t pyos_ticks(void);

extern void eth_send(const uint8_t dst_mac[6], uint16_t ethertype, const uint8_t* payload, int plen);
extern void net_send_ip(const uint8_t dst_ip[4], const uint8_t dst_mac[6], uint8_t proto, const uint8_t* payload, int plen);
extern uint16_t net_l4_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4], uint8_t proto, const uint8_t* l4, int l4len);
extern void net_get_my_ip(uint8_t out[4]);
extern int  arp_resolve(const uint8_t dst_ip[4], uint8_t out_mac[6], uint32_t max_ticks);
extern void net_poll_once(void);
extern int  pyos_net_ready(void);
extern void net_parse_ip(const char* s, uint8_t out[4]);

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* ------------------------------- UDP genérico ----------------------------- */

static uint16_t udp_src_port = 49152;
static uint16_t udp_wait_port = 0;
static uint8_t  udp_reply[512];
static int      udp_reply_len = -1;

/* Llamado desde net.c cuando llega un datagrama UDP dirigido a nosotros. */
void udp_on_packet(const uint8_t src_ip[4], const uint8_t* udp, int len) {
    (void)src_ip;
    if (len < 8) return;
    uint16_t dport = rd16(udp + 2);
    uint16_t ulen = rd16(udp + 4);
    if (dport != udp_wait_port) return;
    int dlen = (int)ulen - 8;
    if (dlen < 0) return;
    if (dlen > (int)sizeof(udp_reply) - 1) dlen = (int)sizeof(udp_reply) - 1;
    for (int i = 0; i < dlen; i++) udp_reply[i] = udp[8 + i];
    udp_reply[dlen] = 0;
    udp_reply_len = dlen;
}

static void udp_send_raw(const uint8_t dst_ip[4], const uint8_t dst_mac[6],
                          uint16_t src_port, uint16_t dst_port,
                          const uint8_t* data, int dlen) {
    uint8_t pkt[8 + 600];
    if (dlen > 600) dlen = 600;
    uint8_t my_ip[4];
    net_get_my_ip(my_ip);

    wr16(pkt + 0, src_port);
    wr16(pkt + 2, dst_port);
    wr16(pkt + 4, (uint16_t)(8 + dlen));
    wr16(pkt + 6, 0);
    for (int i = 0; i < dlen; i++) pkt[8 + i] = data[i];

    uint16_t cksum = net_l4_checksum(my_ip, dst_ip, 17, pkt, 8 + dlen);
    if (cksum == 0) cksum = 0xFFFF; /* 0 está reservado para "sin checksum" */
    wr16(pkt + 6, cksum);

    net_send_ip(dst_ip, dst_mac, 17, pkt, 8 + dlen);
}

/* Manda un datagrama y espera (hasta max_ticks) una respuesta al mismo
 * puerto de origen. Devuelve el largo de la respuesta o -1 si no llegó
 * nada — pensado para request/response simple tipo DNS, no para un
 * socket UDP de propósito general (no hay cola de mensajes). */
static int udp_request(const uint8_t dst_ip[4], uint16_t dst_port,
                        const uint8_t* data, int dlen, uint32_t max_ticks) {
    uint8_t dst_mac[6];
    if (!arp_resolve(dst_ip, dst_mac, 20)) return -1;

    udp_wait_port = udp_src_port;
    udp_reply_len = -1;
    udp_send_raw(dst_ip, dst_mac, udp_src_port, dst_port, data, dlen);

    uint32_t deadline = pyos_ticks() + max_ticks;
    while (udp_reply_len < 0 && pyos_ticks() < deadline) net_poll_once();
    udp_src_port++; /* próxima consulta usa otro puerto efímero */
    return udp_reply_len;
}

int pyos_udp_send(const char* ip_str, int port, const char* text) {
    if (!pyos_net_ready()) return 0;
    uint8_t dst_ip[4];
    net_parse_ip(ip_str, dst_ip);
    uint8_t dst_mac[6];
    if (!arp_resolve(dst_ip, dst_mac, 20)) return 0;
    int len = 0;
    while (text[len]) len++;
    udp_send_raw(dst_ip, dst_mac, udp_src_port, (uint16_t)port, (const uint8_t*)text, len);
    return 1;
}

const char* pyos_udp_recv(int max_ticks) {
    static char empty[1] = {0};
    if (!pyos_net_ready()) return empty;
    udp_wait_port = udp_src_port;
    udp_reply_len = -1;
    uint32_t deadline = pyos_ticks() + (uint32_t)(max_ticks < 0 ? 0 : max_ticks);
    while (udp_reply_len < 0 && pyos_ticks() < deadline) net_poll_once();
    return udp_reply_len >= 0 ? (const char*)udp_reply : empty;
}

/* --------------------------------- DNS ------------------------------------ */

#define DNS_SERVER_IP { 10, 0, 2, 3 } /* proxy DNS de -netdev user de QEMU */

static int dns_encode_name(const char* host, uint8_t* out) {
    int op = 0, i = 0;
    while (host[i]) {
        int start = i, lablen = 0;
        while (host[i] && host[i] != '.') { i++; lablen++; }
        out[op++] = (uint8_t)lablen;
        for (int k = 0; k < lablen; k++) out[op++] = (uint8_t)host[start + k];
        if (host[i] == '.') i++;
    }
    out[op++] = 0;
    return op;
}

/* Resuelve un nombre a "A.B.C.D" (IPv4) por DNS real. Devuelve "" (string
 * vacío) si no hay respuesta o el nombre no tiene registro A. */
const char* pyos_resolve(const char* host) {
    static char result[16];
    result[0] = 0;
    if (!pyos_net_ready()) return result;

    uint8_t dns_ip[4] = DNS_SERVER_IP;
    uint8_t q[300];
    wr16(q + 0, 0x1234);   /* id */
    wr16(q + 2, 0x0100);   /* flags: consulta estándar, recursión deseada */
    wr16(q + 4, 1);        /* 1 pregunta */
    wr16(q + 6, 0);
    wr16(q + 8, 0);
    wr16(q + 10, 0);
    int qn = dns_encode_name(host, q + 12);
    int off = 12 + qn;
    wr16(q + off, 1);      /* QTYPE A */
    wr16(q + off + 2, 1);  /* QCLASS IN */
    off += 4;

    int rlen = udp_request(dns_ip, 53, q, off, 100); /* ~1s de timeout */
    if (rlen < 12) return result;

    const uint8_t* r = udp_reply;
    uint16_t ancount = rd16(r + 6);
    if (ancount == 0) return result;

    /* saltar la sección de preguntas (mismo formato que armamos arriba) */
    int p = 12;
    while (p < rlen && r[p] != 0) {
        if ((r[p] & 0xC0) == 0xC0) { p += 2; break; } /* compresión (no debería aparecer acá) */
        p += r[p] + 1;
    }
    if (p < rlen && r[p] == 0) p++;
    p += 4; /* QTYPE + QCLASS */

    for (uint16_t a = 0; a < ancount && p < rlen; a++) {
        if ((r[p] & 0xC0) == 0xC0) p += 2;
        else { while (p < rlen && r[p] != 0) p += r[p] + 1; p++; }
        if (p + 10 > rlen) break;
        uint16_t rtype = rd16(r + p);
        uint16_t rdlen = rd16(r + p + 8);
        p += 10;
        if (rtype == 1 && rdlen == 4 && p + 4 <= rlen) { /* registro A */
            int n = 0;
            for (int k = 0; k < 4; k++) {
                int v = r[p + k];
                char tmp[4]; int tn = 0;
                if (v == 0) tmp[tn++] = '0';
                while (v > 0) { tmp[tn++] = (char)('0' + v % 10); v /= 10; }
                while (tn > 0) result[n++] = tmp[--tn];
                if (k < 3) result[n++] = '.';
            }
            result[n] = 0;
            return result;
        }
        p += rdlen;
    }
    return result;
}
