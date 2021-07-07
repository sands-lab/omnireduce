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
        uint16_t worker_port, ps_port;
        uint32_t num_updates;
        string worker_ip_str, ps_ips_str, ps_macs_str;

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
                ("daiet.worker_ip", po::value<string>(&worker_ip_str)->default_value("10.0.0.1"), "IP address of this worker")
                ("daiet.worker_port", po::value<uint16_t>(&worker_port)->default_value(4000), "Worker UDP port")
                ("daiet.ps_port", po::value<uint16_t>(&ps_port)->default_value(48879), "PS UDP port")
                ("daiet.ps_ips", po::value<string>(&ps_ips_str)->required(), "Comma-separated list of PS IP addresses")
                ("daiet.ps_macs", po::value<string>(&ps_macs_str)->required(), "Comma-separated list of PS MAC addresses")
                ("daiet.max_num_pending_messages", po::value<uint32_t>(&(daiet_par.getMaxNumPendingMessages()))->default_value(256), "Max number of pending, unaggregated messages")
                ("daiet.num_updates", po::value<uint32_t>(&num_updates)->default_value(32), "Number of updates per packet")
                ("daiet.num_workers", po::value<uint16_t>(&(daiet_par.getNumWorkers()))->default_value(0), "Number of workers")
                ("daiet.sync_blocks", po::value<uint32_t>(&(daiet_par.getSyncBlocks()))->default_value(10), "Synchronization Blocks ")
#ifdef TIMERS
                ("daiet.timeout", po::value<double>(&(daiet_par.getTimeout()))->default_value(1), "Timeout in millisecond")
#endif
                ;

        config_file_options.add(daiet_options).add(dpdk_options);

        config_file = "/etc/daiet.cfg";
        ifs.open(config_file.c_str());
        if(!ifs.good()){
            ifs.close();

            char hostname[500];
            if (gethostname(hostname,sizeof(hostname))!=0)
                LOG_FATAL("gethostname failed: "+ string(strerror(errno)));

            config_file = "daiet-"+string(hostname)+".cfg";
            ifs.open(config_file.c_str());
            if(!ifs.good()){
                ifs.close();

                config_file = "daiet.cfg";
                ifs.open(config_file.c_str());
                if(!ifs.good()){
                    ifs.close();
                    LOG_FATAL("No config file found! (/etc/daiet.cfg, daiet-"+string(hostname)+".cfg, daiet.cfg)");
                }
            }
        }
        LOG_INFO("Configuration file "+config_file);

        po::variables_map vm;
        po::store(po::parse_config_file(ifs, config_file_options), vm);
        po::notify(vm);

        if (!daiet_par.setWorkerIp(worker_ip_str))
            LOG_FATAL("Invalid worker IP: " + worker_ip_str);

        daiet_par.setBaseWorkerPort(worker_port);
        daiet_par.setBasePsPort(ps_port);

        if (!daiet_par.setPs(ps_ips_str, ps_macs_str))
            LOG_FATAL("Invalid PS address: \n" + ps_ips_str + "\n" + ps_macs_str);

        daiet_par.setNumUpdates(num_updates);

        if (daiet_par.getNumWorkers()<=0)
            LOG_FATAL("Number of workers must be positive.");
        daiet_par.print_params();
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

        worker_port = 4000;
        ps_port = 48879;
        worker_ip_be = rte_cpu_to_be_32(0x0a000001);

        ps_ips_be = NULL;

        ps_macs_be = NULL;

        num_ps = 0;

        num_workers = 0;
    }

    daiet_params::~daiet_params() {
        if (ps_ips_be != NULL)
            delete[] ps_ips_be;
        if (ps_macs_be != NULL)
            delete[] ps_macs_be;
    }

    void daiet_params::print_params() {

        LOG_INFO("** DAIET parameters **");
        LOG_INFO("Num updates: " + to_string(num_updates));
        LOG_INFO("Max num pending messages: " + to_string(max_num_pending_messages));
        LOG_INFO("Worker port: " + to_string(worker_port));
        LOG_INFO("PS port: " + to_string(ps_port));

        LOG_INFO("Worker IP: " + ip_to_str(worker_ip_be));

        for (uint32_t i = 0; i < num_ps; i++) {

            LOG_INFO("PS" + to_string(i) + ": " + mac_to_str(ps_macs_be[i]) + " " + ip_to_str(ps_ips_be[i]));
        }

        LOG_INFO("Num workers: " + to_string(num_workers));
    }

    uint16_t& daiet_params::getNumWorkers() {
        return num_workers;
    }

    uint32_t& daiet_params::getSyncBlocks() {
        return sync_blocks;
    }

    void daiet_params::setNumUpdates(uint32_t numUpdates) {
        num_updates = numUpdates;
    }

    void daiet_params::setBaseWorkerPort(uint16_t workerPort) {
        worker_port = workerPort;
    }

    void daiet_params::setBasePsPort(uint16_t psPort) {
        ps_port = psPort;

    }

    /*
     * Returns false if the IP is invalid
     */
    bool daiet_params::setWorkerIp(string workerIp) {

        struct in_addr addr;

        if (inet_aton(workerIp.c_str(), &addr) == 0)
            return false;

        worker_ip_be = addr.s_addr;
        return true;
    }

    bool daiet_params::setPs(string psIps, string psMacs) {

        int64_t rc;

        vector<string> ips = split(psIps, ", ");
        vector<string> macs = split(psMacs, ", ");

        num_ps = ips.size() < macs.size() ? ips.size() : macs.size();

        if (ps_ips_be != NULL)
            delete[] ps_ips_be;
        if (ps_macs_be != NULL)
            delete[] ps_macs_be;

        ps_ips_be = new uint32_t[num_ps];
        ps_macs_be = new uint64_t[num_ps];

        struct in_addr addr;

        for (uint32_t i = 0; i < num_ps; i++) {

            if (inet_aton(ips[i].c_str(), &addr) == 0)
                return false;

            ps_ips_be[i] = addr.s_addr;

            rc = str_to_mac(macs[i]);
            if (rc < 0)
                return false;

            ps_macs_be[i] = rc;
        }

        return true;
    }
}