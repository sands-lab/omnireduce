#include <iostream>
#include <memory>
#include <chrono>
#include <cmath>

#include "gloo/allreduce_halving_doubling.h"
#include "gloo/rendezvous/context.h"
#include "gloo/rendezvous/redis_store.h"
#include "gloo/rendezvous/prefix_store.h"
#include "gloo/transport/tcp/device.h"
#include "gloo/barrier_all_to_one.h"

#include <signal.h>

#include "common.h"

using namespace std;

shared_ptr<gloo::rendezvous::Context> context;

void signal_handler(int signum) {

    if (signum == SIGINT || signum == SIGTERM) {

        cerr << " Signal " << signum << " received!";

#ifdef DAIET
        context->daietContext.StopMaster();
#endif
        exit(1);
    }
}

int main(int argc, char* argv[]) {

    if (argc != 8) {
        cout << " Usage: " << argv[0] << " INTERFACE REDIS_SERVER_IP PREFIX NUM_WORKERS RANK TENSOR_SIZE NUM_ROUNDS" << endl;
        return 0;
    }

    /* Set signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    vector<int32_t, aligned_allocator<int32_t, kBufferAlignment>> base_data;
    vector<int32_t, aligned_allocator<int32_t, kBufferAlignment>> data;
    int roundnum = 0;

    int32_t elem = 1, expected = 0;

    // GLOO transport
    gloo::transport::tcp::attr attr;
    attr.iface = argv[1];
    auto dev = gloo::transport::tcp::CreateDevice(attr);

    // Rendezvous
    auto redisStore = gloo::rendezvous::RedisStore(argv[2]);
    string prefix = argv[3];
    auto prefixStore = gloo::rendezvous::PrefixStore(prefix, redisStore);

    const int size = atoi(argv[4]);
    const int rank = atoi(argv[5]);
    const int tensor_size = atoi(argv[6]);
    const int num_rounds = atoi(argv[7]);
    int num_last_rounds = 0;

    // Init data
    base_data.reserve(tensor_size);
    data.resize(tensor_size);
    cout << "-- Tensor initialization" << endl;
    for (int i = 0; i < tensor_size; i++) {
        base_data.insert(base_data.begin() + i, (i%100)*elem);
    }
    copy(base_data.begin(), base_data.end(), data.begin());
    cout << "---- Ended" << endl;

    vector<int32_t*> ptrs;
    ptrs.push_back(&data[0]);

    int count = data.size();

    // Context
    context = make_shared<gloo::rendezvous::Context>(rank, size);
    context->connectFullMesh(prefixStore, dev);

    auto barrier = make_shared<gloo::BarrierAllToOne>(context);

    barrier->run();

    //Warm up rounds
    for (int i = 0; i < 10; i++) {
        auto allreduce = make_shared<gloo::AllreduceHalvingDoubling<int32_t>>(context, ptrs, count);
        allreduce->run();
    }
    copy(base_data.begin(), base_data.end(), data.begin());

    // Start rounds
    for (roundnum = 0; roundnum < num_rounds; roundnum++) {

        if (roundnum % 10 == 0) {
            copy(base_data.begin(), base_data.end(), data.begin());
            num_last_rounds = 0;
        }

        // Instantiate the collective algorithm
        auto allreduce = make_shared<gloo::AllreduceHalvingDoubling<int32_t>>(context, ptrs, count);

        cout << "-- Allreduce Round " << roundnum << endl;

        auto begin = chrono::high_resolution_clock::now();
        // Run the algorithm
        allreduce->run();

        auto end = chrono::high_resolution_clock::now();

        cout << "---- Ended" << endl << "#ms " << chrono::duration_cast<chrono::milliseconds>(end - begin).count() << endl;
        num_last_rounds++;

    }

    cout << "-- Final check" << endl;
    for (int i = 0; i < tensor_size; i++) {
        expected = (i%100) * elem * powf(size, num_last_rounds);
        if (data[i] != expected) {
            cout << "---- Failed: index: " << i << " -> received " << data[i] << " instead of " << expected << endl;
            break;
        }
    }
    cout << "---- Ended" << endl;

    return 0;
}
