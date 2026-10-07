/* dhcp.c — cliente DHCP real (RFC 2131), tercera vuelta de la Fase 6.
 *
 * Hasta ahora pyos arrancaba siempre con la IP estática 10.0.2.15 (la que
 * da por defecto el modo "user" de QEMU). Esto le pide una IP de verdad
 * al servidor DHCP de la red -- el mismo SLIRP de QEMU corre uno, así que
 * funciona tal cual sin configurar nada extra. En una red real (un router
 * casero, por ejemplo) debería funcionar igual.
 *
 * Handshake completo: DISCOVER (broadcast, "¿hay algún servidor DHCP
 * ahí?") -> OFFER (el servidor propone una IP) -> REQUEST (la aceptamos,
 * todavía por broadcast) -> ACK (confirmado, ya es nuestra). No hay
 * manejo de "lease time" / renovación todavía -- alcanza para que el
 * kernel arranque con una IP real y listo.
 */
#include <stdint.h>

extern void eth_send(const uint8_t dst_mac[6], uint16_t ethertype, const uint8_t* payload, int plen);
extern void net_send_ip(const uint8_t dst_ip[4], const uint8_t dst_mac[6], uint8_t proto, const uint8_t* payload, int plen);
extern uint16_t net_l4_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4], uint8_t proto, const uint8_t* l4, int l4len);
extern void net_get_my_mac(uint8_t out[6]);
extern void net_get_my_ip(uint8_t out[4]);
extern void net_set_ip_config(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4]);
extern void net_poll_once(void);
extern uint32_t pyos_ticks(void);
extern void pyos_draw(const char* s);
extern int pyos_net_ready(void);

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

#define DHCP_PKT_SIZE 300 /* 236 (BOOTP fijo) + 4 (cookie) + opciones, de sobra */

static uint32_t dhcp_xid = 0x1A2B3C4D;
static uint8_t  offered_ip[4];
static uint8_t  server_ip[4];
static uint8_t  offered_mask[4] = {255, 255, 255, 0};
static uint8_t  offered_gw[4] = {0, 0, 0, 0};
static int      got_offer = 0;
static int      got_ack = 0;
static int      got_nak = 0;

/* busca una opción DHCP por código dentro del área de opciones (después
 * del cookie mágico). Devuelve 1 y deja puntero+largo si la encuentra. */
static int dhcp_find_option(const uint8_t* opts, int len, uint8_t code,
                             const uint8_t** out, int* outlen) {
    int i = 0;
    while (i + 1 < len) {
        uint8_t c = opts[i];
        if (c == 255) break;      /* end */
        if (c == 0) { i++; continue; } /* pad */
        uint8_t l = opts[i + 1];
        if (i + 2 + l > len) break;
        if (c == code) { *out = &opts[i + 2]; *outlen = l; return 1; }
        i += 2 + l;
    }
    return 0;
}

/* Llamado desde net.c para cada datagrama UDP dirigido al puerto 68
 * (el puerto que usa un cliente DHCP). */
void dhcp_on_packet(const uint8_t* udp, int len) {
    if (len < 8 + 236 + 4) return; /* header UDP + BOOTP fijo + cookie, mínimo */
    const uint8_t* b = udp + 8; /* el paquete BOOTP/DHCP empieza después del header UDP */

    if (b[0] != 2) return; /* op debe ser BOOTREPLY */
    if (rd32(b + 4) != dhcp_xid) return; /* no es la transacción que esperamos */

    int blen = len - 8;
    const uint8_t* opts = b + 236 + 4; /* después de cookie mágico */
    int optlen = blen - 236 - 4;

    const uint8_t* p; int pl;
    if (!dhcp_find_option(opts, optlen, 53, &p, &pl) || pl < 1) return;
    uint8_t msgtype = p[0];

    for (int i = 0; i < 4; i++) offered_ip[i] = b[16 + i]; /* yiaddr */

    if (dhcp_find_option(opts, optlen, 54, &p, &pl) && pl == 4) {
        for (int i = 0; i < 4; i++) server_ip[i] = p[i];
    } else {
        for (int i = 0; i < 4; i++) server_ip[i] = b[20 + i]; /* siaddr, backup */
    }
    if (dhcp_find_option(opts, optlen, 1, &p, &pl) && pl == 4) {
        for (int i = 0; i < 4; i++) offered_mask[i] = p[i];
    }
    if (dhcp_find_option(opts, optlen, 3, &p, &pl) && pl >= 4) {
        for (int i = 0; i < 4; i++) offered_gw[i] = p[i];
    }

    if (msgtype == 2) got_offer = 1;       /* DHCPOFFER */
    else if (msgtype == 5) got_ack = 1;    /* DHCPACK */
    else if (msgtype == 6) got_nak = 1;    /* DHCPNAK */
}

/* Arma el header BOOTP fijo (236 bytes) + cookie mágico. Común a DISCOVER
 * y REQUEST -- lo único que cambia entre los dos son las opciones. */
static int dhcp_build_header(uint8_t* pkt, const uint8_t ciaddr[4]) {
    uint8_t my_mac[6];
    net_get_my_mac(my_mac);

    for (int i = 0; i < 236; i++) pkt[i] = 0;
    pkt[0] = 1;  /* op: BOOTREQUEST */
    pkt[1] = 1;  /* htype: Ethernet */
    pkt[2] = 6;  /* hlen */
    pkt[3] = 0;  /* hops */
    wr32(pkt + 4, dhcp_xid);
    wr16(pkt + 8, 0);       /* secs */
    wr16(pkt + 10, 0x8000); /* flags: bit de broadcast -- que conteste a
                                255.255.255.255, no a una unicast que
                                todavía no sabemos aceptar */
    for (int i = 0; i < 4; i++) pkt[12 + i] = ciaddr[i]; /* ciaddr */
    for (int i = 0; i < 6; i++) pkt[28 + i] = my_mac[i]; /* chaddr */

    pkt[236] = 0x63; pkt[237] = 0x82; pkt[238] = 0x53; pkt[239] = 0x63; /* cookie */
    return 240;
}

static void dhcp_send(const uint8_t* bootp, int bootplen) {
    uint8_t udp_pkt[8 + DHCP_PKT_SIZE];
    uint8_t zero_ip[4] = {0, 0, 0, 0};
    uint8_t bcast_ip[4] = {255, 255, 255, 255};
    uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    wr16(udp_pkt + 0, 68);  /* src port: cliente DHCP */
    wr16(udp_pkt + 2, 67);  /* dst port: servidor DHCP */
    wr16(udp_pkt + 4, (uint16_t)(8 + bootplen));
    wr16(udp_pkt + 6, 0);
    for (int i = 0; i < bootplen; i++) udp_pkt[8 + i] = bootp[i];

    uint16_t cksum = net_l4_checksum(zero_ip, bcast_ip, 17, udp_pkt, 8 + bootplen);
    if (cksum == 0) cksum = 0xFFFF;
    wr16(udp_pkt + 6, cksum);

    net_send_ip(bcast_ip, bcast_mac, 17, udp_pkt, 8 + bootplen);
}

/* Negocia una IP real por DHCP. Devuelve 1 si consiguió una IP de verdad
 * (y ya quedó aplicada con net_set_ip_config), 0 si no hubo respuesta o
 * el servidor dijo NAK -- en ese caso la config de red queda como estaba
 * antes de llamar a esta función. */
int pyos_dhcp_configure(int timeout_ticks) {
    if (!pyos_net_ready()) return 0;

    uint8_t zero_ip[4] = {0, 0, 0, 0};
    uint8_t old_ip[4];
    net_get_my_ip(old_ip);
    net_set_ip_config(zero_ip, 0, 0); /* todavía no tenemos IP: 0.0.0.0 */

    dhcp_xid++;
    got_offer = got_ack = got_nak = 0;

    /* --- DISCOVER --- */
    uint8_t pkt[DHCP_PKT_SIZE];
    int n = dhcp_build_header(pkt, zero_ip);
    pkt[n++] = 53; pkt[n++] = 1; pkt[n++] = 1;             /* msg type: DISCOVER */
    pkt[n++] = 55; pkt[n++] = 3; pkt[n++] = 1; pkt[n++] = 3; pkt[n++] = 6; /* pedimos mask/gw/dns */
    pkt[n++] = 255;                                         /* end */
    dhcp_send(pkt, n);

    uint32_t deadline = pyos_ticks() + (uint32_t)(timeout_ticks < 0 ? 0 : timeout_ticks);
    while (!got_offer && pyos_ticks() < deadline) net_poll_once();

    if (!got_offer) {
        net_set_ip_config(old_ip, 0, 0); /* nadie contestó: volvemos a lo que había */
        pyos_draw("dhcp: sin respuesta (DISCOVER)\n");
        return 0;
    }

    /* --- REQUEST --- */
    n = dhcp_build_header(pkt, zero_ip);
    pkt[n++] = 53; pkt[n++] = 1; pkt[n++] = 3;             /* msg type: REQUEST */
    pkt[n++] = 50; pkt[n++] = 4;                            /* requested IP */
    for (int i = 0; i < 4; i++) pkt[n++] = offered_ip[i];
    pkt[n++] = 54; pkt[n++] = 4;                            /* server identifier */
    for (int i = 0; i < 4; i++) pkt[n++] = server_ip[i];
    pkt[n++] = 255;
    dhcp_send(pkt, n);

    got_ack = got_nak = 0;
    deadline = pyos_ticks() + (uint32_t)(timeout_ticks < 0 ? 0 : timeout_ticks);
    while (!got_ack && !got_nak && pyos_ticks() < deadline) net_poll_once();

    if (got_nak || !got_ack) {
        net_set_ip_config(old_ip, 0, 0);
        pyos_draw(got_nak ? "dhcp: el servidor dijo NAK\n" : "dhcp: sin respuesta (REQUEST)\n");
        return 0;
    }

    net_set_ip_config(offered_ip, offered_mask, offered_gw);
    return 1;
}
