/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#pragma once

#include <vector>
#include <boost/thread.hpp>

#include "common.hpp"

using namespace std;

namespace daiet {

    class pkt_statistics {

        public:
            pkt_statistics();
            void set_workers(uint16_t, uint64_t, uint64_t, uint64_t);
            void dump();

#ifndef COLOCATED
            void init(uint32_t);
#else
            void init(uint32_t, uint32_t);
            void set_ps(uint32_t, uint64_t, uint64_t);
#endif

#ifdef TIMERS
            void set_timeouts(uint32_t, uint64_t);
#endif

        private:

            boost::mutex w_mutex;
            uint64_t total_w_tx;
            uint64_t total_w_rx;
            uint64_t total_w_unsent;
            vector<uint64_t> w_tx;
            vector<uint64_t> w_rx;
            vector<uint64_t> w_unsent;

#ifdef COLOCATED
            boost::mutex ps_mutex;
            uint64_t total_ps_tx;
            uint64_t total_ps_rx;
            vector<uint64_t>  ps_tx;
            vector<uint64_t>  ps_rx;
#endif

#ifdef TIMERS
            boost::mutex timeouts_mutex;
            vector<uint64_t> w_timeouts;
            uint64_t total_timeouts;
#endif
    };

    extern pkt_statistics pkt_stats;
}
