/**
  * OmniReduce project
  * author: jiawei.fei@kaust.edu.sa
  */

#pragma once

#include "omnireduce/context.hpp"
#include "omnireduce/aggcontext.hpp"

namespace omnireduce {
    int master(OmniContext* dctx);
    int aggmaster(AggContext* dctx);
}
