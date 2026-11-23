// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <bench/data/block413567.raw.h>
#include <chainparams.h>
#include <flatfile.h>
#include <node/blockstorage.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <validation.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

static CBlock CreateTestBlock(const CChainParams& chainparams)
{
    CBlock block;
    SpanReader{benchmark::data::block413567} >> TX_WITH_WITNESS(block);
    // Keep the upstream payload, but use the same fixed native Yespower proof
    // as the stateless validation benchmark. This is not an on-chain block.
    block.nBits = chainparams.GenesisBlock().nBits;
    block.nNonce = 330;
    assert(block.GetPoWHash() == uint256{"0019f3d0bcf28a9b89a104b348cba43fa7ef318ec53908a525b4fe91dde98377"});
    assert(::GetSerializeSize(TX_WITH_WITNESS(block)) == benchmark::data::block413567.size());
    return block;
}

static void WriteBlockBench(benchmark::Bench& bench)
{
    const auto testing_setup{MakeNoLogFileContext<const TestingSetup>(ChainType::MAIN)};
    auto& blockman{testing_setup->m_node.chainman->m_blockman};
    const CBlock block{CreateTestBlock(testing_setup->m_node.chainman->GetParams())};
    bench.run([&] {
        const auto pos{blockman.WriteBlock(block, 413'567)};
        assert(!pos.IsNull());
    });
}

static void ReadBlockBench(benchmark::Bench& bench)
{
    const auto testing_setup{MakeNoLogFileContext<const TestingSetup>(ChainType::MAIN)};
    auto& blockman{testing_setup->m_node.chainman->m_blockman};
    const auto& test_block{CreateTestBlock(testing_setup->m_node.chainman->GetParams())};
    const auto& expected_hash{test_block.GetHash()};
    const auto& pos{blockman.WriteBlock(test_block, 413'567)};
    bench.run([&] {
        CBlock block;
        const auto success{blockman.ReadBlock(block, pos, expected_hash)};
        assert(success);
    });
}

static void ReadRawBlockBench(benchmark::Bench& bench)
{
    const auto testing_setup{MakeNoLogFileContext<const TestingSetup>(ChainType::MAIN)};
    auto& blockman{testing_setup->m_node.chainman->m_blockman};
    const auto pos{blockman.WriteBlock(CreateTestBlock(testing_setup->m_node.chainman->GetParams()), 413'567)};
    bench.run([&] {
        const auto res{blockman.ReadRawBlock(pos)};
        assert(res);
    });
}

BENCHMARK(WriteBlockBench);
BENCHMARK(ReadBlockBench);
BENCHMARK(ReadRawBlockBench);
