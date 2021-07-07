/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#pragma once

#include "common.hpp"

namespace daiet {

    void worker_setup();
    void worker_cleanup();
    int worker(void*);
}
