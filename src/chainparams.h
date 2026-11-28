// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <kernel/chainparams.h> // IWYU pragma: export

#include <memory>

class ArgsManager;

namespace node {
/** Apply a native initial-sync policy to the two public Sugarchain chains. */
constexpr bool UseSharedIbdPolicy(ChainType chain, bool regtest_opt_in, bool native_platform = true)
{
    // The explicit regtest option can exercise native paths on every test host.
    if (chain == ChainType::REGTEST) return regtest_opt_in;
    return native_platform && (chain == ChainType::MAIN || chain == ChainType::TESTNET);
}

/** Public PRESYNC optimizations are for Yespower chains. Regtest keeps its own proof rules. */
inline bool UseSharedPresyncPolicy(const CChainParams& params, bool regtest_opt_in, bool native_platform = true)
{
    const auto chain{params.GetChainType()};
    return UseSharedIbdPolicy(chain, regtest_opt_in, native_platform) &&
        (params.GetConsensus().fYespowerSugar || chain == ChainType::REGTEST);
}
} // namespace node

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 */
std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, ChainType chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/**
 * Sets the params returned by Params() to those for the given chain type.
 */
void SelectParams(ChainType chain);

#endif // BITCOIN_CHAINPARAMS_H
