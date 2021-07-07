/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include "daiet.hpp"

//#include <sys/mman.h>

#include "daiet.hpp"
#include "common.hpp"
#include "utils.hpp"
#include "params.hpp"
#include "stats.hpp"
#include "worker.hpp"
#ifdef COLOCATED
#include "ps.hpp"
#endif

using namespace std;

namespace daiet {

    struct rte_flow* insert_flow_rule(uint8_t port_id, uint16_t rx_q, uint16_t udp_dst_port) {

        struct rte_flow_attr attr;
        struct rte_flow_item pattern[4];
        struct rte_flow_action action[2];
        struct rte_flow *flow = NULL;
        struct rte_flow_action_queue queue;
        struct rte_flow_item_eth eth_spec;
        struct rte_flow_item_eth eth_mask;
        struct rte_flow_item_ipv4 ip_spec;
        struct rte_flow_item_ipv4 ip_mask;
        struct rte_flow_item_udp udp_spec;
        struct rte_flow_item_udp udp_mask;
        struct rte_flow_error error;

        // Rule attribute (ingress)
        memset(&attr, 0, sizeof(struct rte_flow_attr));
        attr.ingress = 1;

        memset(pattern, 0, sizeof(pattern));
        memset(action, 0, sizeof(action));

        // Action sequence
        queue.index = rx_q;
        action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
        action[0].conf = &queue;
        action[1].type = RTE_FLOW_ACTION_TYPE_END;

        // First level of the pattern (eth)
        memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
        memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
        pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
        pattern[0].spec = &eth_spec;
        pattern[0].mask = &eth_mask;

        // Second level of the pattern (IPv4)
        memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
        memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
        ip_spec.hdr.next_proto_id = 17;
        ip_mask.hdr.next_proto_id = 0xFF;
        pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
        pattern[1].spec = &ip_spec;
        pattern[1].mask = &ip_mask;

        // Third level of the pattern (UDP)
        memset(&udp_spec, 0, sizeof(struct rte_flow_item_udp));
        memset(&udp_mask, 0, sizeof(struct rte_flow_item_udp));
        udp_spec.hdr.dst_port = rte_cpu_to_be_16(udp_dst_port);
        udp_mask.hdr.dst_port = 0xFFFF;
        pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
        pattern[2].spec = &udp_spec;
        pattern[2].mask = &udp_mask;

        // Final level
        pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

        if (rte_flow_validate(port_id, &attr, pattern, action, &error) != 0)
            LOG_FATAL ("Flow rule can't be added: " + to_string(error.type) + " " + string((error.message ? error.message : "(no stated reason)")));

        flow = rte_flow_create(port_id, &attr, pattern, action, &error);

        return flow;
    }

#ifndef COLOCATED
    void port_init(uint16_t num_workers) {

        uint16_t num_queues = num_workers;
#else
    void port_init(uint16_t num_workers, uint16_t num_ps) {

        uint16_t num_queues = num_workers + num_ps;
#endif
        int ret;

        uint16_t nb_ports, tmp_portid;
        bool found_portid = false;

        struct rte_mempool *pool;
        string pool_name;

        struct rte_eth_dev_info dev_info;
        struct rte_flow_error error;

        // Port configuration
        struct rte_eth_conf port_conf = { };
        struct rte_eth_rxmode rxm = { };
        struct rte_eth_txmode txm = { };
        struct rte_eth_rxconf rx_conf = { };
        struct rte_eth_txconf tx_conf = { };

        // Port ethernet address
        struct rte_ether_addr port_eth_addr;

        // Check number of ports
        nb_ports = rte_eth_dev_count_avail();
        if (nb_ports == 0)
            LOG_FATAL("No Ethernet ports");

        RTE_ETH_FOREACH_DEV(tmp_portid)
        {

            if (dpdk_par.portid == tmp_portid)
                found_portid = true;
        }

        if (!found_portid) {
            LOG_DEBUG("DPDK ports enabled: " + to_string(nb_ports));
            LOG_FATAL("Wrong port ID: " + to_string(dpdk_par.portid));
        }

        // Get port info
        rte_eth_dev_info_get(dpdk_par.portid, &dev_info);

#ifdef DEBUG
        print_dev_info(dev_info);
#endif

        // Initialize port
        LOG_DEBUG("Initializing port " + to_string(dpdk_par.portid) + "...");

        rxm.split_hdr_size = 0;
        //rxm.ignore_offload_bitfield = 1;

        if (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_IPV4_CKSUM) {
            rxm.offloads |= DEV_RX_OFFLOAD_IPV4_CKSUM;
            LOG_DEBUG("RX IPv4 checksum offload enabled");
        }

        if (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_UDP_CKSUM) {
            rxm.offloads |= DEV_RX_OFFLOAD_UDP_CKSUM;
            LOG_DEBUG("RX UDP checksum offload enabled");
        }

        /* Default in DPDK 18.11
        if (dev_info.rx_offload_capa & DEV_RX_OFFLOAD_CRC_STRIP) {
            rxm.offloads |= DEV_RX_OFFLOAD_CRC_STRIP;
            LOG_DEBUG("RX CRC stripped by the hw");
        }*/

        txm.mq_mode = ETH_MQ_TX_NONE;

        if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_IPV4_CKSUM) {
            txm.offloads |= DEV_TX_OFFLOAD_IPV4_CKSUM;
            LOG_DEBUG("TX IPv4 checksum offload enabled");
        }

        if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_UDP_CKSUM) {
            txm.offloads |= DEV_TX_OFFLOAD_UDP_CKSUM;
            LOG_DEBUG("TX UDP checksum offload enabled");
        }

        if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE) {
            txm.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;
            LOG_DEBUG("Fast release of mbufs enabled");
        }

        port_conf.rxmode = rxm;
        port_conf.txmode = txm;
        //port_conf.link_speeds = ETH_LINK_SPEED_AUTONEG;
        //port_conf.lpbk_mode = 0; // Loopback operation mode disabled
        //port_conf.dcb_capability_en = 0; // DCB disabled

        // Flow director
        port_conf.rx_adv_conf.rss_conf.rss_key = NULL;
        port_conf.rx_adv_conf.rss_conf.rss_hf = 0;

        port_conf.fdir_conf.mode = RTE_FDIR_MODE_PERFECT;
        port_conf.fdir_conf.pballoc = RTE_FDIR_PBALLOC_64K;
        port_conf.fdir_conf.status = RTE_FDIR_NO_REPORT_STATUS;
        port_conf.fdir_conf.drop_queue = 127;

        //memset(&port_conf.fdir_conf.mask, 0x00, sizeof(struct rte_eth_fdir_masks));
        //port_conf.fdir_conf.mask.dst_port_mask = 0xFFFF;

        if (rte_flow_isolate(dpdk_par.portid, 1, &error) < 0) {
            LOG_ERROR ("Flow isolated mode failed: " + to_string(error.type) + " " + string((error.message ? error.message : "(no stated reason)")));
        }

        ret = rte_eth_dev_configure(dpdk_par.portid, num_queues, num_queues, &port_conf);
        if (ret < 0)
            LOG_FATAL("Cannot configure port: " + string(rte_strerror(ret)));

#ifdef MLX
        // Fix for mlx5 driver ring size overflow
        dpdk_par.port_rx_ring_size = 4096;
        dpdk_par.port_tx_ring_size = 4096;
#else
        dpdk_par.port_rx_ring_size = dev_info.rx_desc_lim.nb_max < 1024 ? dev_info.rx_desc_lim.nb_max : 1024;
        dpdk_par.port_tx_ring_size = dev_info.tx_desc_lim.nb_max < 1024 ? dev_info.tx_desc_lim.nb_max : 1024;
#endif

        // Check that numbers of Rx and Tx descriptors satisfy descriptors
        // limits from the ethernet device information, otherwise adjust
        // them to boundaries.
        ret = rte_eth_dev_adjust_nb_rx_tx_desc(dpdk_par.portid, &dpdk_par.port_rx_ring_size, &dpdk_par.port_tx_ring_size);
        if (ret < 0)
            LOG_FATAL("Cannot adjust number of descriptors: " + string(rte_strerror(ret)));

        //Get the port address
        rte_eth_macaddr_get(dpdk_par.portid, &port_eth_addr);

        // init RX queue
        rx_conf = dev_info.default_rxconf;
        rx_conf.offloads = port_conf.rxmode.offloads;
        //rx_conf.rx_thresh.pthresh = 8;
        //rx_conf.rx_thresh.hthresh = 8;
        //rx_conf.rx_thresh.wthresh = 4;
        //rx_conf.rx_free_thresh = 64;
        //rx_conf.rx_drop_en = 0;

        // init TX queue on each port
        tx_conf = dev_info.default_txconf;
        //tx_conf.txq_flags = ETH_TXQ_FLAGS_IGNORE;
        tx_conf.offloads = port_conf.txmode.offloads;
        //tx_conf.tx_thresh.pthresh = 36;
        //tx_conf.tx_thresh.hthresh = 0;
        //tx_conf.tx_thresh.wthresh = 0;
        //tx_conf.tx_free_thresh = 0;
        //tx_conf.tx_rs_thresh = 0;

        for (uint32_t i=0; i<num_queues; i++){

            // Init the rx buffer pool
            pool_name = "rx_pool_" + to_string(i);
            pool = rte_pktmbuf_pool_create(pool_name.c_str(), dpdk_par.pool_size, dpdk_par.pool_cache_size, 0, dpdk_data.pool_buffer_size, rte_socket_id());
            if (pool == NULL)
                LOG_FATAL("Cannot init mbuf pool: " + string(rte_strerror(rte_errno)));

            // Init queues
            ret = rte_eth_rx_queue_setup(dpdk_par.portid, i, dpdk_par.port_rx_ring_size, rte_eth_dev_socket_id(dpdk_par.portid), &rx_conf, pool);
            if (ret < 0)
                LOG_FATAL("RX queue setup error: " + string(rte_strerror(ret)));

            ret = rte_eth_tx_queue_setup(dpdk_par.portid, i, dpdk_par.port_tx_ring_size, rte_eth_dev_socket_id(dpdk_par.portid), &tx_conf);
            if (ret < 0)
                LOG_FATAL("TX queue setup error: " + string(rte_strerror(ret)));

            // Stats mapping
            if (i<RTE_ETHDEV_QUEUE_STAT_CNTRS){

                ret = rte_eth_dev_set_rx_queue_stats_mapping(dpdk_par.portid, i, i);
                if (ret < 0)
                    LOG_ERROR("RX queue stats mapping error " + string(rte_strerror(ret)));

                ret = rte_eth_dev_set_tx_queue_stats_mapping(dpdk_par.portid, i, i);
                if (ret < 0)
                    LOG_ERROR("TX queue stats mapping error " + string(rte_strerror(ret)));
            } else {
                LOG_ERROR ("No stats mapping for queue: " + to_string(i));
            }
        }

        // Start device
        ret = rte_eth_dev_start(dpdk_par.portid);
        if (ret < 0)
            LOG_FATAL("Error starting the port: " + string(rte_strerror(ret)));

        // Enable promiscuous mode
        //rte_eth_promiscuous_enable(dpdk_par.portid);

        LOG_DEBUG("Initialization ended. Port " + to_string(dpdk_par.portid) + " address: " + mac_to_str(port_eth_addr));

        check_port_link_status(dpdk_par.portid);

        // Add FDIR filters
        for (uint16_t i = 0; i < num_queues; i++){

#ifndef COLOCATED
            insert_flow_rule(dpdk_par.portid, i, daiet_par.getBaseWorkerPort()+i);
#else
            if (i < num_workers)
                insert_flow_rule(dpdk_par.portid, i, daiet_par.getBaseWorkerPort()+i);
            else
                insert_flow_rule(dpdk_par.portid, i, daiet_par.getBasePsPort()+i-num_workers);
#endif
        }
    }

    int master(DaietContext* dctx_ptr) {

        try {

            int ret;

            uint64_t hz;
            ostringstream hz_str;
            clock_t begin_cpu;
            uint64_t begin;
            double elapsed_secs_cpu;
            double elapsed_secs;
            ostringstream elapsed_secs_str, elapsed_secs_cpu_str;

            uint16_t num_threads = 0, num_worker_threads = 0;
            string eal_cmdline;

            force_quit = false;

#ifdef COLOCATED
            uint16_t num_ps_threads;
#endif

            char hostname[500];
            if (gethostname(hostname,sizeof(hostname))!=0)
                strcpy(hostname, "NOHOSTNAME");
            daiet_log = std::ofstream(string("daiet-") + string(hostname) + string(".log"), std::ios::out);

            const char *buildString = "Compiled at " __DATE__ ", " __TIME__ ".";
            LOG_INFO (string(buildString));

            parse_parameters();

            // Set EAL log file
            FILE * dpdk_log_file;
            string dpdk_log_path = string("dpdk-") + string(hostname) + string(".log");
            dpdk_log_file = fopen(dpdk_log_path.c_str(), "w");
            if (dpdk_log_file == NULL) {
                LOG_ERROR("Failed to open log file: " + string(strerror(errno)));
            } else {
                ret = rte_openlog_stream(dpdk_log_file);
                if (ret < 0)
                    LOG_ERROR("Failed to open dpdk log stream");
            }

            // EAL cmd line
            eal_cmdline = "daiet -l " + dpdk_par.corestr + " --file-prefix " + dpdk_par.prefix + " " + dpdk_par.eal_options;
            vector<string> par_vec = split(eal_cmdline);

            int args_c = par_vec.size();
            char* args[args_c];
            char* args_ptr[args_c];

            for (vector<string>::size_type i = 0; i != par_vec.size(); i++) {
                args[i] = new char[par_vec[i].size() + 1];
                args_ptr[i] = args[i];
                strcpy(args[i], par_vec[i].c_str());
            }

/* mlockall has issues inside docker
#ifndef COLOCATED
            // This is causing some issue with the PS
            // Lock pages
            if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
                LOG_FATAL("mlockall() failed with error: " + string(strerror(rte_errno)));
            }
#endif
*/
            // EAL init
            ret = rte_eal_init(args_c, args);
            if (ret < 0)
                LOG_FATAL("EAL init failed: " + string(rte_strerror(rte_errno)));

            // Count cores/workers
            uint16_t lcore_id, tid = 0;
            for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
                if (rte_lcore_is_enabled(lcore_id) != 0) {

                    dpdk_data.core_to_thread_id[lcore_id] = tid;
                    tid++;
                    num_threads++;
                }
            }

            LOG_INFO("Number of threads: " + to_string(num_threads));

#ifndef COLOCATED
            num_worker_threads = num_threads;
#else
            if (num_threads%2 != 0)
                LOG_FATAL("Colocated mode needs an even number of cores!");

            num_ps_threads = num_threads/2;
            num_worker_threads = num_threads - num_ps_threads;
            LOG_INFO("Workers: " + to_string(num_worker_threads) + ", PS: " + to_string(num_ps_threads));
#endif

            // Estimate CPU frequency
            hz = rte_get_timer_hz();
            hz_str << setprecision(3) << (double) hz / 1000000000;
            LOG_INFO("CPU freq: " + hz_str.str() + " GHz");

            // Initialize port
#ifndef COLOCATED
            port_init(num_worker_threads);
#else
            port_init(num_worker_threads, num_ps_threads);
#endif

            // Initialize workers/PSs
            worker_setup();

#ifdef COLOCATED
            ps_setup();
#endif

            dctx_ptr->set_num_worker_threads(num_worker_threads);

#ifndef COLOCATED
            pkt_stats.init(num_worker_threads);
#else
            pkt_stats.init(num_worker_threads, num_ps_threads);
#endif

            // Check state of slave cores
            RTE_LCORE_FOREACH_SLAVE(lcore_id)
            {
                if (rte_eal_get_lcore_state(lcore_id) != WAIT)
                    LOG_FATAL("Core " + to_string(lcore_id) + " in state " + to_string(rte_eal_get_lcore_state(lcore_id)));
            }

            begin_cpu = clock();
            begin = rte_get_timer_cycles();

            // Launch functions on slave cores
            RTE_LCORE_FOREACH_SLAVE(lcore_id) {

#ifndef COLOCATED
                 rte_eal_remote_launch(worker, dctx_ptr, lcore_id);
#else
                 if (dpdk_data.core_to_thread_id[lcore_id] < num_worker_threads)
                     rte_eal_remote_launch(worker, dctx_ptr, lcore_id);
                 else
                     rte_eal_remote_launch(ps, &num_worker_threads, lcore_id);
#endif
            }

            // Launch function on master core
#ifndef COLOCATED
            worker(dctx_ptr);
#else
             if (dpdk_data.core_to_thread_id[rte_lcore_id()] < num_worker_threads)
                 worker(dctx_ptr);
             else
                 ps(&num_worker_threads);
#endif

            // Join slave threads
            RTE_LCORE_FOREACH_SLAVE(lcore_id) {

                ret = rte_eal_wait_lcore(lcore_id);
                if (unlikely(ret < 0)) {
                    LOG_DEBUG("Core " + to_string(lcore_id) + " returned " + to_string(ret));
                }
            }

            elapsed_secs = ((double) (rte_get_timer_cycles() - begin)) / hz;
            elapsed_secs_cpu = double(clock() - begin_cpu) / CLOCKS_PER_SEC;

            // Print stats
#ifdef DEBUG
            print_dev_stats(dpdk_par.portid);
            print_dev_xstats(dpdk_par.portid);
#endif
            pkt_stats.dump();

            elapsed_secs_str << fixed << setprecision(6) << elapsed_secs;
            elapsed_secs_cpu_str << fixed << setprecision(6) << elapsed_secs_cpu;

            LOG_INFO("Time elapsed: " + elapsed_secs_str.str() + " seconds (CPU time: " + elapsed_secs_cpu_str.str() + " seconds)");

            // Cleanup

            LOG_DEBUG("Closing port...");
            struct rte_flow_error error;
            if(rte_flow_flush(dpdk_par.portid, &error)!=0)
                LOG_ERROR("Flow flush failed!");
            rte_eth_dev_stop(dpdk_par.portid);
            rte_eth_dev_close(dpdk_par.portid);
            LOG_DEBUG("Port closed");

            worker_cleanup();
#ifdef COLOCATED
            ps_cleanup();
#endif

            /* EAL cleanup (deprecated)
            ret = rte_eal_cleanup();
            if (ret < 0)
                LOG_FATAL("EAL cleanup failed!");*/

            fclose(dpdk_log_file);
            daiet_log.close();

            for (vector<string>::size_type i = 0; i != par_vec.size(); i++) {
                delete[] args_ptr[i];
            }

            return 0;

        } catch (exception& e) {
            cerr << e.what() << endl;
            return -1;
        }
    }
} // End namespace
