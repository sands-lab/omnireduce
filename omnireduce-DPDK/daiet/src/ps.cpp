/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#ifdef COLOCATED
#include "ps.hpp"
#include "common.hpp"
#include "utils.hpp"
#include "params.hpp"
#include "stats.hpp"

using namespace std;

namespace daiet {

    struct mac_ip_pair {
        struct rte_ether_addr mac;
        uint32_t be_ip;
    };

    thread_local static uint32_t num_updates;
    thread_local static mac_ip_pair* ps_workers_ip_to_mac;
    thread_local static uint32_t known_workers = 0;

    thread_local static int32_t** ps_aggregated_messages;
    thread_local static uint32_t* ps_received_message_counters;

    thread_local static uint16_t ps_port_be;

#ifdef DEBUG
    __rte_always_inline struct daiet_hdr * is_daiet_pkt_to_ps(struct rte_ether_hdr* eth_hdr, uint16_t size) {

        int idx;
        uint16_t etherType;
        struct rte_ipv4_hdr* ip_hdr;
        struct rte_udp_hdr* rte_udp_hdr;

        idx = sizeof(struct rte_ether_hdr);
        etherType = rte_be_to_cpu_16(eth_hdr->ether_type);

        if (etherType == RTE_ETHER_TYPE_IPV4 && size >= idx + sizeof(struct rte_ipv4_hdr)) {

            idx += sizeof(struct rte_ipv4_hdr);
            ip_hdr = (struct rte_ipv4_hdr *) (eth_hdr + 1);

            if (ip_hdr->next_proto_id == IPPROTO_UDP && size >= idx + sizeof(struct rte_udp_hdr)) {
                idx += sizeof(struct rte_udp_hdr);
                rte_udp_hdr = (struct rte_udp_hdr *) (ip_hdr + 1);

                if (rte_udp_hdr->dst_port == ps_port_be && size >= idx + sizeof(struct daiet_hdr)) {

                    return (struct daiet_hdr *) (rte_udp_hdr + 1);
                }
            }
        }
        return NULL;
    }
#endif

    __rte_always_inline void ps_msg_setup(struct daiet_hdr * daiet, uint16_t pool_index) {

        struct entry_hdr *entry;
        int32_t* base_ptr = ps_aggregated_messages[pool_index];

        entry = (struct entry_hdr *) (daiet + 1);
        for (uint32_t i = 0; i < num_updates; i++, entry++) {
            entry->upd = rte_cpu_to_be_32(base_ptr[i]);
            base_ptr[i] = 0;
        }
    }

    /* Returns true if the aggregation for the offset is complete */
    __rte_always_inline bool ps_aggregate_message(struct daiet_hdr* daiet, uint32_t be_src_ip, struct rte_ether_addr src_mac, uint16_t pool_index, uint16_t num_workers) {

        struct entry_hdr * entry = (struct entry_hdr *) (daiet + 1);
        int32_t* base_ptr = ps_aggregated_messages[pool_index];

        for (uint32_t i = 0; i < num_updates; i++, entry++) {
            base_ptr[i] += rte_be_to_cpu_32(entry->upd);
        }

        if (unlikely(known_workers < num_workers)) {

            bool found = false;

            for (uint32_t i = 0; i < known_workers && !found; i++) {

                if (ps_workers_ip_to_mac[i].be_ip==be_src_ip)
                    found = true;
            }

            if (!found) {

                // New worker
                char ipstring[INET_ADDRSTRLEN];

                if (unlikely(inet_ntop(AF_INET, &be_src_ip, ipstring, INET_ADDRSTRLEN) == NULL)) {
                    LOG_FATAL("Wrong IP: error " + to_string(errno));
                }

                LOG_INFO("Worker: " + string(ipstring) + " " + mac_to_str(src_mac));

                ps_workers_ip_to_mac[known_workers].mac = src_mac;
                ps_workers_ip_to_mac[known_workers].be_ip = be_src_ip;
                known_workers++;
            }
        }

        ps_received_message_counters[pool_index]--;

        if (unlikely(ps_received_message_counters[pool_index]==0)) {
            ps_received_message_counters[pool_index] = num_workers;
            return true;
        }

        return false;
    }

    void ps_setup() {
    }

    void ps_cleanup() {
    }

    int ps(void* num_worker_threads) {

        int ret;

        unsigned lcore_id;
        unsigned nb_rx = 0, j = 0, i = 0, nb_tx = 0, sent = 0;

        uint16_t ps_id, id_shift = *((uint16_t*)(num_worker_threads));
        uint16_t num_workers = daiet_par.getNumWorkers();
        const uint32_t max_num_pending_messages = daiet_par.getMaxNumPendingMessages();
        num_updates = daiet_par.getNumUpdates();
        uint64_t ps_tx = 0, ps_rx = 0;

        struct rte_mempool *pool;
        string pool_name = "ps_pool";
        struct rte_mbuf** pkts_burst;
        struct rte_mbuf* m;
        struct rte_mbuf** clone_burst;

        struct rte_ether_hdr* eth;
        struct rte_ipv4_hdr * ip;
        struct rte_udp_hdr * udp;
        struct daiet_hdr* daiet;
        uint16_t pool_index = 0, start_pool_index = 0;

        // Get core ID
        lcore_id = rte_lcore_id();
        ps_id = dpdk_data.core_to_thread_id[lcore_id];
        LOG_DEBUG("PS core: " + to_string(lcore_id) + " PS id: " + to_string(ps_id));

        start_pool_index = (ps_id - id_shift) * max_num_pending_messages;
        ps_port_be = rte_cpu_to_be_16(daiet_par.getBasePsPort() + ps_id - id_shift);

        ps_aggregated_messages = (int32_t**) rte_malloc_socket(NULL, max_num_pending_messages * sizeof(int32_t*), RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (ps_aggregated_messages == NULL)
            LOG_FATAL("Failed PS aggregated messages allocation!");

        for (i = 0; i < max_num_pending_messages; i++) {
            ps_aggregated_messages[i] = (int32_t*) rte_zmalloc_socket(NULL, num_updates * sizeof(int32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
            if (ps_aggregated_messages[i] == NULL)
                LOG_FATAL("Failed PS aggregated messages allocation: element " + to_string(i));
        }

        ps_received_message_counters = (uint32_t*) rte_zmalloc_socket(NULL, max_num_pending_messages * sizeof(uint32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (ps_received_message_counters == NULL)
            LOG_FATAL("Failed PS aggregated messages allocation!");

        for (i = 0; i < max_num_pending_messages; i++) {
            ps_received_message_counters[i] = num_workers;
        }

        ps_workers_ip_to_mac = (mac_ip_pair*) rte_zmalloc_socket(NULL, num_workers * sizeof(struct mac_ip_pair), RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (ps_workers_ip_to_mac == NULL)
            LOG_FATAL("PS thread: cannot allocate ps_workers_ip_to_mac");

        pkts_burst = (rte_mbuf **) rte_malloc_socket(NULL, dpdk_par.burst_rx * sizeof(struct rte_mbuf*), RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (pkts_burst == NULL)
            LOG_FATAL("PS thread: cannot allocate pkts burst");

        clone_burst = (rte_mbuf **) rte_malloc_socket(NULL, num_workers * sizeof(struct rte_mbuf*), RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (clone_burst == NULL)
            LOG_FATAL("PS thread: cannot allocate clone burst");

        // Init the buffer pool
        pool_name = pool_name + to_string(ps_id);
        pool = rte_pktmbuf_pool_create(pool_name.c_str(), dpdk_par.pool_size, dpdk_par.pool_cache_size, 0, dpdk_data.pool_buffer_size, rte_socket_id());
        if (pool == NULL)
            LOG_FATAL("Cannot init mbuf pool: " + string(rte_strerror(rte_errno)));

        while (!force_quit) {

            nb_rx = rte_eth_rx_burst(dpdk_par.portid, ps_id, pkts_burst, dpdk_par.burst_rx);

            for (j = 0; j < nb_rx; j++) {

                m = pkts_burst[j];

                rte_prefetch0 (rte_pktmbuf_mtod(m, void *));
                eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);

#ifdef DEBUG
                daiet = is_daiet_pkt_to_ps(eth, m->data_len);
                if (likely(daiet != NULL)) {
#else
                    daiet = (struct daiet_hdr *) ((uint8_t *) (eth+1) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr));
#endif

                    ps_rx++;
                    ip = (struct rte_ipv4_hdr *) (eth + 1);
                    udp = (struct rte_udp_hdr *) (ip + 1);

                    pool_index = (rte_be_to_cpu_16(daiet->pool_index) & 0x7FFF) - start_pool_index;

                    if (ps_aggregate_message(daiet, ip->src_addr, eth->s_addr, pool_index, num_workers)) {

                        // Checksum offload
                        m->l2_len = sizeof(struct rte_ether_hdr);
                        m->l3_len = sizeof(struct rte_ipv4_hdr);
                        m->ol_flags |= daiet_par.getTxFlags();

                        // Set src MAC
                        rte_ether_addr_copy(&(eth->d_addr), &(eth->s_addr));

                        // Set src IP
                        ip->hdr_checksum = 0;
                        ip->src_addr = ip->dst_addr;

                        // Swap ports
                        swap((uint16_t&) (udp->dst_port), (uint16_t&) (udp->src_port));
                        udp->dgram_cksum = rte_ipv4_phdr_cksum(ip, m->ol_flags);

                        ps_msg_setup(daiet, pool_index);

                        // Allocate pkt burst
                        ret = rte_pktmbuf_alloc_bulk(pool, clone_burst, num_workers);
                        if (unlikely(ret < 0))
                            LOG_FATAL("Cannot allocate clone burst");

                        for (i = 0; i < num_workers; i++) {

                            // Clone packet
                            deep_copy_single_segment_pkt(clone_burst[i], m);

                            eth = rte_pktmbuf_mtod(clone_burst[i], struct rte_ether_hdr *);

                            // Set dst MAC
                            rte_ether_addr_copy(&(ps_workers_ip_to_mac[i].mac), &(eth->d_addr));

                            // Set dst IP
                            ip = (struct rte_ipv4_hdr *) (eth + 1);
                            ip->dst_addr = ps_workers_ip_to_mac[i].be_ip;
                        }

                        // Send packet burst
                        sent = 0;
                        do {
                            nb_tx = rte_eth_tx_burst(dpdk_par.portid, ps_id,clone_burst, num_workers);

                            sent += nb_tx;
                        } while (sent < num_workers);

                        ps_tx += num_workers;

                        // Free original packet
                        rte_pktmbuf_free(m);

                    } else {
                        // Free original packet
                        rte_pktmbuf_free(m);
                    }
#ifdef DEBUG
                } else {
                    // Free original packet
                    rte_pktmbuf_free(m);
                }
#endif
            }
        }

        // Set stats
        pkt_stats.set_ps(ps_id - id_shift, ps_tx, ps_rx);

        // Cleanup
        rte_free(clone_burst);
        rte_free(pkts_burst);

        rte_free(ps_workers_ip_to_mac);

        rte_free(ps_received_message_counters);

        for (uint32_t i = 0; i < max_num_pending_messages; i++) {
            rte_free(ps_aggregated_messages[i]);
        }

        rte_free(ps_aggregated_messages);

        return 0;
    }
}
#endif
