/* Copyright (c) 2026 The Sugarchain En developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or https://opensource.org/license/MIT.
 * Offline fixture generation only; not compiled into node binaries.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include "yespower.h"
static const uint8_t personal[] =
    "Satoshi Nakamoto 31/Oct/2008 Proof-of-work is essentially one-CPU-one-vote";
_Static_assert(sizeof(personal) - 1 == 74, "Official personalization length");
static const yespower_params_t params = {
    .version = YESPOWER_1_0, .N = 2048, .r = 32,
    .pers = personal, .perslen = 74
};
uint64_t mine_header(const uint8_t *header, const uint8_t *target, unsigned start, unsigned step, _Atomic int *stop)
{
    uint8_t candidate[80];
    memcpy(candidate, header, 80);
    for (uint64_t nonce = start; nonce <= UINT32_MAX && !atomic_load(stop); nonce += step) {
        candidate[76] = nonce; candidate[77] = nonce >> 8;
        candidate[78] = nonce >> 16; candidate[79] = nonce >> 24;
        yespower_binary_t proof;
        if (yespower_tls(candidate, 80, &params, &proof)) return UINT64_MAX;
        int valid = 1;
        for (int i = 31; i >= 0; --i) {
            if (proof.uc[i] < target[i]) break;
            if (proof.uc[i] > target[i]) { valid = 0; break; }
        }
        if (valid) {
            int expected = 0;
            if (atomic_compare_exchange_strong(stop, &expected, 1)) return nonce + 1;
            return 0;
        }
    }
    return 0;
}
