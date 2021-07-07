/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include <map>
#include "ps.hpp"
#include "common.hpp"
#include "utils.hpp"
#include "params.hpp"
#include "stats.hpp"

#ifdef TIMERS
#include <unordered_map>
#endif

using namespace std;

namespace daiet {
#ifdef NOSCALING
    enum TensorUpdateType {
        NONE = 0, INT32 = 1, FLOAT32 = 2, FLOAT16 = 3
    };
#endif
    struct mac_ip_pair {
        struct rte_ether_addr mac;
        uint32_t be_ip;
    };
    struct worker_info {
        uint32_t worker_ip;
        uint32_t next_tsi;
#ifndef NOSCALING
        int16_t next_exp;
#endif
    };

    thread_local static uint32_t num_updates;
    thread_local static mac_ip_pair* ps_workers_ip_to_mac;
    thread_local static uint32_t known_workers = 0;

    thread_local static int32_t** ps_aggregated_messages;
    thread_local static uint32_t* ps_received_message_counters;

    thread_local static uint16_t ps_port_be;

#ifdef TIMERS
    thread_local static unordered_map<uint32_t, uint32_t> ip_to_worker_idx;
#endif

    thread_local static uint16_t ps_id;
    thread_local static struct rte_mempool *pool;
    thread_local static struct rte_mbuf** clone_burst;
    thread_local static uint64_t ps_tx = 0;    
    thread_local static worker_info** ps_workers_info;
    thread_local static uint32_t* min_next_tsi;

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

    __rte_always_inline void ps_msg_setup(struct daiet_hdr * daiet, uint16_t pool_index, uint16_t num_workers) {

        struct entry_hdr *entry;
        int32_t* base_ptr = ps_aggregated_messages[pool_index];
#ifndef NOSCALING
        worker_info* winfo_ptr = ps_workers_info[pool_index];
        struct exp_hdr * exp = (struct exp_hdr *) (((struct entry_hdr *) (daiet + 1)) + num_updates);
        int16_t max_exp = -126;
        for (uint16_t i = 0; i<num_workers; i++){
            if (daiet->next_tsi == winfo_ptr[i].next_tsi && max_exp < winfo_ptr[i].next_exp) {
                max_exp = winfo_ptr[i].next_exp;
                winfo_ptr[i].next_exp = -126;
            }
        }
        exp->exp = rte_cpu_to_be_16((uint16_t) max_exp);
#endif
#ifdef ALGO2
#ifndef TIMERS
        min_next_tsi[pool_index] = UINT32_MAX;
#endif
#endif
        entry = (struct entry_hdr *) (daiet + 1);
        for (uint32_t i = 0; i < num_updates; i++, entry++) {
            entry->upd = rte_cpu_to_be_32(base_ptr[i]);
#ifndef TIMERS
            base_ptr[i] = 0;
#endif
        }
    }

    __rte_always_inline void register_worker(uint32_t be_src_ip, struct rte_ether_addr src_mac) {
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
#ifdef TIMERS
            ip_to_worker_idx.insert(make_pair(be_src_ip, known_workers));
#endif
            known_workers++;
        }
    }

    __rte_always_inline void update_workerinfo(struct daiet_hdr* daiet, uint32_t be_src_ip, uint16_t num_workers, uint16_t pool_index){
        worker_info* winfo_ptr = ps_workers_info[pool_index];
#ifndef NOSCALING
        struct exp_hdr * exp = (struct exp_hdr *) (((struct entry_hdr *) (daiet + 1)) + num_updates);
#endif
        for (uint32_t i = 0; i < num_workers; i++) {
            if (ps_workers_ip_to_mac[i].be_ip==be_src_ip){
                winfo_ptr[i].next_tsi = daiet->next_tsi;
#ifndef NOSCALING
                winfo_ptr[i].next_exp = (int16_t)rte_be_to_cpu_16(exp->exp);
#endif
                break;
            }         
        }
    }

    /* Returns true if the aggregation for the offset is complete */
    __rte_always_inline bool ps_aggregate_message(struct daiet_hdr* daiet, uint32_t be_src_ip, struct rte_ether_addr src_mac, uint16_t set_pool_index, uint16_t shadow_pool_index, uint16_t num_workers) {

        struct entry_hdr * entry = (struct entry_hdr *) (daiet + 1);
#ifndef ALGO2
        worker_info* winfo_ptr = ps_workers_info[set_pool_index];
#endif
#ifdef ALGO2
        if (min_next_tsi[set_pool_index]>daiet->next_tsi) min_next_tsi[set_pool_index]=daiet->next_tsi;
        ps_received_message_counters[set_pool_index]--;
#endif
#ifdef NOSCALING
        switch (daiet->data_type) {
            case INT32:
                {
                    int32_t* base_ptr_int = ps_aggregated_messages[set_pool_index];
                    for (uint32_t i = 0; i < num_updates; i++, entry++) {
                        uint32_t tmp = rte_be_to_cpu_32(entry->upd);
                        base_ptr_int[i] += ((int*)&tmp)[0]; 
                    }
                }
                break;
            case FLOAT32:
                {
                    float*  base_ptr_float32 = (float*)ps_aggregated_messages[set_pool_index];
                    for (uint32_t i = 0; i < num_updates; i++, entry++) {
                        uint32_t tmp = rte_be_to_cpu_32(entry->upd);
                        base_ptr_float32[i] += ((float*)&tmp)[0]; 
                    }
                }
                break;
            case FLOAT16:
                {
                    float*  base_ptr_float16 = (float*)ps_aggregated_messages[set_pool_index];
                    for (uint32_t i = 0; i < num_updates; i++, entry++) {
                        uint32_t tmp = rte_be_to_cpu_32(entry->upd);;
                        base_ptr_float16[i] += ((float*)&tmp)[0]; 
                    }
                }
                break;
            default:
                LOG_FATAL("Tensor type error");
        }
#else
        int32_t* base_ptr = ps_aggregated_messages[set_pool_index];
        struct exp_hdr * exp = (struct exp_hdr *) (((struct entry_hdr *) (daiet + 1)) + num_updates);
        for (uint32_t i = 0; i < num_updates; i++, entry++) {
            base_ptr[i] += rte_be_to_cpu_32(entry->upd);
        }
#endif
#ifndef ALGO2
#ifndef NOSCALING
        bool all_equal = true;
#endif
        uint32_t min_nexttsi = UINT32_MAX;
        for (uint16_t i = 0; i < num_workers; i++) {
#ifndef NOSCALING
            if (winfo_ptr[i].next_tsi != daiet->tsi) {
                all_equal = false;
            }
#endif
            if (likely(winfo_ptr[i].next_tsi < min_nexttsi)) {
                min_nexttsi = winfo_ptr[i].next_tsi;
            }
        }
#endif        
#ifdef NOSCALING
#ifdef ALGO2
        if (unlikely(ps_received_message_counters[set_pool_index]==0)) {
#else
        if (daiet->tsi < min_nexttsi) {
            if(min_nexttsi==UINT32_MAX){
                for (uint16_t i = 0; i<num_workers; i++)
                    winfo_ptr[i].next_tsi = 0;
            }
#endif
#else
        if (all_equal || daiet->tsi < min_nexttsi) {
#endif
#ifdef ALGO2
            ps_received_message_counters[set_pool_index] = num_workers;
            daiet->next_tsi = min_next_tsi[set_pool_index];
#ifdef TIMERS
            //update ps_aggregated_messages and min_next_tsi of the other buffer
            int32_t* base_ptr = ps_aggregated_messages[shadow_pool_index];
            min_next_tsi[shadow_pool_index] = UINT32_MAX;
            memset(ps_aggregated_messages[shadow_pool_index], 0, num_updates * sizeof(int32_t));
#endif
#else
            daiet->next_tsi = min_nexttsi;
#endif
            return true;
        }
        return false;
    }

    void ps_setup() {
    }

    void ps_cleanup() {
    }

    int ps(void*) {
        
        int ret;

        unsigned lcore_id;
        unsigned nb_rx = 0, j = 0, i = 0;

        uint16_t num_workers = daiet_par.getNumWorkers();
        const uint32_t max_num_pending_messages = daiet_par.getMaxNumPendingMessages();
        num_updates = daiet_par.getNumUpdates();
        uint64_t ps_rx = 0;

        string pool_name = "ps_pool";
        struct rte_mbuf** pkts_burst;
        struct rte_mbuf* m;

        struct rte_ether_hdr* eth;
        struct rte_ipv4_hdr * ip;
        struct rte_udp_hdr * udp;
        struct daiet_hdr* daiet;
        uint16_t pool_index = 0, start_pool_index = 0, set_pool_index = 0, shadow_pool_index = 0, set = 0, nb_tx = 0, sent = 0;

#ifdef TIMERS
        const uint32_t monoset_bitmap_size = max_num_pending_messages * num_workers;
        uint32_t bitmap_index = 0, bitmap_shadow_index = 0, worker_idx = 0;

        // Bitmap
        void* bitmap_mem;
        uint32_t bitmap_size;
        struct rte_bitmap *bitmap;
#endif

        // Get core ID
        lcore_id = rte_lcore_id();
        ps_id = dpdk_data.core_to_thread_id[lcore_id];
        LOG_DEBUG("PS core: " + to_string(lcore_id) + " PS id: " + to_string(ps_id));

        start_pool_index = ps_id * max_num_pending_messages;
        ps_port_be = rte_cpu_to_be_16(daiet_par.getBasePsPort() + ps_id);

#ifdef TIMERS
        ps_aggregated_messages = (int32_t**) rte_malloc_socket(NULL, 2 * max_num_pending_messages * sizeof(int32_t*), RTE_CACHE_LINE_SIZE, rte_socket_id());
#else
        ps_aggregated_messages = (int32_t**) rte_malloc_socket(NULL, max_num_pending_messages * sizeof(int32_t*), RTE_CACHE_LINE_SIZE, rte_socket_id());
#endif
        if (ps_aggregated_messages == NULL)
            LOG_FATAL("Failed PS aggregated messages allocation!");

#ifdef TIMERS
        for (i = 0; i < 2 * max_num_pending_messages; i++) {
#else
        for (i = 0; i < max_num_pending_messages; i++) {
#endif
            ps_aggregated_messages[i] = (int32_t*) rte_zmalloc_socket(NULL, num_updates * sizeof(int32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
            if (ps_aggregated_messages[i] == NULL)
                LOG_FATAL("Failed PS aggregated messages allocation: element " + to_string(i));
        }

#ifndef ALGO2
        ps_workers_info = (worker_info**) rte_zmalloc_socket(NULL, max_num_pending_messages * sizeof(struct worker_info*), RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (ps_workers_info == NULL)
            LOG_FATAL("Failed PS worker information allocation!");
        
        for (i = 0; i < max_num_pending_messages; i++) {
            ps_workers_info[i] = (struct worker_info*) rte_zmalloc_socket(NULL, num_workers * sizeof(struct worker_info), RTE_CACHE_LINE_SIZE, rte_socket_id());
            if (ps_workers_info[i] == NULL)
                LOG_FATAL("Failed PS worker information allocation: element " + to_string(i));
            for (j = 0; j < num_workers; j++) {
                
#ifdef NOSCALING
                ps_workers_info[i][j].next_tsi = 0;
#else
                ps_workers_info[i][j].next_tsi = UINT32_MAX;
                ps_workers_info[i][j].next_exp = -126;
#endif
            }
        }
#endif
#ifdef ALGO2
#ifdef TIMERS
        ps_received_message_counters = (uint32_t*) rte_zmalloc_socket(NULL, 2 * max_num_pending_messages * sizeof(uint32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
#else
        ps_received_message_counters = (uint32_t*) rte_zmalloc_socket(NULL, max_num_pending_messages * sizeof(uint32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
#endif
        if (ps_received_message_counters == NULL)
            LOG_FATAL("Failed PS aggregated messages allocation!");

#ifdef TIMERS
        for (i = 0; i < 2 * max_num_pending_messages; i++) {
#else
        for (i = 0; i < max_num_pending_messages; i++) {
#endif
            ps_received_message_counters[i] = num_workers;
        }
#ifdef TIMERS
        min_next_tsi = (uint32_t*) rte_zmalloc_socket(NULL, 2 * max_num_pending_messages * sizeof(uint32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
#else        
        min_next_tsi = (uint32_t*) rte_zmalloc_socket(NULL, max_num_pending_messages * sizeof(uint32_t), RTE_CACHE_LINE_SIZE, rte_socket_id());
#endif
        if (min_next_tsi == NULL)
            LOG_FATAL("Failed PS min next tsi allocation!");

#ifdef TIMERS
        for (i = 0; i < 2 * max_num_pending_messages; i++) {
#else
        for (i = 0; i < max_num_pending_messages; i++) {
#endif
            min_next_tsi[i] = UINT32_MAX;
        }
#endif

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

#ifdef TIMERS
        // Initialize bitmap
        bitmap_size = rte_bitmap_get_memory_footprint(2 * monoset_bitmap_size);
        if (unlikely(bitmap_size == 0)) {
            LOG_FATAL("Bitmap failed");
        }

        bitmap_mem = rte_zmalloc_socket("bitmap", bitmap_size, RTE_CACHE_LINE_SIZE, rte_socket_id());
        if (unlikely(bitmap_mem == NULL)) {
            LOG_FATAL("Cannot allocate bitmap");
        }

        bitmap = rte_bitmap_init(2 * monoset_bitmap_size, (uint8_t*) bitmap_mem, bitmap_size);
        if (unlikely(bitmap == NULL)) {
            LOG_FATAL("Failed to init bitmap");
        }
        rte_bitmap_reset(bitmap);
#endif

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

                    if (unlikely(known_workers < num_workers)) {
                        register_worker(ip->src_addr, eth->s_addr);
                    }

                    udp = (struct rte_udp_hdr *) (ip + 1);

                    pool_index = rte_be_to_cpu_16(daiet->pool_index);
                    set = ((pool_index  >> 15) & 1);
                    pool_index = (pool_index & 0x7FFF) - start_pool_index;
#ifndef TIMERS
                    set_pool_index = pool_index;
#else
                    set_pool_index = (set == 0) ? pool_index : pool_index + max_num_pending_messages;
                    shadow_pool_index = (set == 0) ? pool_index + max_num_pending_messages : pool_index;
                    worker_idx = ip_to_worker_idx.find(ip->src_addr)->second;
                    //if (worker_idx==1)
                    //    std::cout<<"worker id: "<<worker_idx<<"; tsi"<<daiet->tsi<<"; pool index: "<<pool_index<<"; set: "<<set<<std::endl;
                    if (set == 0) {
                        bitmap_index = pool_index + worker_idx * max_num_pending_messages;
                        bitmap_shadow_index = bitmap_index + monoset_bitmap_size;
                    } else {
                        bitmap_shadow_index = pool_index + worker_idx * max_num_pending_messages;
                        bitmap_index = bitmap_shadow_index + monoset_bitmap_size;
                    }        
                    if (rte_bitmap_get(bitmap, bitmap_index) == 0) {

                        rte_bitmap_set(bitmap, bitmap_index);
                        rte_bitmap_clear(bitmap, bitmap_shadow_index);            
#endif
                    
                    

#ifndef ALGO2
                    update_workerinfo(daiet, ip->src_addr, num_workers, set_pool_index);
#endif                   
                        if (ps_aggregate_message(daiet, ip->src_addr, eth->s_addr, set_pool_index, shadow_pool_index, num_workers)) {
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
                            ps_msg_setup(daiet, set_pool_index, num_workers);
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
                        }
                        // Free original packet
                        rte_pktmbuf_free(m);
#ifdef TIMERS
                    }
                    else{
                        if (ps_received_message_counters[set_pool_index] == num_workers) {
                            daiet->next_tsi = min_next_tsi[set_pool_index];
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
                            ps_msg_setup(daiet, set_pool_index, num_workers);
                            ret = rte_pktmbuf_alloc_bulk(pool, clone_burst, 1);
                            if (unlikely(ret < 0))
                                LOG_FATAL("Cannot allocate clone burst");
                            deep_copy_single_segment_pkt(clone_burst[0], m); 
                            eth = rte_pktmbuf_mtod(clone_burst[0], struct rte_ether_hdr *); 
                            // Set dst MAC
                            rte_ether_addr_copy(&(ps_workers_ip_to_mac[worker_idx].mac), &(eth->d_addr));
                            // Set dst IP
                            ip = (struct rte_ipv4_hdr *) (eth + 1);
                            ip->dst_addr = ip->src_addr;
                            // Send packet burst
                            sent = 0;
                            do {
                                nb_tx = rte_eth_tx_burst(dpdk_par.portid, ps_id,clone_burst, 1);
    
                                sent += nb_tx;
                            } while (sent < 1);                            
                            ps_tx += 1;
                        }
                        // Free original packet
                        rte_pktmbuf_free(m);
                    }
#endif
#ifdef DEBUG
                }
#endif
            }
        }

        // Set stats
        pkt_stats.set_ps(ps_id, ps_tx, ps_rx);

        // Cleanup
        rte_free(clone_burst);
        rte_free(pkts_burst);

        rte_free(ps_workers_ip_to_mac);
#ifdef TIMERS
        rte_bitmap_free(bitmap);
        rte_free(bitmap_mem);
#endif
#ifdef ALGO2
        rte_free(ps_received_message_counters);
        rte_free(min_next_tsi);
#endif
#ifdef TIMERS
        for (uint32_t i = 0; i < 2*max_num_pending_messages; i++) {
#else
        for (uint32_t i = 0; i < max_num_pending_messages; i++) {
#endif
            rte_free(ps_aggregated_messages[i]);
        }

        rte_free(ps_aggregated_messages);

        return 0;
    }
}