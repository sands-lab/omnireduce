#include <iostream>
#include <fstream>
#include <signal.h>
#include <chrono>
#include <thread>
#include <daiet/DaietContext.hpp>

using namespace daiet;
using namespace std;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        cout << " Signal " << signum << " received, preparing to exit...";
        exit(EXIT_SUCCESS);
    }
}

int main() {

    DaietContext& ctx = DaietContext::getInstance();

    long int count = 1024 * 1024 * 200;
    int num_rounds = 10;
    int num_workers = 2;
    float base_value = 1.2; // must be [1,2[
    double accuracy=0.0001;
    int min_exp = -126 + log2(num_workers * count * num_rounds);
    int max_exp = 127 - log2(num_workers * count * num_rounds);
    int exp = min_exp;

    int faulty = 0, neg = 1, base_value_int = (base_value-1)*10;
    double avg_err = 0, err = 0;

    int32_t* p = new int32_t[count];
    int32_t expected_int;

    float* fp = new float[count];
    float expected_float;

    /* Set signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    for (int jj = 1; jj <= num_rounds; jj++) {

        std::cout << "INT round " << jj << std::endl;

        faulty = 0;
        neg = 1;

        for (int i = 0; i < count; i++) {
            p[i] = neg * base_value_int * jj * i;

            neg = -neg;
        }

        auto begin = std::chrono::high_resolution_clock::now();
        if (!ctx.try_daiet(p, count,1)){
            cout << "Daiet failed";
            exit(EXIT_FAILURE);
        }
        auto end = std::chrono::high_resolution_clock::now();

        neg = 1;
        for (int i = 0; i < count; i++) {

            expected_int = neg * base_value_int * jj * i * num_workers;

            if (p[i] != expected_int) {
                faulty++;
                std::cerr << "Index: " << i
                          << " Received: " << p[i]
                          << " Expected: " << expected_int << std::endl;
            }

            neg = -neg;
        }

        std::cout << "Done INT round " << jj
                  << ": Faulty: " << faulty
                  << " Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count()
                  << " ms" << std::endl;
    }

    for (int jj = 1; jj <= num_rounds; jj++) {

        std::cout << "FLOAT round " << jj << std::endl;

        faulty = 0;
        neg = 1;

        for (int i = 0; i < count; i++) {

            fp[i] = neg * ldexpf(base_value,exp) * jj * i;

            neg = -neg;
            exp++;

            if (exp > max_exp)
                exp=min_exp;
        }

        auto begin = std::chrono::high_resolution_clock::now();
        if (!ctx.try_daiet(fp, count,1)){
            cout << "Daiet failed";
            exit(EXIT_FAILURE);
        }

        auto end = std::chrono::high_resolution_clock::now();

        neg = 1;

        for (int i = 0; i < count; i++) {

            expected_float = neg * ldexpf(base_value,exp) * jj * i * num_workers;

            err = abs(expected_float - fp[i]) / abs(expected_float);

            if (err > accuracy){

                faulty++;
                avg_err += err;

                std::cerr << "Index: " << i
                          << " Received: " << fp[i]
                          << " Expected: " << expected_float
                          << " Error: " << err*100<<"%"<<std::endl;
            }

            neg = -neg;
            exp++;

            if (exp > max_exp)
                exp=min_exp;
        }

        avg_err = avg_err * 100 / count;

        std::cout << "Done FLOAT round " << jj
                  << ": Faulty: " << faulty
                  << " AVG err: "<< avg_err
                  <<"% Time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count()
                  << " ms" << std::endl;
    }

    exit(EXIT_SUCCESS);
}
