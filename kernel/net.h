/* Polling RTL8139, DHCP, DNS, and one HTTP client. Catalog fetch does not use this. */
static inline void outl(u16 p, u32 v) {
  __asm__ volatile("outl %0,%1" ::"a"(v), "Nd"(p));
}
static inline u32 inl(u16 p) {
  u32 v;
  __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p));
  return v;
}
static u32 nic_pci_read(u8 bus, u8 dev, u8 fn, u8 off) {
  outl(0xcf8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                  ((u32)fn << 8) | (off & 0xfc));
  return inl(0xcfc);
}
static void nic_pci_write(u8 bus, u8 dev, u8 fn, u8 off, u32 val) {
  outl(0xcf8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                  ((u32)fn << 8) | (off & 0xfc));
  outl(0xcfc, val);
}
static u16 nic_io;
static u8 nic_mac[6];
static u8 nic_tx_i;
static u8 nic_tx[4][1536] __attribute__((aligned(16)));
static u8 nic_rx[8192 + 16] __attribute__((aligned(16)));
static u32 nic_rx_off;
static u32 net_ip, net_gw, net_dns_ip, net_mask;
static int net_ready;
static u16 ip_seq = 1;

static void be16p(u8 *p, u16 v) {
  p[0] = (u8)(v >> 8);
  p[1] = (u8)v;
}
static void be32p(u8 *p, u32 v) {
  p[0] = (u8)(v >> 24);
  p[1] = (u8)(v >> 16);
  p[2] = (u8)(v >> 8);
  p[3] = (u8)v;
}
static u16 rd16b(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static u32 rd32b(const u8 *p) {
  return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static u16 fold_sum(u32 sum) {
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (u16)~sum;
}
static u32 words_sum(const u8 *p, u32 n, u32 sum) {
  while (n > 1) {
    sum += ((u32)p[0] << 8) | p[1];
    p += 2;
    n -= 2;
  }
  if (n)
    sum += (u32)p[0] << 8;
  return sum;
}
static void say_ip(u32 ip) {
  number(ip >> 24);
  put('.');
  number((ip >> 16) & 255);
  put('.');
  number((ip >> 8) & 255);
  put('.');
  number(ip & 255);
}
static int nic_transmit(const u8 *frame, u32 len) {
  if (!nic_io || !len || len > 1536)
    return 0;
  u8 slot = nic_tx_i;
  memset(nic_tx[slot], 0, len < 60 ? 60 : len);
  memcpy(nic_tx[slot], frame, len);
  if (len < 60)
    len = 60;
  outl(nic_io + 0x20 + slot * 4, (u32)(u64)nic_tx[slot]);
  outl(nic_io + 0x10 + slot * 4, len & 0x1fff);
  for (u32 i = 0; i < 200000; i++) {
    u32 st = inl(nic_io + 0x10 + slot * 4);
    if (st & (1u << 15)) {
      nic_tx_i = (u8)((slot + 1) & 3);
      return 1;
    }
  }
  nic_tx_i = (u8)((slot + 1) & 3);
  return 0;
}
static u8 rx_at(u32 i) { return ((volatile u8 *)nic_rx)[i & 8191]; }
static int nic_recv(u8 *dst, u32 cap, u32 *out) {
  if (!nic_io || (inb(nic_io + 0x37) & 1))
    return 0;
  u32 off = nic_rx_off;
  u16 status = (u16)(rx_at(off) | (rx_at(off + 1) << 8));
  u16 length = (u16)(rx_at(off + 2) | (rx_at(off + 3) << 8));
  if (length < 8 || length > 2048) {
    outb(nic_io + 0x37, 0x10);
    for (u32 i = 0; i < 100000 && (inb(nic_io + 0x37) & 0x10); i++)
      ;
    nic_rx_off = 0;
    outw(nic_io + 0x38, 0xfff0);
    outb(nic_io + 0x37, 0x0c);
    return 0;
  }
  u32 payload = (u32)length - 4;
  if (payload > cap)
    payload = cap;
  if (status & 1)
    for (u32 i = 0; i < payload; i++)
      dst[i] = rx_at(off + 4 + i);
  u32 next = (off + length + 4 + 3) & ~3u;
  while (next >= 8192)
    next -= 8192;
  nic_rx_off = next;
  outw(nic_io + 0x38, (u16)(next - 16));
  outw(nic_io + 0x3e, 0xffff);
  if (!(status & 1))
    return 0;
  *out = payload;
  return 1;
}
static int nic_open(void) {
  if (nic_io)
    return 1;
  for (u8 dev = 0; dev < 32; dev++) {
    u32 id = nic_pci_read(0, dev, 0, 0);
    if ((id & 0xffff) != 0x10ec || (id >> 16) != 0x8139)
      continue;
    u32 cmd = nic_pci_read(0, dev, 0, 4);
    nic_pci_write(0, dev, 0, 4, (cmd & 0xffff0000u) | ((cmd | 0x5u) & 0xffffu));
    u32 bar = nic_pci_read(0, dev, 0, 0x10);
    if (!(bar & 1))
      return 0;
    nic_io = (u16)(bar & ~3u);
    break;
  }
  if (!nic_io)
    return 0;
  outb(nic_io + 0x50, 0xc0);
  outb(nic_io + 0x52, 0x00);
  outb(nic_io + 0x50, 0x00);
  outb(nic_io + 0x37, 0x10);
  for (u32 i = 0; i < 1000000 && (inb(nic_io + 0x37) & 0x10); i++)
    ;
  for (u32 i = 0; i < 6; i++)
    nic_mac[i] = inb(nic_io + (u16)i);
  memset(nic_rx, 0, sizeof nic_rx);
  nic_rx_off = 0;
  outl(nic_io + 0x30, (u32)(u64)nic_rx);
  outl(nic_io + 0x08, 0xffffffffu);
  outl(nic_io + 0x0c, 0xffffffffu);
  outl(nic_io + 0x44, 0x8e);
  outw(nic_io + 0x3c, 0);
  outw(nic_io + 0x3e, 0xffff);
  outw(nic_io + 0x38, 0xfff0);
  outb(nic_io + 0x37, 0x0c);
  return 1;
}
static int net_present(void) {
  if (nic_io)
    return 1;
  for (u8 dev = 0; dev < 32; dev++) {
    u32 id = nic_pci_read(0, dev, 0, 0);
    if ((id & 0xffff) == 0x10ec && (id >> 16) == 0x8139)
      return 1;
  }
  return 0;
}

static u8 pkt[1600];
static u32 arp_want, arp_found;
static u8 arp_mac[6];
static u32 dhcp_xid, dhcp_offer, dhcp_server, dhcp_msg;
static int dhcp_seen;
static u16 dns_id;
static u32 dns_ip;
static int dns_seen;
static u32 tcp_iss, tcp_seq, tcp_ack, tcp_peer;
static u16 tcp_sport, tcp_dport;
static u8 tcp_peer_mac[6];
static int tcp_state, tcp_fin;
static u32 tcp_total;
static u8 tcp_preview[2048];
static u32 tcp_preview_n;

static void eth_send(const u8 dst[6], u16 type, const u8 *payload, u32 len) {
  memset(pkt, 0, 60);
  memcpy(pkt, dst, 6);
  memcpy(pkt + 6, nic_mac, 6);
  be16p(pkt + 12, type);
  if (len && payload)
    memcpy(pkt + 14, payload, len);
  u32 n = 14 + len;
  nic_transmit(pkt, n < 60 ? 60 : n);
}
static void ip_send(u32 dst, const u8 dstmac[6], u8 proto, const u8 *data,
                    u32 len) {
  u8 frame[1514];
  if (len + 34 > sizeof frame)
    return;
  memset(frame, 0, 34);
  memcpy(frame, dstmac, 6);
  memcpy(frame + 6, nic_mac, 6);
  be16p(frame + 12, 0x0800);
  frame[14] = 0x45;
  be16p(frame + 16, (u16)(20 + len));
  be16p(frame + 18, ip_seq++);
  be16p(frame + 20, 0x4000);
  frame[22] = 64;
  frame[23] = proto;
  be32p(frame + 26, net_ip);
  be32p(frame + 30, dst);
  be16p(frame + 24, fold_sum(words_sum(frame + 14, 20, 0)));
  if (len)
    memcpy(frame + 34, data, len);
  nic_transmit(frame, 34 + len);
}
static void udp_send(u32 dst, const u8 dstmac[6], u16 sport, u16 dport,
                     const u8 *data, u32 len) {
  u8 body[1280];
  if (len + 8 > sizeof body)
    return;
  be16p(body, sport);
  be16p(body + 2, dport);
  be16p(body + 4, (u16)(8 + len));
  be16p(body + 6, 0);
  if (len)
    memcpy(body + 8, data, len);
  ip_send(dst, dstmac, 17, body, 8 + len);
}
static void arp_send(u16 oper, const u8 tha[6], u32 tpa, const u8 dst[6]) {
  u8 a[28];
  memset(a, 0, sizeof a);
  be16p(a, 1);
  be16p(a + 2, 0x0800);
  a[4] = 6;
  a[5] = 4;
  be16p(a + 6, oper);
  memcpy(a + 8, nic_mac, 6);
  be32p(a + 14, net_ip);
  memcpy(a + 18, tha, 6);
  be32p(a + 24, tpa);
  eth_send(dst, 0x0806, a, 28);
}
static void net_on_ip(const u8 *f, u32 n);
static void net_on_frame(const u8 *f, u32 n) {
  if (n < 14)
    return;
  u16 type = rd16b(f + 12);
  if (type == 0x0806 && n >= 42) {
    u16 oper = rd16b(f + 20);
    u32 spa = rd32b(f + 28);
    u32 tpa = rd32b(f + 38);
    if (oper == 1 && net_ip && tpa == net_ip) {
      u8 reply_dst[6];
      memcpy(reply_dst, f + 22, 6);
      u8 tha[6];
      memcpy(tha, f + 22, 6);
      arp_send(2, tha, spa, reply_dst);
    } else if (oper == 2 && spa == arp_want) {
      memcpy(arp_mac, f + 22, 6);
      arp_found = 1;
    }
    return;
  }
  if (type == 0x0800)
    net_on_ip(f + 14, n - 14);
}
static void net_on_ip(const u8 *ip, u32 n) {
  if (n < 20 || (ip[0] >> 4) != 4)
    return;
  u32 ihl = (ip[0] & 0x0f) * 4;
  if (ihl < 20 || n < ihl)
    return;
  u32 src = rd32b(ip + 12);
  u32 dst = rd32b(ip + 16);
  u8 proto = ip[9];
  if (proto == 17 && n >= ihl + 8) {
    const u8 *u = ip + ihl;
    u16 dport = rd16b(u + 2);
    u16 ulen = rd16b(u + 4);
    if (ulen < 8 || ihl + ulen > n)
      return;
    const u8 *data = u + 8;
    u32 dlen = ulen - 8;
    if (dport == 68 && dlen >= 240 && rd32b(data + 4) == dhcp_xid &&
        rd32b(data + 236) == 0x63825363) {
      dhcp_offer = rd32b(data + 16);
      dhcp_msg = 0;
      dhcp_server = 0;
      for (u32 i = 240; i + 1 < dlen;) {
        u8 code = data[i++];
        if (code == 255)
          break;
        if (code == 0)
          continue;
        u8 ln = data[i++];
        if (i + ln > dlen)
          break;
        if (code == 53 && ln == 1)
          dhcp_msg = data[i];
        else if (code == 1 && ln == 4)
          net_mask = rd32b(data + i);
        else if (code == 3 && ln >= 4)
          net_gw = rd32b(data + i);
        else if (code == 6 && ln >= 4)
          net_dns_ip = rd32b(data + i);
        else if (code == 54 && ln == 4)
          dhcp_server = rd32b(data + i);
        i += ln;
      }
      if (dst == 0xffffffffu || dst == dhcp_offer || !net_ip)
        dhcp_seen = 1;
    }
    (void)src;
    return;
  }
}
static int net_poll(u32 spins) {
  u8 buf[1600];
  int any = 0;
  for (u32 i = 0; i < spins; i++) {
    u32 n = 0;
    if (!nic_recv(buf, sizeof buf, &n))
      continue;
    any = 1;
    net_on_frame(buf, n);
  }
  return any;
}
static u32 dhcp_build(u8 *p, u8 msg, u32 req, u32 sid) {
  memset(p, 0, 260);
  p[0] = 1;
  p[1] = 1;
  p[2] = 6;
  be32p(p + 4, dhcp_xid);
  be16p(p + 10, 0x8000);
  memcpy(p + 28, nic_mac, 6);
  be32p(p + 236, 0x63825363);
  u32 i = 240;
  p[i++] = 53;
  p[i++] = 1;
  p[i++] = msg;
  if (req) {
    p[i++] = 50;
    p[i++] = 4;
    be32p(p + i, req);
    i += 4;
  }
  if (sid) {
    p[i++] = 54;
    p[i++] = 4;
    be32p(p + i, sid);
    i += 4;
  }
  p[i++] = 55;
  p[i++] = 4;
  p[i++] = 1;
  p[i++] = 3;
  p[i++] = 6;
  p[i++] = 51;
  p[i++] = 255;
  return i;
}
static int net_dhcp(void) {
  if (net_ready)
    return 1;
  u8 bcast[6] = {255, 255, 255, 255, 255, 255};
  dhcp_xid = 0x4e495801u;
  for (int attempt = 0; attempt < 4 && !net_ready; attempt++) {
    u8 boot[320];
    u32 n = dhcp_build(boot, 1, 0, 0);
    dhcp_seen = 0;
    udp_send(0xffffffffu, bcast, 68, 67, boot, n);
    for (u32 s = 0; s < 30 && !dhcp_seen; s++)
      net_poll(20000);
    if (!dhcp_seen || dhcp_msg != 2 || !dhcp_offer)
      continue;
    u32 offer = dhcp_offer;
    u32 sid = dhcp_server;
    n = dhcp_build(boot, 3, offer, sid);
    dhcp_seen = 0;
    udp_send(0xffffffffu, bcast, 68, 67, boot, n);
    for (u32 s = 0; s < 30 && !dhcp_seen; s++)
      net_poll(20000);
    if (dhcp_seen && dhcp_msg == 5 && dhcp_offer) {
      net_ip = dhcp_offer;
      if (!net_mask)
        net_mask = 0xffffff00u;
      if (!net_gw)
        net_gw = (net_ip & net_mask) | 1u;
      if (!net_dns_ip)
        net_dns_ip = net_gw;
      net_ready = 1;
    }
  }
  return net_ready;
}
static void net_show(void) {
  if (!nic_open()) {
    line("No network device is attached.");
    return;
  }
  say("RTL8139 ");
  for (u32 i = 0; i < 6; i++) {
    const char *hex = "0123456789abcdef";
    put(hex[nic_mac[i] >> 4]);
    put(hex[nic_mac[i] & 15]);
    if (i != 5)
      put(':');
  }
  line("");
  if (!net_dhcp()) {
    line("DHCP did not answer.");
    return;
  }
  say("Address ");
  say_ip(net_ip);
  line("");
  say("Gateway ");
  say_ip(net_gw);
  line("");
  say("DNS ");
  say_ip(net_dns_ip);
  line("");
}
