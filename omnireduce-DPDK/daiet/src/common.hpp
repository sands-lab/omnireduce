/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#pragma once

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include <iostream>
#include <string>
#include <cstring>
#include <fstream>

#include "dpdk.h"
#include "msgs.h"

namespace daiet {

    extern volatile bool force_quit;
}
