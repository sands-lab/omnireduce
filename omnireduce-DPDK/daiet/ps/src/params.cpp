/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#include "params.hpp"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace daiet {

    struct dpdk_data dpdk_data;
    struct dpdk_params dpdk_par;
    daiet_params daiet_par;

    void parse_parameters() {

        string config_file;
        ifstream ifs;
        uint16_t ps_port;
        uint32_t num_updates;

        po::options_description dpdk_options("DPDK options");
        po::options_description daiet_options("DAIET options");
        po::options_description config_file_options;

        dpdk_options.add_options()
                ("dpdk.cores", po::value<string>(&dpdk_par.corestr)->default_value("0-2"), "List of cores")
                ("dpdk.prefix", po::value<string>(&dpdk_par.prefix)->default_value("daiet"), "Process prefix")
                ("dpdk.extra_eal_options", po::value<string>(&dpdk_par.eal_options)->default_value(""), "Extra EAL options")
                ("dpdk.port_id", po::value<uint16_t>(&dpdk_par.portid)->default_value(0), "Port ID")
                ("dpdk.pool_size", po::value<uint32_t>(&dpdk_par.pool_size)->default_value(8192 * 32), "Pool size")
                ("dpdk.pool_cache_size", po::value<uint32_t>(&dpdk_par.pool_cache_size)->default_value(256 * 2), "Pool cache size")
                ("dpdk.burst_rx", po::value<uint32_t>(&dpdk_par.burst_rx)->default_value(64), "RX burst size")
                ("dpdk.burst_tx", po::value<uint32_t>(&dpdk_par.burst_tx)->default_value(64), "TX burst size")
                ("dpdk.bulk_drain_tx_us", po::value<uint32_t>(&dpdk_par.bulk_drain_tx_us)->default_value(100), "TX bulk drain timer (us)");

        daiet_options.add_options()
                ("daiet.ps_port", po::value<uint16_t>(&ps_port)->default_value(48879), "PS UDP port")
                ("daiet.max_num_pending_messages", po::value<uint32_t>(&(daiet_par.getMaxNumPendingMessages()))->default_value(256), "Max number of pending, unaggregated messages")
                ("daiet.num_updates", po::value<uint32_t>(&num_updates)->default_value(32), "Number of updates per packet")
                ("daiet.num_workers", po::value<uint16_t>(&(daiet_par.getNumWorkers()))->default_value(0), "Number of workers");

        config_file_options.add(daiet_options).add(dpdk_options);

        config_file = "/etc/ps.cfg";
        ifs.open(config_file.c_str());
        if(!ifs.good()){
            ifs.close();

            char hostname[500];
            if (gethostname(hostname,sizeof(hostname))!=0)
                LOG_FATAL("gethostname failed: "+ string(strerror(errno)));

            config_file = "ps-"+string(hostname)+".cfg";
            ifs.open(config_file.c_str());
            if(!ifs.good()){
                ifs.close();

                config_file = "ps.cfg";
                ifs.open(config_file.c_str());
                if(!ifs.good()){
                    ifs.close();
                    LOG_FATAL("No config file found! (/etc/ps.cfg, ps-"+string(hostname)+".cfg, ps.cfg)");
                }
            }
        }
        LOG_INFO("Configuration file "+config_file);

        po::variables_map vm;
        po::store(po::parse_config_file(ifs, config_file_options), vm);
        po::notify(vm);

        daiet_par.setBasePsPort(ps_port);
        daiet_par.setNumUpdates(num_updates);

        if (daiet_par.getNumWorkers()<=0)
            LOG_FATAL("Number of workers must be greater than 0.");
    }

    void print_dpdk_params() {

        LOG_INFO("** DPDK parameters **");
        LOG_INFO("Cores: " + dpdk_par.corestr);
        LOG_INFO("Port ID: " + to_string(dpdk_par.portid));
        LOG_INFO("Port RX ring size: " + to_string(dpdk_par.port_rx_ring_size));
        LOG_INFO("Port TX ring size: " + to_string(dpdk_par.port_tx_ring_size));
        LOG_INFO("Pool size: " + to_string(dpdk_par.pool_size));
        LOG_INFO("Pool cache size: " + to_string(dpdk_par.pool_cache_size));
        LOG_INFO("Burst size RX: " + to_string(dpdk_par.burst_rx));
        LOG_INFO("Burst size TX: " + to_string(dpdk_par.burst_tx));
        LOG_INFO("Burst drain TX us: " + to_string(dpdk_par.bulk_drain_tx_us));
        LOG_INFO("Prefix: " + dpdk_par.prefix);
        LOG_INFO("Extra EAL options: " + dpdk_par.eal_options);
    }

    daiet_params::daiet_params() {

        // Defaults
        num_updates = 32;

        max_num_pending_messages = 256;

        tx_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4 | PKT_TX_UDP_CKSUM;

        ps_port = 5000;

        num_workers = 0;
    }

    daiet_params::~daiet_params() {
    }

    void daiet_params::print_params() {

        LOG_INFO("** DAIET parameters **");
        LOG_INFO("Num updates: " + to_string(num_updates));
        LOG_INFO("Max num pending messages: " + to_string(max_num_pending_messages));
        LOG_INFO("PS port: " + to_string(ps_port));
        LOG_INFO("Num workers: " + to_string(num_workers));
    }

    uint16_t& daiet_params::getNumWorkers() {
        return num_workers;
    }

    void daiet_params::setNumUpdates(uint32_t numUpdates) {
        num_updates = numUpdates;
    }

    void daiet_params::setBasePsPort(uint16_t psPort) {
        ps_port = psPort;

    }
}
