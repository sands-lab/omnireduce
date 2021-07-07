/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include "stats.hpp"
#include "utils.hpp"

namespace daiet {

    pkt_statistics pkt_stats;

    pkt_statistics::pkt_statistics() : total_w_tx(0), total_w_rx(0), total_w_unsent(0) {
#ifdef COLOCATED
        total_ps_tx = 0;
        total_ps_rx = 0;
#endif
    }

#ifndef COLOCATED
    void pkt_statistics::init(uint32_t nb_w) {
#else
    void pkt_statistics::init(uint32_t nb_w, uint32_t nb_ps) {
#endif

        total_w_tx = 0;
        total_w_rx = 0;
        total_w_unsent = 0;

        w_tx.resize(nb_w);
        w_rx.resize(nb_w);
        w_unsent.resize(nb_w);

#ifdef COLOCATED
        total_ps_tx = 0;
        total_ps_rx = 0;

        ps_tx.resize(nb_ps);
        ps_rx.resize(nb_ps);
#endif

#ifdef TIMERS
        w_timeouts.resize(nb_w);
#endif
    }

    void pkt_statistics::set_workers(uint16_t wid, uint64_t tx, uint64_t rx, uint64_t unsent) {

        boost::unique_lock<boost::mutex> lock(w_mutex);

        w_tx[wid] = tx;
        w_rx[wid] = rx;
        w_unsent[wid] = unsent;

        total_w_tx += tx;
        total_w_rx += rx;
        total_w_unsent += unsent;
    }

#ifdef COLOCATED
    void pkt_statistics::set_ps(uint32_t psid, uint64_t tx, uint64_t rx) {

        boost::unique_lock<boost::mutex> lock(ps_mutex);

        ps_tx[psid] = tx;
        ps_rx[psid] = rx;

        total_ps_tx += tx;
        total_ps_rx += rx;
    }
#endif

#ifdef TIMERS
    void pkt_statistics::set_timeouts(uint32_t wid, uint64_t timeouts) {

        boost::unique_lock<boost::mutex> lock(timeouts_mutex);

        w_timeouts[wid] = timeouts;

        total_timeouts += timeouts;
    }
#endif

    void pkt_statistics::dump(){

#ifndef COLOCATED
            LOG_INFO("TX " + to_string(total_w_tx));
            LOG_INFO("RX " + to_string(total_w_rx));
            LOG_INFO("UNSENT " + to_string(total_w_unsent));
#else
            LOG_INFO("Worker TX " + to_string(total_w_tx));
            LOG_INFO("Worker RX " + to_string(total_w_rx));
            LOG_INFO("Worker UNSENT " + to_string(total_w_unsent));
            LOG_INFO("PS TX " + to_string(total_ps_tx));
            LOG_INFO("PS RX " + to_string(total_ps_rx));
#endif

#ifdef TIMERS
            LOG_INFO("Timeouts " + to_string(total_timeouts));
#endif

            for (uint32_t i = 0; i < w_tx.size(); i++) {

                LOG_INFO("## Worker " + to_string(i));
                LOG_INFO("TX " + to_string(w_tx[i]));
                LOG_INFO("RX " + to_string(w_rx[i]));
                LOG_INFO("UNSENT " + to_string(w_unsent[i]));
#ifdef TIMERS
                LOG_INFO("Timeouts " + to_string(w_timeouts[i]));
#endif
            }

#ifdef COLOCATED
            for (uint32_t i = 0; i < ps_tx.size(); i++) {

                LOG_INFO("## PS" + to_string(i));
                LOG_INFO("TX " + to_string(ps_tx[i]));
                LOG_INFO("RX " + to_string(ps_rx[i]));
            }
#endif
    }
}
