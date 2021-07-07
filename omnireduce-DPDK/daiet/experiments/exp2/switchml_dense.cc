#include <iostream>
#include <memory>
#include <chrono>
#include <cmath>
#include <numeric>
#include <algorithm>

#include "gloo/allreduce_halving_doubling.h"
#include "gloo/rendezvous/context.h"
#include "gloo/rendezvous/redis_store.h"
#include "gloo/rendezvous/prefix_store.h"
#include "gloo/transport/tcp/device.h"
#include "gloo/barrier_all_to_one.h"

#include <signal.h>
#include <assert.h>

#include "mpi.h"
#include "common.h"

using namespace std;

//#define SAVE_RESULT
#define OUTPUT_RANK 0
#define INTTYPE

#ifdef INTTYPE
typedef int ValType;
#else
typedef float ValType;
#endif

void set_seed(unsigned int seed) {
  srand(seed);
  srand48(seed);
}

void set_seed_random(int id) {
  set_seed(clock() + (id * 147));
}

// Between 0 (included) and max (excluded)
unsigned int get_random_int(unsigned int max) {
  return rand()%max;
}

// Between 0 (included) and max(excluded)
float get_random_float(unsigned int max) {
  return drand48()*max;
}

ValType get_random_value() {
#ifdef FLOATTYPE
  return get_random_float(100) - 50;
#elif defined(INTTYPE)
  return get_random_int(200) - 100; // Change to int if this changes
#endif
}

void create_sparse(const unsigned dim, const float density, ValType* v, const int blocksize) {
  // Create indices from 0 to dim 
  
  int block_num = (int)(dim/blocksize);
  int count = (int)(density*block_num);
  std::vector<unsigned int> indices(block_num);
  std::iota (indices.begin(), indices.end(), 0);

  // Random suffel indices
  std::random_shuffle ( indices.begin(), indices.end() );
  // Sort first count items
  std::sort( indices.begin(), indices.begin() + count);

  size_t idx = 0;
  for(std::vector<unsigned int>::const_iterator index = indices.begin(); index != indices.end() && index < indices.begin() + count; ++index) {
    for(int i=(*index)*blocksize;i<(*index+1)*blocksize; i++){
      ValType val = get_random_value();
      v[i]= val;
    }
  }
  return;
}

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


    MPI_Init(&argc, &argv);
    if (argc != 9) {
        cout << " Usage: " << argv[0] << " INTERFACE REDIS_SERVER_IP PREFIX NUM_WORKERS RANK TENSOR_SIZE NUM_ROUNDS DENSITY" << endl;
        return 0;
    }

    int myrank, worldsize;
    MPI_Comm_size(MPI_COMM_WORLD, &worldsize);
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

    /* Set signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    vector<ValType, aligned_allocator<ValType, kBufferAlignment>> base_data;
    vector<ValType, aligned_allocator<ValType, kBufferAlignment>> data;
    vector<ValType, aligned_allocator<ValType, kBufferAlignment>> results;


    int roundnum = 0;

    float elem = 0.01, expected = 0;

    // GLOO transport
    gloo::transport::tcp::attr attr;
    attr.iface = argv[1];
    auto dev = gloo::transport::tcp::CreateDevice(attr);

    // Rendezvous
    auto redisStore = gloo::rendezvous::RedisStore(argv[2]);
    time_t t = time(0);
    char ch[64];
    strftime(ch, sizeof(ch), "%Y-%m-%d-%H-%M-%S", localtime(&t));
    string prefix = ch;
    auto prefixStore = gloo::rendezvous::PrefixStore(prefix, redisStore);

    const int size = worldsize;
    const int rank = myrank;
    const int tensor_size = atoi(argv[6]);
    const int num_rounds = atoi(argv[7]);
    const float density = atof(argv[8]);
    const int blocksize = 256;
    int num_last_rounds = 0;
    int* timecost = (int*)malloc(sizeof(int)*num_rounds);

    // Init data
    set_seed_random(rank);
    srand(time(NULL));
    int cnt = (int)(density*tensor_size);
    base_data.resize(tensor_size);
    data.resize(tensor_size);
    results.resize(tensor_size);
    cout << "-- Tensor initialization" << endl;
    create_sparse(tensor_size, density, &base_data[0], blocksize);
    copy(base_data.begin(), base_data.end(), data.begin());
    copy(base_data.begin(), base_data.end(), results.begin());
    cout << "---- Ended" << endl;

    vector<ValType*> ptrs;
    ptrs.push_back(&data[0]);

    ValType* result_ptr = &results[0];
    

    int count = data.size();

    // Context
    context = make_shared<gloo::rendezvous::Context>(rank, size);
    context->connectFullMesh(prefixStore, dev);

    auto barrier = make_shared<gloo::BarrierAllToOne>(context);

    barrier->run();

    //Warm up rounds
    for (int i = 0; i < 10; i++) {
        auto allreduce = make_shared<gloo::AllreduceHalvingDoubling<ValType>>(context, ptrs, count);
        allreduce->run();
    }
    copy(base_data.begin(), base_data.end(), data.begin());
    copy(base_data.begin(), base_data.end(), results.begin());

    // Start rounds
    for (roundnum = 0; roundnum < num_rounds; roundnum++) {
        MPI_Barrier(MPI_COMM_WORLD);
        double t_mpi, maxT;
        if (roundnum % 5 == 0) {
            copy(base_data.begin(), base_data.end(), data.begin());
            copy(base_data.begin(), base_data.end(), results.begin());
            num_last_rounds = 0;
        }

        MPI_Allreduce(MPI_IN_PLACE, result_ptr, count, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

        // Instantiate the collective algorithm
        
        auto allreduce = make_shared<gloo::AllreduceHalvingDoubling<ValType>>(context, ptrs, count);

        //cout << "-- Allreduce Round " << roundnum << endl;

        auto begin = chrono::high_resolution_clock::now();
        // Run the algorithm
        t_mpi = -MPI_Wtime();
        allreduce->run();
        t_mpi += MPI_Wtime();
        MPI_Reduce(&t_mpi, &maxT, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);  
        if (myrank==OUTPUT_RANK) {
            printf("switchml Dense Allreduce: %f secs\n", maxT);
        }

        auto end = chrono::high_resolution_clock::now();

        //cout << "---- Ended" << endl << "#ms " << chrono::duration_cast<chrono::milliseconds>(end - begin).count() << endl;
        num_last_rounds++;
        timecost[roundnum] = (int)(maxT*1000000);
        MPI_Barrier(MPI_COMM_WORLD);
    }

    cout << "-- Final check" << endl;
    for (int i = 0; i < tensor_size; i++) {
        //if (i<100) cout<<data[i]<<" "<<results[i]<<endl;
        expected = results[i];
        if (data[i] != expected) {
            cout << "---- Failed: index: " << i << " -> received " << data[i] << " instead of " << expected << endl;
            break;
        }
    }
    cout << "---- Ended" << endl;
#ifdef SAVE_RESULT
    if(rank==OUTPUT_RANK){
        FILE *fp = NULL;
        char* filename = (char*)malloc(200*sizeof(char));
        strcpy(filename, "result/switchML_Dense");
        strcat(filename, "-");
        char temp[20];
        sprintf(temp, "%d", tensor_size);
        strcat(filename, temp);
        strcat(filename, "-");
        char temp2[20];
        sprintf(temp2, "%f", density);
        strcat(filename, temp2);
        strcat(filename, ".txt");
        fp = fopen(filename, "w+");
        fprintf(fp, "%s", "switchML_Dense\n");
        for(int j=0; j<num_rounds-1; j++){
            fprintf(fp, "%d", timecost[j]);
            fprintf(fp, "%s",",");
        }
        fprintf(fp, "%d", timecost[num_rounds-1]);
        fprintf(fp, "%s","\n");
        fclose(fp);
    }    
#endif
    MPI_Finalize();
    return 0;
}
