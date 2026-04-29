#include <string.h>
#define rte_memcpy memcpy

#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <math.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_malloc.h>

/* ── Portable helpers ───────────────────────────────────────────────────── */
static inline uint64_t bench_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline void     bench_pause(void)        { __asm__ __volatile__("pause"); }
static inline uint64_t bench_ntohll(uint64_t x) { return __builtin_bswap64(x); }
static inline uint64_t bench_htonll(uint64_t x) { return __builtin_bswap64(x); }

/* ── Network config ─────────────────────────────────────────────────────── */
#define SRC_MAC    { 0x10, 0x70, 0xfd, 0x65, 0x80, 0x10 }
#define DST_MAC    { 0xb8, 0xce, 0xf6, 0x99, 0xfe, 0x06 }
#define SRC_IP     RTE_IPV4(192, 168, 10, 131)
#define DST_IP     RTE_IPV4(192, 168, 10, 121)
#define DST_PORT   1152
#define BASE_SPORT 49152

/* ── Protocol ───────────────────────────────────────────────────────────── */
#define OPTYPE_GETREQ  0x0030
#define OPTYPE_GETRES  0x0009

/* ── DPDK config ────────────────────────────────────────────────────────── */
#define PORT_ID        0
#define NUM_RX_DESC    4096
#define NUM_TX_DESC    4096
#define MBUF_POOL_SIZE (1<<18)
#define MBUF_CACHE     512
#define BURST_SIZE     32
#define MAX_PKTS       (1 << 25)   /* 32M */
#define MAX_TX_LCORES  32
#define MAX_RX_LCORES  32

/* ── Benchmark config ───────────────────────────────────────────────────── */
static uint64_t cfg_rate_pps  = 10000;
static uint64_t cfg_count     = 10000;
static uint8_t  cfg_recir     = 39;
static uint8_t  cfg_depth     = 10;
static int      cfg_tx_lcores = 1;
static int      cfg_rx_lcores = 1;

/* ── Shared state ───────────────────────────────────────────────────────── */
static volatile int      force_stop   = 0;
static atomic_long       tx_sent;
static atomic_long       rx_recvd;
static atomic_long       lat_idx;
static atomic_long       tx_done_cnt;
static volatile uint64_t first_tx_tsc = 0;
static atomic_ulong last_tx_tsc_atomic;

static uint64_t         *send_tsc;
static uint64_t         *latencies;
static struct rte_mempool *mbuf_pool;

/* ── TX lcore tracking ──────────────────────────────────────────────────── */
static unsigned tx_lcore_ids[MAX_TX_LCORES];
static unsigned rx_lcore_ids[MAX_RX_LCORES];

typedef struct { int worker_id; } tx_arg_t;
typedef struct { int queue_id;  } rx_arg_t;
static tx_arg_t tx_args[MAX_TX_LCORES];
static rx_arg_t rx_args[MAX_RX_LCORES];

/* ── Payload template ───────────────────────────────────────────────────── */
#define MAX_PAYLOAD 1024
static uint8_t  payload_template[MAX_PAYLOAD];
static uint16_t payload_len;
static uint16_t tsc_offset;
static uint16_t seq_offset;

/* ── MD5 ────────────────────────────────────────────────────────────────── */
#include "md5.h"

static const char *TARGET_PATH =
    "/#test-dir.0.d.8.n.31981446.b.4"
    "/mdtest_tree.0/mdtest_tree.1";

// static const char *TARGET_PATH =
//     "/#test-dir.0.d.8.n.31981446.b.4"
//     "/mdtest_tree.0/mdtest_tree.1/mdtest_tree.5"
//     "/mdtest_tree.21/mdtest_tree.85/mdtest_tree.341/mdtest_tree.1365/mdtest_tree.5461";

static void md5_8(const char *s, uint8_t out[8]) {
    uint8_t full[16];
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, (const uint8_t *)s, strlen(s));
    MD5Final(full, &ctx);
    memcpy(out, full, 8);
}

static void build_payload(void) {
    const char *path = TARGET_PATH;
    int path_len = strlen(path);
    uint8_t depth = cfg_depth;
    uint8_t recir = cfg_recir;

    char segs[16][256]; int nseg = 0;
    char tmp[1024];
    strncpy(tmp, path + 1, sizeof(tmp));
    char *tok = strtok(tmp, "/");
    while (tok && nseg < 16) { strncpy(segs[nseg++], tok, 255); tok = strtok(NULL, "/"); }

    uint8_t hashes[16][8]; int nhash = 0;
    md5_8("/", hashes[nhash++]);
    char prefix[1024] = "";
    for (int i = 0; i < nseg; i++) {
        char seg_path[1024];
        snprintf(seg_path, sizeof(seg_path), "%s/%s", prefix[0] ? prefix : "", segs[i]);
        if (seg_path[0] != '/') {
            char tmp2[1024];
            snprintf(tmp2, sizeof(tmp2), "/%s", seg_path);
            strncpy(seg_path, tmp2, sizeof(seg_path));
        }
        md5_8(seg_path, hashes[nhash++]);
        snprintf(prefix, sizeof(prefix), "%s", seg_path);
    }

    int start = nhash - depth; if (start < 0) start = 0;
    uint8_t *p = payload_template;
    *(uint16_t *)p = htons(OPTYPE_GETREQ); p += 2;
    *p++ = recir; *p++ = depth;
    for (int i = 0; i < depth; i++) memcpy(p + i*8, hashes[start+i], 8);
    p += depth * 8;
    memset(p, 0x01, depth); p += depth;
    *(uint16_t *)p = htons(path_len + 12); p += 2;
    memcpy(p, path, path_len); p += path_len;
    tsc_offset = p - payload_template; memset(p, 0, 8); p += 8;
    seq_offset = p - payload_template; memset(p, 0, 4); p += 4;
    payload_len = p - payload_template;
    printf("Payload: %u bytes  tsc_offset=%u  seq_offset=%u\n",
           payload_len, tsc_offset, seq_offset);
}

/* ── Build packet ───────────────────────────────────────────────────────── */
static inline struct rte_mbuf *
build_pkt(uint32_t seq, uint64_t send_ts_ns, uint16_t sport)
{
    struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
    if (unlikely(!m)) return NULL;

    uint16_t pkt_len = sizeof(struct rte_ether_hdr)
                     + sizeof(struct rte_ipv4_hdr)
                     + sizeof(struct rte_udp_hdr)
                     + payload_len;
    m->data_len = pkt_len; m->pkt_len = pkt_len;

    uint8_t *buf = rte_pktmbuf_mtod(m, uint8_t *);

    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)buf;
    static const uint8_t src_mac[] = SRC_MAC;
    static const uint8_t dst_mac[] = DST_MAC;
    memcpy(eth->s_addr.addr_bytes, src_mac, 6);
    memcpy(eth->d_addr.addr_bytes, dst_mac, 6);
    eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);

    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    memset(ip, 0, sizeof(*ip));
    ip->version_ihl   = 0x45;
    ip->total_length  = htons(sizeof(*ip) + sizeof(struct rte_udp_hdr) + payload_len);
    ip->time_to_live  = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr      = htonl(SRC_IP);
    ip->dst_addr      = htonl(DST_IP);
    ip->hdr_checksum  = rte_ipv4_cksum(ip);

    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip + 1);
    udp->src_port    = htons(sport);
    udp->dst_port    = htons(DST_PORT);
    udp->dgram_len   = htons(sizeof(*udp) + payload_len);
    udp->dgram_cksum = 0;

    uint8_t *pay = (uint8_t *)(udp + 1);
    memcpy(pay, payload_template, payload_len);
    *(uint64_t *)(pay + tsc_offset) = bench_htonll(send_ts_ns);
    *(uint32_t *)(pay + seq_offset) = htonl(seq);

    return m;
}

/* ── TX thread ──────────────────────────────────────────────────────────── */
static int tx_thread(void *arg)
{
    int wid = arg ? ((tx_arg_t *)arg)->worker_id : 0;
    uint16_t txq = (uint16_t)wid;

    printf("TX worker %d on lcore %u  queue %u\n", wid, rte_lcore_id(), txq);

    uint64_t hz       = rte_get_tsc_hz();
    uint64_t total    = cfg_count / (uint64_t)cfg_tx_lcores;
    uint64_t rate     = cfg_rate_pps / (uint64_t)cfg_tx_lcores;
    uint64_t interval = (rate > 0) ? hz / rate : 1;
    uint64_t sent     = 0;

    /* Each worker owns a distinct slice of the seq space to avoid collisions */
    uint64_t seq_base = (uint64_t)wid * (MAX_PKTS / MAX_TX_LCORES);

    struct rte_mbuf *batch[BURST_SIZE];
    int batch_n = 0;

    /* Worker 0 marks the start time */
    if (wid == 0)
        first_tx_tsc = bench_rdtsc();

    uint64_t next_send = bench_rdtsc();

    while (sent < total && !force_stop) {
        uint32_t seq   = (uint32_t)((seq_base + sent + batch_n) % MAX_PKTS);
        uint16_t sport = BASE_SPORT + (seq % 16384);

        while (bench_rdtsc() < next_send)
            bench_pause();

        uint64_t now_ns = bench_rdtsc() * 1000000000ULL / hz;
        send_tsc[seq] = now_ns;

        struct rte_mbuf *m = build_pkt(seq, now_ns, sport);
        if (unlikely(!m)) { printf("TX%d: mbuf alloc failed\n", wid); break; }

        batch[batch_n++] = m;
        next_send += interval;

        if (batch_n == BURST_SIZE || bench_rdtsc() >= next_send) {
            uint16_t nb = 0;
            while (nb < batch_n)
                nb += rte_eth_tx_burst(PORT_ID, txq, batch + nb, batch_n - nb);
            atomic_fetch_add(&tx_sent, batch_n);
            sent += batch_n;
            batch_n = 0;
        }
    }

    /* flush remainder */
    if (batch_n > 0) {
        uint16_t nb = 0;
        while (nb < batch_n)
            nb += rte_eth_tx_burst(PORT_ID, txq, batch + nb, batch_n - nb);
        atomic_fetch_add(&tx_sent, batch_n);
        sent += batch_n;
    }

    /* Worker 0 marks the end time */
    uint64_t my_end = bench_rdtsc();
    uint64_t cur = atomic_load(&last_tx_tsc_atomic);
    while (my_end > cur) {
        if (atomic_compare_exchange_weak(&last_tx_tsc_atomic, &cur, my_end))
            break;
    }

    printf("TX worker %d done: sent %lu\n", wid, sent);
    atomic_fetch_add(&tx_done_cnt, 1);
    return 0;
}

/* ── RX thread ──────────────────────────────────────────────────────────── */
static int rx_thread(void *arg)
{
    int qid = arg ? ((rx_arg_t *)arg)->queue_id : 0;
    printf("RX worker %d on lcore %u  queue %d\n", qid, rte_lcore_id(), qid);

    struct rte_mbuf *pkts[BURST_SIZE];
    uint64_t hz = rte_get_tsc_hz();

    while (!force_stop) {
        uint16_t nb = rte_eth_rx_burst(PORT_ID, qid, pkts, BURST_SIZE);
        if (nb == 0) continue;

        // for (int i = 0; i < nb; i++) rte_pktmbuf_free(pkts[i]);
        // atomic_fetch_add(&rx_recvd, nb);

        for (int i = 0; i < nb; i++) {
            struct rte_mbuf *m = pkts[i];
            uint8_t *buf  = rte_pktmbuf_mtod(m, uint8_t *);
            uint8_t *pay  = buf + sizeof(struct rte_ether_hdr)
                               + sizeof(struct rte_ipv4_hdr)
                               + sizeof(struct rte_udp_hdr);
            uint16_t plen = m->data_len
                          - sizeof(struct rte_ether_hdr)
                          - sizeof(struct rte_ipv4_hdr)
                          - sizeof(struct rte_udp_hdr);

            if (plen < 4) goto free_pkt;
            if (ntohs(*(uint16_t *)pay) != OPTYPE_GETRES) goto free_pkt;
            if (plen < 14) goto free_pkt;

            uint16_t vallen = ntohs(*(uint16_t *)(pay + 12));
            uint16_t off2   = 14 + vallen;
            if (plen < off2 + 8 + 2) goto free_pkt;

            uint16_t path_len_field = ntohs(*(uint16_t *)(pay + off2 + 6));
            uint16_t path_start     = off2 + 8;
            if (plen < path_start + path_len_field) goto free_pkt;
            if (path_len_field < 12) goto free_pkt;

            uint8_t *tail   = pay + path_start + path_len_field - 12;
            uint64_t snt_ns = bench_ntohll(*(uint64_t *)tail);
            uint64_t now_ns = bench_rdtsc() * 1000000000ULL / hz;
            uint64_t lat_ns = now_ns - snt_ns;

            if (lat_ns == 0 || lat_ns > 100000000ULL) goto free_pkt;

            long idx = atomic_fetch_add(&lat_idx, 1);
            if (idx < (long)cfg_count)
                latencies[idx] = lat_ns;

            atomic_fetch_add(&rx_recvd, 1);

free_pkt:
            rte_pktmbuf_free(m);
        }
    }

    printf("RX worker %d done: received %ld\n", qid, atomic_load(&rx_recvd));
    return 0;
}

/* ── Stats ──────────────────────────────────────────────────────────────── */
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(uint64_t*)a, y = *(uint64_t*)b;
    return (x > y) - (x < y);
}

static void print_stats(void) {
    long n = atomic_load(&rx_recvd);
    long s = atomic_load(&tx_sent);
    if (n == 0) { printf("No responses received.\n"); return; }
    if (n > (long)cfg_count) n = (long)cfg_count;

    qsort(latencies, n, sizeof(uint64_t), cmp_u64);

    double sum = 0;
    for (long i = 0; i < n; i++) sum += latencies[i];

    #define PCT(p) (latencies[(long)((p)/100.0*n)] / 1000.0)

    uint64_t hz = rte_get_tsc_hz();
    double duration_sec = (double)(atomic_load(&last_tx_tsc_atomic) - first_tx_tsc) / hz;

    struct rte_eth_stats stats;
    rte_eth_stats_get(PORT_ID, &stats);

    double throughput   = (duration_sec > 0) ? (double)(stats.ipackets+stats.imissed) / duration_sec : 0;
    
    printf("\n=== Results ===\n");
    printf("  TX lcores  : %d  RX lcores: %d\n", cfg_tx_lcores, cfg_rx_lcores);
    printf("  Sent       : %ld\n", s);
    printf("  Received   : %ld  (loss %.2f%%)\n", n, 100.0*(s-n)/s);
    printf("  Duration   : %.3f s\n", duration_sec);
    printf("  Port RX    : %lu pkts  missed=%lu  errors=%lu recved=%lu\n",
        stats.ipackets, stats.imissed, stats.ierrors, stats.ipackets+stats.imissed);
    printf("  Port TX    : %lu pkts  errors=%lu\n",
        stats.opackets, stats.oerrors);
    printf("  Throughput : %.1f pps  (%.3f Mpps)\n", throughput, throughput/1e6);
    printf("  Latency µs : min=%.1f  mean=%.1f  p50=%.1f  p95=%.1f  p99=%.1f  max=%.1f\n",
           latencies[0]/1000.0, sum/n/1000.0,
           PCT(50), PCT(95), PCT(99), latencies[n-1]/1000.0);
}

/* ── Port init ──────────────────────────────────────────────────────────── */
static void port_init(int nrxq, int ntxq) {
    struct rte_eth_conf port_conf;
    memset(&port_conf, 0, sizeof(port_conf));
    port_conf.rxmode.max_rx_pkt_len = RTE_ETHER_MAX_LEN;
    if (nrxq > 1) {
        port_conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
        port_conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_UDP | ETH_RSS_IP;
    }
    port_conf.txmode.mq_mode = ETH_MQ_TX_NONE;

    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(PORT_ID, &dev_info);
    port_conf.rx_adv_conf.rss_conf.rss_hf &= dev_info.flow_type_rss_offloads;

    int ret = rte_eth_dev_configure(PORT_ID, nrxq, ntxq, &port_conf);
    if (ret < 0) rte_exit(EXIT_FAILURE, "dev_configure failed\n");

    struct rte_eth_rxconf rxconf = dev_info.default_rxconf;
    for (int q = 0; q < nrxq; q++) {
        ret = rte_eth_rx_queue_setup(PORT_ID, q, NUM_RX_DESC,
            rte_eth_dev_socket_id(PORT_ID), &rxconf, mbuf_pool);
        if (ret < 0) rte_exit(EXIT_FAILURE, "rx_queue_setup q=%d failed\n", q);
    }

    struct rte_eth_txconf txconf = dev_info.default_txconf;
    for (int q = 0; q < ntxq; q++) {
        ret = rte_eth_tx_queue_setup(PORT_ID, q, NUM_TX_DESC,
            rte_eth_dev_socket_id(PORT_ID), &txconf);
        if (ret < 0) rte_exit(EXIT_FAILURE, "tx_queue_setup q=%d failed\n", q);
    }

    ret = rte_eth_dev_start(PORT_ID);
    if (ret < 0) rte_exit(EXIT_FAILURE, "dev_start failed\n");
    rte_eth_promiscuous_enable(PORT_ID);
    printf("Port %u started  rxq=%d  txq=%d\n", PORT_ID, nrxq, ntxq);
}

/* ── CLI ────────────────────────────────────────────────────────────────── */
static void parse_args(int argc, char **argv) {
    int opt;
    static struct option long_opts[] = {
        {"rate",      required_argument, 0, 'r'},
        {"count",     required_argument, 0, 'c'},
        {"recir",     required_argument, 0, 'R'},
        {"depth",     required_argument, 0, 'd'},
        {"tx-lcores", required_argument, 0, 't'},
        {"rx-lcores", required_argument, 0, 'x'},
        {0, 0, 0, 0}
    };
    while ((opt = getopt_long(argc, argv, "r:c:R:d:t:x:", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'r': cfg_rate_pps  = atoll(optarg); break;
        case 'c': cfg_count     = atoll(optarg); break;
        case 'R': cfg_recir     = atoi(optarg);  break;
        case 'd': cfg_depth     = atoi(optarg);  break;
        case 't': cfg_tx_lcores = atoi(optarg);  break;
        case 'x': cfg_rx_lcores = atoi(optarg);  break;
        }
    }
    if (cfg_tx_lcores < 1) cfg_tx_lcores = 1;
    if (cfg_tx_lcores > MAX_TX_LCORES) cfg_tx_lcores = MAX_TX_LCORES;
    if (cfg_rx_lcores < 1) cfg_rx_lcores = 1;
    if (cfg_rx_lcores > MAX_RX_LCORES) cfg_rx_lcores = MAX_RX_LCORES;
    printf("Config: rate=%lu  count=%lu  recir=%u  depth=%u  tx=%d  rx=%d\n",
           cfg_rate_pps, cfg_count, cfg_recir, cfg_depth, cfg_tx_lcores, cfg_rx_lcores);
}

static void sig_handler(int sig) { (void)sig; force_stop = 1; }

/* ── Main ───────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed\n");
    argc -= ret; argv += ret;

    parse_args(argc, argv);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    send_tsc  = rte_zmalloc("send_tsc",  MAX_PKTS * sizeof(uint64_t), 64);
    latencies = rte_zmalloc("latencies", cfg_count * sizeof(uint64_t), 64);
    if (!send_tsc || !latencies) rte_exit(EXIT_FAILURE, "zmalloc failed\n");

    atomic_init(&tx_sent, 0);
    atomic_init(&rx_recvd, 0);
    atomic_init(&lat_idx, 0);
    atomic_init(&tx_done_cnt, 0);
    atomic_init(&last_tx_tsc_atomic, 0);

    mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", MBUF_POOL_SIZE,
        MBUF_CACHE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    build_payload();
    port_init(cfg_rx_lcores, cfg_tx_lcores);

    /* lcore layout:
     *   main lcore (0)           → TX worker 0
     *   next cfg_rx_lcores lcores → RX workers 0..N-1
     *   remaining lcores          → TX workers 1,2,...  */
    unsigned main_lcore = rte_get_main_lcore();
    tx_lcore_ids[0] = main_lcore;

    /* Assign RX lcores */
    unsigned lc = main_lcore;
    int rx_found = 0;
    while (rx_found < cfg_rx_lcores) {
        lc = rte_get_next_lcore(lc, 1, 0);
        if (lc == RTE_MAX_LCORE) {
            printf("Warning: only %d RX lcores available\n", rx_found);
            cfg_rx_lcores = rx_found;
            break;
        }
        rx_lcore_ids[rx_found++] = lc;
    }

    /* Assign extra TX lcores */
    int tx_found = 1;
    while (tx_found < cfg_tx_lcores) {
        lc = rte_get_next_lcore(lc, 1, 0);
        if (lc == RTE_MAX_LCORE) {
            printf("Warning: only %d TX lcores available\n", tx_found);
            cfg_tx_lcores = tx_found;
            break;
        }
        tx_lcore_ids[tx_found++] = lc;
    }

    printf("TX workers:");
    for (int i = 0; i < cfg_tx_lcores; i++) printf(" %u", tx_lcore_ids[i]);
    printf("\nRX workers:");
    for (int i = 0; i < cfg_rx_lcores; i++) printf(" %u", rx_lcore_ids[i]);
    printf("\n");

    /* Launch RX workers */
    for (int i = 0; i < cfg_rx_lcores; i++) {
        rx_args[i].queue_id = i;
        rte_eal_remote_launch(rx_thread, &rx_args[i], rx_lcore_ids[i]);
    }

    /* Launch extra TX workers (1+) */
    for (int i = 1; i < cfg_tx_lcores; i++) {
        tx_args[i].worker_id = i;
        rte_eal_remote_launch(tx_thread, &tx_args[i], tx_lcore_ids[i]);
    }

    /* TX worker 0 on main lcore */
    tx_args[0].worker_id = 0;
    tx_thread(&tx_args[0]);

    /* Wait for all TX workers */
    while (atomic_load(&tx_done_cnt) < cfg_tx_lcores)
        usleep(1000);

    /* Drain remaining responses then stop */
    sleep(3);
    force_stop = 1;
    for (int i = 0; i < cfg_rx_lcores; i++)
        rte_eal_wait_lcore(rx_lcore_ids[i]);
    for (int i = 1; i < cfg_tx_lcores; i++)
        rte_eal_wait_lcore(tx_lcore_ids[i]);

    print_stats();

    rte_eth_dev_stop(PORT_ID);
    rte_eth_dev_close(PORT_ID);
    return 0;
}