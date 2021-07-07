/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include "utils.hpp"
#include "params.hpp"
#include <boost/algorithm/string/split.hpp>

using namespace std;

namespace daiet {
    mutex info_mutex_, err_mutex_, debug_mutex_;
    std::ofstream daiet_log;

    template<typename T>
    void LOG_FATAL(T msg) {
        unique_lock<mutex> mlock(err_mutex_);
        daiet_log << msg << endl << flush;
        cerr << msg << endl << flush;
        exit(1);
    }
    template void LOG_FATAL<string>(string);

    template<typename T>
    void LOG_ERROR(T msg) {
        unique_lock<mutex> mlock(err_mutex_);
        daiet_log << msg << endl << flush;
    }
    template void LOG_ERROR<string>(string);

    template<typename T>
    void LOG_INFO(T msg) {
        unique_lock<mutex> mlock(info_mutex_);
        daiet_log << msg << endl << flush;
    }
    template void LOG_INFO<string>(string);
    template void LOG_INFO<char const*>(char const*);

#ifdef DEBUG
    template<typename T>
    void LOG_DEBUG(T msg) {
        unique_lock<mutex> mlock(debug_mutex_);
        daiet_log << msg << endl << flush;
    }
    template void LOG_DEBUG<string>(string);
#endif

    template<typename T>
    string to_hex(T i) {
        stringstream stream;
        stream << setfill('0') << setw(sizeof(T) * 2) << hex << (int) i;
        return stream.str();
    }
    template string to_hex<int>(int);

    vector<string> split(const string &str) {
        vector<string> elems;

        boost::split(elems, str, [](char c) {return c == ' ';}, boost::token_compress_on);

        return elems;
    }

    vector<string> split(const string &str, const string &delim) {
        vector<string> elems;

        boost::split(elems, str, [delim](char c) {
            return delim.find (c)!=string::npos;
        }, boost::token_compress_on);

        return elems;
    }

    string mac_to_str(const rte_ether_addr addr) {

        string mac = "";
        for (int i = 0; i < 5; i++) {
            mac += to_hex(addr.addr_bytes[i]) + ":";
        }

        return mac + to_hex(addr.addr_bytes[5]);
    }

    string mac_to_str(const uint64_t mac, bool change_endianess) {

        string mac_str = "", hex;

        if (change_endianess) {

            for (int i = 0; i < 5; i++) {
                hex = to_hex((mac & ((uint64_t) 0xFF << 8 * i)) >> 8 * i);
                mac_str += hex.substr(sizeof(uint64_t) * 2 - 2, 2) + ":";
            }

            hex = to_hex((mac & ((uint64_t) 0xFF << 40)) >> 40);
            mac_str += hex.substr(sizeof(uint64_t) * 2 - 2, 2);
        } else {

            hex = to_hex((mac & ((uint64_t) 0xFF << 40)) >> 40);
            mac_str += hex.substr(sizeof(uint64_t) * 2 - 2, 2);

            for (int i = 4; i >= 0; i++) {

                hex = to_hex((mac & ((uint64_t) 0xFF << 8 * i)) >> 8 * i);
                mac_str += ":" + hex.substr(sizeof(uint64_t) * 2 - 2, 2);
            }
        }

        return mac_str;
    }

    int64_t str_to_mac(string const& s, bool change_endianess) {

        uint8_t mac[6];
        uint last;
        int rc;

        if (change_endianess)
            rc = sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%n", mac + 5, mac + 4, mac + 3, mac + 2, mac + 1, mac, &last);
        else
            rc = sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx%n", mac, mac + 1, mac + 2, mac + 3, mac + 4, mac + 5, &last);

        if (rc != 6 || s.size() != last)
            return -1;
        else
            return uint64_t(mac[0]) << 40 | uint64_t(mac[1]) << 32 | uint64_t(mac[2]) << 24 | uint64_t(mac[3]) << 16 | uint64_t(mac[4]) << 8 | uint64_t(mac[5]);
    }

    string ip_to_str(uint32_t ip) {
        struct in_addr addr;
        addr.s_addr = ip;
        string ipstr(inet_ntoa(addr));
        return ipstr;
    }

    void swap_eth_addr(rte_ether_hdr *eth) {
        struct rte_ether_addr tmp_addr;

        rte_ether_addr_copy(&eth->s_addr, &tmp_addr);
        rte_ether_addr_copy(&eth->d_addr, &eth->s_addr);
        rte_ether_addr_copy(&tmp_addr, &eth->d_addr);
    }

    void deep_copy_single_segment_pkt(rte_mbuf* dst, const rte_mbuf* src) {

        dst->data_len = src->data_len;
        dst->pkt_len = src->pkt_len;

        rte_prefetch0(rte_pktmbuf_mtod(dst, void *));
        rte_memcpy(rte_pktmbuf_mtod(dst, void *), rte_pktmbuf_mtod(src, void *), src->data_len);
    }

    void check_port_link_status(uint16_t portid) {

        #define CHECK_INTERVAL 100 /* 100ms */
        #define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */

        struct rte_eth_link link;

        LOG_DEBUG("Checking link status");

        for (uint8_t count = 0; count <= MAX_CHECK_TIME; count++) {

            memset(&link, 0, sizeof(link));
            rte_eth_link_get_nowait(portid, &link);

            if (link.link_status) {
                LOG_INFO(
                        "Link Up. Speed " + to_string(link.link_speed) + " Mbps - "
                                + ((link.link_duplex == ETH_LINK_FULL_DUPLEX) ? ("Full-duplex") : ("Half-duplex")));
                return;
            } else{
                rte_delay_ms(CHECK_INTERVAL);
            }
        }

        LOG_FATAL("Link Down");
    }

    void print_packet(struct rte_ether_hdr *eth, uint16_t size) {

        int idx = sizeof(struct rte_ether_hdr);

        LOG_INFO("** Ethernet **");
        LOG_INFO("Src: " + mac_to_str(eth->s_addr));
        LOG_INFO("Dst: " + mac_to_str(eth->d_addr));
        uint16_t etherType = rte_be_to_cpu_16(eth->ether_type);
        LOG_INFO("EtherType: 0x" + to_hex(etherType));

        if (etherType == RTE_ETHER_TYPE_IPV4 && size >= idx + sizeof(struct rte_ipv4_hdr)) {

            idx += sizeof(struct rte_ipv4_hdr);
            struct rte_ipv4_hdr* ip = (struct rte_ipv4_hdr *) (eth + 1);
            LOG_INFO("** IPv4 **");
            LOG_INFO("Version: " + to_string((ip->version_ihl & 0xf0) >> 4));
            LOG_INFO("IHL: " + to_string(ip->version_ihl & 0x0f));
            LOG_INFO("TTL: " + to_string(ip->time_to_live));
            LOG_INFO("TotalLen: " + to_string(rte_be_to_cpu_16(ip->total_length)));
            LOG_INFO("Src: 0x" + to_hex(rte_be_to_cpu_32(ip->src_addr)));
            LOG_INFO("Dst: 0x" + to_hex(rte_be_to_cpu_32(ip->dst_addr)));
            LOG_INFO("Protocol: " + to_string(ip->next_proto_id));

            if (ip->next_proto_id == IPPROTO_UDP && size >= idx + sizeof(struct rte_udp_hdr)) {

                idx += sizeof(struct rte_udp_hdr);
                struct rte_udp_hdr* udp = (struct rte_udp_hdr *) (ip + 1);

                LOG_INFO("** UDP **");
                LOG_INFO("Src: " + to_string(rte_be_to_cpu_16(udp->src_port)));
                LOG_INFO("Dst: " + to_string(rte_be_to_cpu_16(udp->dst_port)));
                LOG_INFO("Len: " + to_string(rte_be_to_cpu_16(udp->dgram_len)));

                if (size >= idx + sizeof(struct daiet_hdr)) {

                    idx += sizeof(struct daiet_hdr);
                    struct daiet_hdr* daiet = (struct daiet_hdr *) (udp + 1);
                    LOG_INFO("** DAIET **");
                    LOG_INFO("TSI: " + to_string(daiet->tsi));
                    LOG_INFO("PoolIndex: " + to_string(rte_be_to_cpu_16(daiet->pool_index)));

                    struct entry_hdr* entry = (struct entry_hdr *) (daiet + 1);

                    for (uint32_t i = 0; i < daiet_par.getNumUpdates() && size >= idx + sizeof(struct entry_hdr); i++) {

                        idx += sizeof(struct entry_hdr);
                        LOG_INFO("Entry " + to_string(i) + ": " + to_string((int32_t) rte_be_to_cpu_32(entry->upd)));
                        entry++;
                    }
                }
            }
        }
        LOG_INFO("*******");
    }

    void print_dev_info(struct rte_eth_dev_info& dev_info) {

        string dev_driver(dev_info.driver_name);

        LOG_DEBUG("Driver: " + dev_driver);
        LOG_DEBUG("RX buffer min size: " + to_string(dev_info.min_rx_bufsize));
        LOG_DEBUG("RX queues max number: " + to_string(dev_info.max_rx_queues));
        LOG_DEBUG("TX queues max number: " + to_string(dev_info.max_tx_queues));
        LOG_DEBUG("Per-port RX offload capabilities: 0x" + to_hex(dev_info.rx_offload_capa));
        LOG_DEBUG("Per-port TX offload capabilities: 0x" + to_hex(dev_info.tx_offload_capa));
        LOG_DEBUG("Per-queue RX offload capabilities: 0x" + to_hex(dev_info.rx_queue_offload_capa));
        LOG_DEBUG("Per-queue TX offload capabilities: 0x" + to_hex(dev_info.tx_queue_offload_capa));
        LOG_DEBUG(
                "RX descriptors limits: [" + to_string(dev_info.rx_desc_lim.nb_min) + "," + to_string(dev_info.rx_desc_lim.nb_max) + "] aligned: "
                        + to_string(dev_info.rx_desc_lim.nb_align));
        LOG_DEBUG(
                "TX descriptors limits: [" + to_string(dev_info.tx_desc_lim.nb_min) + "," + to_string(dev_info.tx_desc_lim.nb_max) + "] aligned: "
                        + to_string(dev_info.tx_desc_lim.nb_align));
    }

    void print_dev_stats(uint16_t portid) {

        int ret;
        rte_eth_stats res;

        ret = rte_eth_stats_get(portid, &res);
        if (ret < 0)
            LOG_ERROR("Failed to get device stats");

        LOG_DEBUG("** Port " + to_string(portid) + " stats **");
        LOG_DEBUG("Received packets: " + to_string(res.ipackets) + " (" + to_string(res.ibytes) + " bytes)");
        LOG_DEBUG("Transmitted packets: " + to_string(res.opackets) + " (" + to_string(res.obytes) + " bytes)");
        LOG_DEBUG("Erroneous received packets: " + to_string(res.ierrors));
        LOG_DEBUG("Failed transmitted packets: " + to_string(res.oerrors));

        LOG_DEBUG("RX mbuf allocation failures: " + to_string(res.rx_nombuf));
        LOG_DEBUG("RX packets dropped by the HW: " + to_string(res.imissed));
        LOG_DEBUG("*******");
    }

    void print_dev_xstats(uint16_t portid) {

        struct rte_eth_xstat_name *xstats_names;
        uint64_t *values;
        int len, i;

        /* Get number of stats */
        len = rte_eth_xstats_get_names_by_id(portid, NULL, 0, NULL);
        if (len < 0) {
            LOG_FATAL("Cannot get xstats count");
        }

        xstats_names = new rte_eth_xstat_name[len];
        if (xstats_names == NULL) {
            LOG_FATAL("Cannot allocate memory for xstat names");
        }

        /* Retrieve xstats names, passing NULL for IDs to return all statistics */
        if (len != rte_eth_xstats_get_names_by_id(portid, xstats_names, len, NULL)) {
            LOG_FATAL("Cannot get xstat names");
        }

        values = new uint64_t[len];
        if (values == NULL) {
            LOG_FATAL("Cannot allocate memory for xstats");
        }

        /* Getting xstats values */
        if (len != rte_eth_xstats_get_by_id(portid, NULL, values, len)) {
            LOG_FATAL("Cannot get xstat values");
        }

        /* Print all xstats names and values */
        LOG_DEBUG("** Port " + to_string(portid) + " xstats **");
        for (i = 0; i < len; i++) {
            if (values[i] != 0) {
                string name(xstats_names[i].name);
                LOG_DEBUG(name + ": " + to_string(values[i]));
            }
        }
        LOG_DEBUG("*******");

        // Cleanup
        delete[] xstats_names;
        delete[] values;
    }
} // End namespace
