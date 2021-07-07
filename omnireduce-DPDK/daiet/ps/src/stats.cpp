/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include "stats.hpp"
#include "utils.hpp"

namespace daiet {

    pkt_statistics pkt_stats;

    pkt_statistics::pkt_statistics() : total_ps_tx(0), total_ps_rx(0) {
    }

    void pkt_statistics::init(uint32_t nb_ps) {

        total_ps_tx = 0;
        total_ps_rx = 0;

        ps_tx.resize(nb_ps);
        ps_rx.resize(nb_ps);
    }

    void pkt_statistics::set_ps(uint32_t psid, uint64_t tx, uint64_t rx) {

        boost::unique_lock<boost::mutex> lock(ps_mutex);

        ps_tx[psid] = tx;
        ps_rx[psid] = rx;

        total_ps_tx += tx;
        total_ps_rx += rx;
    }

    void pkt_statistics::dump(){

            LOG_INFO("PS TX " + to_string(total_ps_tx));
            LOG_INFO("PS RX " + to_string(total_ps_rx));

            for (uint32_t i = 0; i < ps_tx.size(); i++) {

                LOG_INFO("## PS" + to_string(i));
                LOG_INFO("TX " + to_string(ps_tx[i]));
                LOG_INFO("RX " + to_string(ps_rx[i]));
            }
    }
}
