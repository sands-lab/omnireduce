/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#ifdef COLOCATED
#pragma once

namespace daiet {

    void ps_setup();
    void ps_cleanup();
    int ps(void*);
}
#endif
