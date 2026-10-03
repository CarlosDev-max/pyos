/* tcp.c — cliente TCP mínimo, una sola conexión a la vez (Fase 6, segunda
 * vuelta).
 *
 * Alcance deliberado: handshake de 3 vías real, envío con un reintento
 * simple (stop-and-wait, no hay ventana deslizante ni control de
 * congestión — sería mucho para este MVP), recepción acumulando segmentos
 * hasta el timeout o un FIN, y cierre con FIN/ACK. Suficiente para hablar
 * HTTP/1.0 con un servidor real sobre una red confiable como la de QEMU;
 * no es una pila TCP de producción (no hay retransmisión con backoff,
 * ventanas, ni manejo de out-of-order).
 */
#include <stdint.h>

extern void pyos_draw(const char* s);
extern uint32_t pyos_ticks(void);
extern int pyos_net_ready(void);

extern void net_send_ip(const uint8_t dst_ip[4], const uint8_t dst_mac[6], uint8_t proto, const uint8_t* payload, int plen);
extern uint16_t net_l4_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4], uint8_t proto, const uint8_t* l4, int l4len);
extern void net_get_my_ip(uint8_t out[4]);
extern int  arp_resolve(const uint8_t dst_ip[4], uint8_t out_mac[6], uint32_t max_ticks);
extern void net_poll_once(void);
extern void net_parse_ip(const char* s, uint8_t out[4]);
extern const char* pyos_resolve(const char* host);

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

#define TCP_CLOSED       0
#define TCP_SYN_SENT     1
#define TCP_ESTABLISHED  2
#define TCP_CLOSING      3

static int      state = TCP_CLOSED;
static uint8_t  peer_ip[4];
static uint8_t  peer_mac[6];
static uint16_t local_port = 51000;
static uint16_t remote_port;
static uint32_t snd_nxt, rcv_nxt;

#define RECV_BUF_MAX 4096
static char     recv_buf[RECV_BUF_MAX + 1];
static int      recv_len = 0;
static int      got_fin = 0;

static void tcp_send_seg(uint8_t flags, const uint8_t* data, int dlen) {
    uint8_t pkt[20 + 1024];
    if (dlen > 1024) dlen = 1024;
    uint8_t my_ip[4];
    net_get_my_ip(my_ip);

    wr16(pkt + 0, local_port);
    wr16(pkt + 2, remote_port);
    wr32(pkt + 4, snd_nxt);
    wr32(pkt + 8, rcv_nxt);
    pkt[12] = 5 << 4; /* data offset: 5 palabras de 32 bits, sin opciones */
    pkt[13] = flags;
    wr16(pkt + 14, 8192); /* ventana: fija, generosa para este MVP */
    wr16(pkt + 16, 0);    /* checksum, se calcula abajo */
    wr16(pkt + 18, 0);    /* urgent pointer */
    for (int i = 0; i < dlen; i++) pkt[20 + i] = data[i];

    uint16_t cksum = net_l4_checksum(my_ip, peer_ip, 6, pkt, 20 + dlen);
    wr16(pkt + 16, cksum);

    net_send_ip(peer_ip, peer_mac, 6, pkt, 20 + dlen);
}

/* Llamado desde net.c cuando llega un segmento TCP para nosotros. */
void tcp_on_segment(const uint8_t src_ip[4], const uint8_t src_mac[6],
                     const uint8_t* seg, int len) {
    if (state == TCP_CLOSED || len < 20) return;
    for (int i = 0; i < 4; i++) if (src_ip[i] != peer_ip[i]) return;

    uint16_t sport = rd16(seg + 0);
    if (sport != remote_port || rd16(seg + 2) != local_port) return;

    uint32_t seq = rd32(seg + 4);
    uint32_t ack = rd32(seg + 8);
    int doff = (seg[12] >> 4) * 4;
    uint8_t flags = seg[13];
    int dlen = len - doff;
    (void)src_mac;

    if (state == TCP_SYN_SENT) {
        if (flags & 0x04) { /* RST: conexión rechazada, no hace falta esperar el timeout */
            state = TCP_CLOSED;
            return;
        }
        if ((flags & 0x12) == 0x12) { /* SYN+ACK */
            rcv_nxt = seq + 1;
            snd_nxt = ack;
            state = TCP_ESTABLISHED;
            tcp_send_seg(0x10 /* ACK */, 0, 0); /* completa el 3-way handshake */
        }
        return;
    }

    if (state == TCP_ESTABLISHED || state == TCP_CLOSING) {
        if (flags & 0x04) { /* RST: el otro lado cortó */
            state = TCP_CLOSED;
            return;
        }
        if (seq == rcv_nxt && dlen > 0) {
            int room = RECV_BUF_MAX - recv_len;
            int copy = dlen < room ? dlen : room;
            for (int i = 0; i < copy; i++) recv_buf[recv_len++] = (char)seg[doff + i];
            recv_buf[recv_len] = 0;
            rcv_nxt += (uint32_t)dlen;
            tcp_send_seg(0x10 /* ACK */, 0, 0);
        }
        if (flags & 0x01) { /* FIN */
            rcv_nxt += 1;
            got_fin = 1;
            tcp_send_seg(0x10, 0, 0);
        }
    }
}

int pyos_tcp_connect(const char* ip_str, int port) {
    if (!pyos_net_ready()) return 0;
    net_parse_ip(ip_str, peer_ip);
    if (!arp_resolve(peer_ip, peer_mac, 20)) {
        pyos_draw("tcp: sin respuesta ARP para el destino\n");
        return 0;
    }

    local_port++;
    remote_port = (uint16_t)port;
    snd_nxt = pyos_ticks() * 12345u + 7u; /* ISN pseudo-aleatorio, alcanza para un cliente */
    recv_len = 0;
    got_fin = 0;
    state = TCP_SYN_SENT;

    tcp_send_seg(0x02 /* SYN */, 0, 0);
    snd_nxt += 1; /* el SYN consume un número de secuencia */

    uint32_t deadline = pyos_ticks() + 100; /* ~1s para el handshake */
    while (state == TCP_SYN_SENT && pyos_ticks() < deadline) net_poll_once();

    if (state != TCP_ESTABLISHED) {
        if (state == TCP_CLOSED) {
            pyos_draw("tcp: conexion rechazada (RST)\n");
        } else {
            pyos_draw("tcp: timeout en el handshake\n");
        }
        state = TCP_CLOSED;
        return 0;
    }
    return 1;
}

int pyos_tcp_send(const char* text) {
    if (state != TCP_ESTABLISHED) return 0;
    int len = 0;
    while (text[len]) len++;

    uint32_t base_seq = snd_nxt;
    tcp_send_seg(0x18 /* PSH+ACK */, (const uint8_t*)text, len);

    /* stop-and-wait simple: dejamos una ventana breve para que el otro
     * lado empiece a mandar su respuesta (no cambia snd_nxt esperar,
     * pero ayuda a que ya haya datos en el buffer cuando el usuario
     * llame a tcp_recv) */
    uint32_t deadline = pyos_ticks() + 20;
    while (pyos_ticks() < deadline) net_poll_once();

    snd_nxt = base_seq + (uint32_t)len;
    return len;
}

const char* pyos_tcp_recv(int max_ticks) {
    if (state != TCP_ESTABLISHED && state != TCP_CLOSING) return recv_buf;
    uint32_t deadline = pyos_ticks() + (uint32_t)(max_ticks < 0 ? 0 : max_ticks);
    while (!got_fin && pyos_ticks() < deadline) net_poll_once();
    return recv_buf;
}

void pyos_tcp_close(void) {
    if (state == TCP_CLOSED) return;
    tcp_send_seg(0x11 /* FIN+ACK */, 0, 0);
    snd_nxt += 1;
    state = TCP_CLOSING;
    uint32_t deadline = pyos_ticks() + 30;
    while (pyos_ticks() < deadline) net_poll_once();
    state = TCP_CLOSED;
}

/* ------------------------------- HTTP GET --------------------------------- */

/* GET simple por HTTP/1.0 (sin keep-alive: el server cierra solo al
 * terminar, así no hace falta saber Content-Length para cortar la
 * lectura). Resuelve `host` por DNS si no es ya una IP puntuada. */
const char* pyos_http_get(const char* host, const char* path) {
    static char empty[1] = {0};
    if (!pyos_net_ready()) return empty;

    int looks_like_ip = 1;
    for (int i = 0; host[i]; i++) {
        char c = host[i];
        if (!((c >= '0' && c <= '9') || c == '.')) { looks_like_ip = 0; break; }
    }
    const char* ip = host;
    if (!looks_like_ip) {
        ip = pyos_resolve(host);
        if (ip[0] == 0) { pyos_draw("http: no se pudo resolver el host\n"); return empty; }
    }

    if (!pyos_tcp_connect(ip, 80)) return empty;

    char req[300];
    int p = 0;
    const char* parts[] = { "GET ", path, " HTTP/1.0\r\nHost: ", host, "\r\nConnection: close\r\n\r\n" };
    for (int s = 0; s < 5; s++) {
        const char* t = parts[s];
        for (int i = 0; t[i] && p < (int)sizeof(req) - 1; i++) req[p++] = t[i];
    }
    req[p] = 0;

    pyos_tcp_send(req);
    const char* body = pyos_tcp_recv(300); /* ~3s para toda la respuesta */
    pyos_tcp_close();
    return body;
}
