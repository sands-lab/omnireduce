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
            void dump();

            void init(uint32_t);
            void set_ps(uint32_t, uint64_t, uint64_t);

        private:

            boost::mutex ps_mutex;
            uint64_t total_ps_tx;
            uint64_t total_ps_rx;
            vector<uint64_t>  ps_tx;
            vector<uint64_t>  ps_rx;
    };

    extern pkt_statistics pkt_stats;
}
