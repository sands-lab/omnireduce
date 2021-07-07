/**
 * DAIET project
 * author: amedeo.sapio@kaust.edu.sa
 */

#pragma once

namespace daiet {

#ifdef __cplusplus
    extern "C" {
#endif

        /**
         * DAIET Header
         */
        struct daiet_hdr {
                uint32_t tsi; /**< tensor start index */
                uint16_t pool_index; /**< pool index */
                uint32_t next_tsi; /**< next tensor start index */
#ifdef NOSCALING
                uint8_t data_type;
#endif
        }__attribute__((__packed__));

        struct entry_hdr {
                int32_t upd; /**< vector entry */
        }__attribute__((__packed__));
#ifndef NOSCALING
        struct exp_hdr {
                int16_t exp; /**< exponent */
        }__attribute__((__packed__));
#endif
#ifdef __cplusplus
    }
#endif

}  // End namespace