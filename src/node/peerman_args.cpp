// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit.

#include <node/peerman_args.h>

#include <chainparams.h>
#include <common/args.h>
#include <net_processing.h>
#include <util/chaintype.h>

#include <algorithm>
#include <limits>

namespace node {

namespace {
#if (defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))) || \
    (defined(__APPLE__) && (defined(__aarch64__) || defined(__x86_64__)))
constexpr bool USE_RAM_BASED_IBD_REQUEST_LIMIT{true};
#else
constexpr bool USE_RAM_BASED_IBD_REQUEST_LIMIT{false};
#endif
constexpr uint64_t IBD_REQUEST_LIMIT_64_MIN_RAM{4096ULL << 20};
constexpr uint64_t IBD_REQUEST_LIMIT_128_MIN_RAM{8192ULL << 20};
} // namespace

void ApplyArgsManOptions(const ArgsManager& argsman, PeerManager::Options& options, ChainType chain, uint64_t total_ram)
{
    if (auto value{argsman.GetBoolArg("-txreconciliation")}) options.reconcile_txs = *value;

    if (auto value{argsman.GetIntArg("-blockreconstructionextratxn")}) {
        options.max_extra_txs = uint32_t((std::clamp<int64_t>(*value, 0, std::numeric_limits<uint32_t>::max())));
    }

    if (auto value{argsman.GetBoolArg("-capturemessages")}) options.capture_messages = *value;

    if (auto value{argsman.GetBoolArg("-blocksonly")}) options.ignore_incoming_txs = *value;

    if (auto value{argsman.GetBoolArg("-privatebroadcast")}) options.private_broadcast = *value;

    if (auto value{argsman.GetIntArg("-parpow")}) {
        options.header_pow_workers = std::clamp<int64_t>(*value, 1, MAX_HEADER_POW_WORKERS);
    }
    options.regtest_shared_ibd = chain == ChainType::REGTEST && HasTestOption(argsman, "sharedibd");
    if (auto value{argsman.GetIntArg("-maxibdblocksinflight")}) {
        options.ibd_block_request_limit = std::clamp<int64_t>(*value, DEFAULT_IBD_BLOCK_REQUEST_LIMIT, MAX_IBD_BLOCK_REQUEST_LIMIT);
    } else if (UseSharedIbdPolicy(chain, options.regtest_shared_ibd, USE_RAM_BASED_IBD_REQUEST_LIMIT)) {
        if (total_ram >= IBD_REQUEST_LIMIT_128_MIN_RAM) {
            options.ibd_block_request_limit = MAX_IBD_BLOCK_REQUEST_LIMIT;
        } else if (total_ram >= IBD_REQUEST_LIMIT_64_MIN_RAM) {
            options.ibd_block_request_limit = 64;
        }
    }
}

} // namespace node

