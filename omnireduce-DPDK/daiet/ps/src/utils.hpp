/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#pragma once

#include <sys/queue.h>

#include <sstream>
#include <utility>
#include <mutex>
#include <vector>
#include <iomanip>
#include <algorithm>

#include "common.hpp"

using namespace std;

namespace daiet {

    extern std::ofstream daiet_log;

    template<typename T>
    void LOG_FATAL(T) __attribute__((used));
    template<typename T>
    void LOG_ERROR(T) __attribute__((used));
    template<typename T>
    void LOG_INFO(T) __attribute__((used));

#ifdef DEBUG
    template<typename T>
    void LOG_DEBUG(T) __attribute__((used));
#else
#define LOG_DEBUG(T)
#endif

    template<typename T>
    string to_hex(T);

    vector<string> split(const string &);
    vector<string> split(const string &, const string &);

    string mac_to_str(const rte_ether_addr);
    string mac_to_str(const uint64_t, bool = true);
    int64_t str_to_mac(string const&, bool = true);
    string ip_to_str(uint32_t);

    void swap_eth_addr(rte_ether_hdr *);
    void deep_copy_single_segment_pkt(rte_mbuf*, const rte_mbuf*);
    void check_port_link_status(uint16_t);
    void print_packet(struct rte_ether_hdr *, uint16_t);
    void print_dev_info(struct rte_eth_dev_info&);
    void print_dev_stats(uint16_t);
    void print_dev_xstats(uint16_t);

} // End namespace
