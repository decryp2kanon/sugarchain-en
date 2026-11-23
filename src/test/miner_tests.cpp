// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <coins.h>
#include <common/system.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <interfaces/mining.h>
#include <node/miner.h>
#include <policy/policy.h>
#include <test/util/random.h>
#include <test/util/transaction_utils.h>
#include <test/util/txmempool.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/check.h>
#include <util/feefrac.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <versionbits.h>
#include <pow.h>

#include <test/util/common.h>
#include <test/util/setup_common.h>

#include <iterator>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

using namespace util::hex_literals;
using interfaces::BlockTemplate;
using interfaces::Mining;
using node::BlockAssembler;

namespace miner_tests {
struct MinerTestingSetup : public TestingSetup {
    MinerTestingSetup() : TestingSetup{ChainType::MAIN} {}

    void TestPackageSelection(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void TestBasicMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    void TestPrioritisedMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
    bool TestSequenceLocks(const CTransaction& tx, CTxMemPool& tx_mempool) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
    {
        CCoinsViewMemPool view_mempool{&m_node.chainman->ActiveChainstate().CoinsTip(), tx_mempool};
        CBlockIndex* tip{m_node.chainman->ActiveChain().Tip()};
        const std::optional<LockPoints> lock_points{CalculateLockPointsAtTip(tip, view_mempool, tx)};
        return lock_points.has_value() && CheckSequenceLocksAtTip(tip, *lock_points);
    }
    CTxMemPool& MakeMempool()
    {
        // Delete the previous mempool to ensure with valgrind that the old
        // pointer is not accessed, when the new one should be accessed
        // instead.
        m_node.mempool.reset();
        bilingual_str error;
        auto opts = MemPoolOptionsForTest(m_node);
        // The "block size > limit" test creates a cluster of 1192590 vbytes,
        // so set the cluster vbytes limit big enough so that the txgraph
        // doesn't become oversized.
        opts.limits.cluster_size_vbytes = 1'200'000;
        m_node.mempool = std::make_unique<CTxMemPool>(opts, error);
        Assert(error.empty());
        return *m_node.mempool;
    }
    std::unique_ptr<Mining> MakeMining()
    {
        return interfaces::MakeMining(m_node, /*wait_loaded=*/false);
    }
};
} // namespace miner_tests

BOOST_FIXTURE_TEST_SUITE(miner_tests, MinerTestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

// Sugarchain MAIN fixtures: genuine YespowerSugar, five-second timestamps
// after genesis, and height-ordered extranonces. Stale fixtures must fail;
// normal tests never grind replacement proofs.
constexpr static struct {
    unsigned int extranonce;
    unsigned int nonce;
} BLOCKINFO[]{{0, 1516},   {1, 3379},  {2, 833},   {3, 1703},  {4, 117},   {5, 350},
              {6, 553},    {7, 1054},  {8, 2017},  {9, 1142},  {10, 1489}, {11, 32},
              {12, 1087},  {13, 856},  {14, 2961}, {15, 3101}, {16, 44},   {17, 380},
              {18, 96},    {19, 1882}, {20, 2111}, {21, 1014}, {22, 569},  {23, 1238},
              {24, 1493},  {25, 195},  {26, 186},  {27, 413},  {28, 968},  {29, 67},
              {30, 1755},  {31, 191},  {32, 226},  {33, 96},   {34, 2866}, {35, 327},
              {36, 120},   {37, 319},  {38, 1056}, {39, 1618}, {40, 2806}, {41, 926},
              {42, 2551},  {43, 576},  {44, 120},  {45, 876},  {46, 1239}, {47, 695},
              {48, 4795},  {49, 447},  {50, 1546}, {51, 1609}, {52, 370},  {53, 505},
              {54, 124},   {55, 1047}, {56, 107},  {57, 1497}, {58, 1179}, {59, 893},
              {60, 289},   {61, 1498}, {62, 2829}, {63, 359},  {64, 191},  {65, 174},
              {66, 406},   {67, 909},  {68, 1043}, {69, 1080}, {70, 1122}, {71, 355},
              {72, 1575},  {73, 2286}, {74, 2288}, {75, 202},  {76, 4},    {77, 225},
              {78, 154},   {79, 1571}, {80, 256},  {81, 771},  {82, 887},  {83, 748},
              {84, 36},    {85, 707},  {86, 1524}, {87, 1679}, {88, 492},  {89, 592},
              {90, 84},    {91, 465},  {92, 347},  {93, 601},  {94, 589},  {95, 885},
              {96, 395},   {97, 1153}, {98, 556},  {99, 1048}, {100, 199}, {101, 361},
              {102, 2128}, {103, 400}, {104, 255}, {105, 432}, {106, 982}, {107, 990},
              {108, 2107}, {109, 185}};

static std::unique_ptr<CBlockIndex> CreateBlockIndex(int nHeight, CBlockIndex* active_chain_tip) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    auto index{std::make_unique<CBlockIndex>()};
    index->nHeight = nHeight;
    index->pprev = active_chain_tip;
    return index;
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
void MinerTestingSetup::TestPackageSelection(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst)
{
    CTxMemPool& tx_mempool{MakeMempool()};
    auto mining{MakeMining()};
    BlockAssembler::Options options;
    options.coinbase_output_script = scriptPubKey;
    options.include_dummy_extranonce = true;

    LOCK(tx_mempool.cs);
    BOOST_CHECK(tx_mempool.size() == 0);

    // Block template should only have a coinbase when there's nothing in the mempool
    std::unique_ptr<BlockTemplate> block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    CBlock block{block_template->getBlock()};
    BOOST_REQUIRE_EQUAL(block.vtx.size(), 1U);

    // waitNext() on an empty mempool should return nullptr because there is no better template
    auto should_be_nullptr = block_template->waitNext({.timeout = MillisecondsDouble{0}, .fee_threshold = 1});
    BOOST_REQUIRE(should_be_nullptr == nullptr);

    // Unless fee_threshold is 0
    block_template = block_template->waitNext({.timeout = MillisecondsDouble{0}, .fee_threshold = 0});
    BOOST_REQUIRE(block_template);

    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000;
    // This tx has a low fee: 1000 satoshis
    Txid hashParentTx = tx.GetHash(); // save this txid for later use
    const auto parent_tx{entry.Fee(1000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx)};
    TryAddToMempool(tx_mempool, parent_tx);

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 10000;
    Txid hashMediumFeeTx = tx.GetHash();
    const auto medium_fee_tx{entry.Fee(10000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx)};
    TryAddToMempool(tx_mempool, medium_fee_tx);

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 50000; // 50k satoshi fee
    Txid hashHighFeeTx = tx.GetHash();
    const auto high_fee_tx{entry.Fee(50000).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx)};
    TryAddToMempool(tx_mempool, high_fee_tx);

    block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    block = block_template->getBlock();
    BOOST_REQUIRE_EQUAL(block.vtx.size(), 4U);
    BOOST_CHECK(block.vtx[1]->GetHash() == hashParentTx);
    BOOST_CHECK(block.vtx[2]->GetHash() == hashHighFeeTx);
    BOOST_CHECK(block.vtx[3]->GetHash() == hashMediumFeeTx);
    BOOST_CHECK_EQUAL(block.vtx.front()->GetValueOut(), txFirst[0]->vout[0].nValue + 61'000);

    // Test the inclusion of package feerates in the block template and ensure they are sequential.
    const auto block_package_feerates = BlockAssembler{m_node.chainman->ActiveChainstate(), &tx_mempool, options}.CreateNewBlock()->m_package_feerates;
    BOOST_CHECK(block_package_feerates.size() == 2);

    // parent_tx and high_fee_tx are added to the block as a package.
    const auto combined_txs_fee = parent_tx.GetFee() + high_fee_tx.GetFee();
    const auto combined_txs_size = parent_tx.GetTxSize() + high_fee_tx.GetTxSize();
    FeeFrac package_feefrac{combined_txs_fee, combined_txs_size};
    // The package should be added first.
    BOOST_CHECK(block_package_feerates[0] == package_feefrac);

    // The medium_fee_tx should be added next.
    FeeFrac medium_tx_feefrac{medium_fee_tx.GetFee(), medium_fee_tx.GetTxSize()};
    BOOST_CHECK(block_package_feerates[1] == medium_tx_feefrac);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 50000; // 0 fee
    Txid hashFreeTx = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(0).FromTx(tx));
    uint64_t freeTxSize{::GetSerializeSize(TX_WITH_WITNESS(tx))};

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 50000 - feeToUse;
    Txid hashLowFeeTx = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(feeToUse).FromTx(tx));

    // waitNext() should return nullptr because there is no better template
    should_be_nullptr = block_template->waitNext({.timeout = MillisecondsDouble{0}, .fee_threshold = 1});
    BOOST_REQUIRE(should_be_nullptr == nullptr);

    block = block_template->getBlock();
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<block.vtx.size(); ++i) {
        BOOST_CHECK(block.vtx[i]->GetHash() != hashFreeTx);
        BOOST_CHECK(block.vtx[i]->GetHash() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    tx_mempool.removeRecursive(CTransaction(tx), MemPoolRemovalReason::REPLACED);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(feeToUse + 2).FromTx(tx));

    // waitNext() should return if fees for the new template are at least 1 sat up
    // Fees already changed; inspect the next template without advancing mock time.
    block_template = block_template->waitNext({.timeout = MillisecondsDouble{0}, .fee_threshold = 1});
    BOOST_REQUIRE(block_template);
    block = block_template->getBlock();
    BOOST_REQUIRE_EQUAL(block.vtx.size(), 6U);
    BOOST_CHECK(block.vtx[4]->GetHash() == hashFreeTx);
    BOOST_CHECK(block.vtx[5]->GetHash() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout.resize(2);
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 100000000;
    tx.vout[1].nValue = 100000000; // 1 SUGAR output
    // Increase size to avoid rounding errors: when the feerate is extremely small (i.e. 1sat/kvB), evaluating the fee
    // at smaller sizes gives us rounded values that are equal to each other, which means we incorrectly include
    // hashFreeTx2 + hashLowFeeTx2.
    BulkTransaction(tx, 4000);
    Txid hashFreeTx2 = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 100000000 - feeToUse;
    Txid hashLowFeeTx2 = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    block = block_template->getBlock();

    // Verify that this tx isn't selected.
    for (size_t i=0; i<block.vtx.size(); ++i) {
        BOOST_CHECK(block.vtx[i]->GetHash() != hashFreeTx2);
        BOOST_CHECK(block.vtx[i]->GetHash() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = 100000000 - 10000; // 10k satoshi fee
    TryAddToMempool(tx_mempool, entry.Fee(10000).FromTx(tx));
    block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    block = block_template->getBlock();
    BOOST_REQUIRE_EQUAL(block.vtx.size(), 9U);
    BOOST_CHECK(block.vtx[8]->GetHash() == hashLowFeeTx2);
}

std::vector<CTransactionRef> CreateBigSigOpsCluster(const CTransactionRef& first_tx)
{
    std::vector<CTransactionRef> ret;

    CMutableTransaction tx;
    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_CHECKSIG << OP_1;
    tx.vin[0].prevout.hash = first_tx->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(50);
    for (auto &out : tx.vout) {
        out.nValue = first_tx->vout[0].nValue / 50;
        out.scriptPubKey = CScript() << OP_1;
    }

    tx.vout[0].nValue -= CENT;
    CTransactionRef parent_tx = MakeTransactionRef(tx);
    ret.push_back(parent_tx);
    assert(GetLegacySigOpCount(*parent_tx) == 1);

    // Tx1 has 1 sigops, 1 input, 50 outputs.
    // Tx2-51 has 400 sigops: 1 input, 20 CHECKMULTISIG outputs
    // Total: 1000 CHECKMULTISIG + 1
    for (unsigned int i = 0; i < 50; ++i) {
        auto tx2 = tx;
        tx2.vin.resize(1);
        tx2.vin[0].prevout.hash = parent_tx->GetHash();
        tx2.vin[0].prevout.n = i;
        tx2.vin[0].scriptSig = CScript() << OP_1;
        tx2.vout.resize(20);
        tx2.vout[0].nValue = parent_tx->vout[i].nValue - CENT;
        for (auto &out : tx2.vout) {
            out.nValue = 0;
            out.scriptPubKey = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
        }
        ret.push_back(MakeTransactionRef(tx2));
    }
    return ret;
}

void MinerTestingSetup::TestBasicMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst, int baseheight)
{
    Txid hash;
    CMutableTransaction tx;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    const CAmount BLOCKSUBSIDY{GetBlockSubsidy(baseheight + 1, m_node.chainman->GetConsensus())};
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = COIN;
    const CAmount HIGHERFEE = 4 * COIN;

    auto mining{MakeMining()};
    BOOST_REQUIRE(mining);

    BlockAssembler::Options options;
    options.coinbase_output_script = scriptPubKey;
    options.include_dummy_extranonce = true;

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // Just to make sure we can still make simple blocks
        auto block_template{mining->createNewBlock(options, /*cooldown=*/false)};
        BOOST_REQUIRE(block_template);
        CBlock block{block_template->getBlock()};

        auto txs = CreateBigSigOpsCluster(txFirst[0]);

        int64_t legacy_sigops = 0;
        for (auto& t : txs) {
            // If we don't set the number of sigops in the CTxMemPoolEntry,
            // template creation fails during sanity checks.
            TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(t));
            legacy_sigops += GetLegacySigOpCount(*t);
            BOOST_CHECK(tx_mempool.GetIter(t->GetHash()).has_value());
        }
        assert(tx_mempool.mapTx.size() == 51);
        assert(legacy_sigops == 20001);
        BOOST_CHECK_EXCEPTION(mining->createNewBlock(options, /*cooldown=*/false), std::runtime_error, HasReason("bad-blk-sigops"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // Check that the mempool is empty.
        assert(tx_mempool.mapTx.empty());

        // Just to make sure we can still make simple blocks
        auto block_template{mining->createNewBlock(options, /*cooldown=*/false)};
        BOOST_REQUIRE(block_template);
        CBlock block{block_template->getBlock()};

        auto txs = CreateBigSigOpsCluster(txFirst[0]);

        int64_t legacy_sigops = 0;
        for (auto& t : txs) {
            TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).SigOpsCost(GetLegacySigOpCount(*t)*WITNESS_SCALE_FACTOR).FromTx(t));
            legacy_sigops += GetLegacySigOpCount(*t);
            BOOST_CHECK(tx_mempool.GetIter(t->GetHash()).has_value());
        }
        assert(tx_mempool.mapTx.size() == 51);
        assert(legacy_sigops == 20001);

        BOOST_REQUIRE(mining->createNewBlock(options, /*cooldown=*/false));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // block size > limit
        tx.vin.resize(1);
        tx.vout.resize(1);
        tx.vout[0].nValue = BLOCKSUBSIDY;
        // 36 * (520char + DROP) + OP_1 = 18757 bytes
        std::vector<unsigned char> vchData(520);
        for (unsigned int i = 0; i < 18; ++i) {
            tx.vin[0].scriptSig << vchData << OP_DROP;
            tx.vout[0].scriptPubKey << vchData << OP_DROP;
        }
        tx.vin[0].scriptSig << OP_1;
        tx.vout[0].scriptPubKey << OP_1;
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vout[0].nValue = BLOCKSUBSIDY;
        for (unsigned int i = 0; i < 63; ++i) {
            tx.vout[0].nValue -= LOWFEE;
            hash = tx.GetHash();
            bool spendsCoinbase = i == 0; // only first tx spends coinbase
            TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
            BOOST_CHECK(tx_mempool.GetIter(hash).has_value());
            tx.vin[0].prevout.hash = hash;
        }
        BOOST_REQUIRE(mining->createNewBlock(options, /*cooldown=*/false));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // orphan in tx_mempool, template creation fails
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).FromTx(tx));
        BOOST_CHECK_EXCEPTION(mining->createNewBlock(options, /*cooldown=*/false), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // child with higher feerate than parent
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vin[0].prevout.hash = txFirst[1]->GetHash();
        tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
        tx.vin.resize(2);
        tx.vin[1].scriptSig = CScript() << OP_1;
        tx.vin[1].prevout.hash = txFirst[0]->GetHash();
        tx.vin[1].prevout.n = 0;
        tx.vout[0].nValue = tx.vout[0].nValue + BLOCKSUBSIDY - HIGHERFEE; // First txn output + fresh coinbase - new txn fee
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(HIGHERFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        BOOST_REQUIRE(mining->createNewBlock(options, /*cooldown=*/false));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // coinbase in tx_mempool, template creation fails
        tx.vin.resize(1);
        tx.vin[0].prevout.SetNull();
        tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
        tx.vout[0].nValue = 0;
        hash = tx.GetHash();
        // give it a fee so it'll get mined
        TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));
        // Should throw bad-cb-multiple
        BOOST_CHECK_EXCEPTION(mining->createNewBlock(options, /*cooldown=*/false), std::runtime_error, HasReason("bad-cb-multiple"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // double spend txn pair in tx_mempool, template creation fails
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE;
        tx.vout[0].scriptPubKey = CScript() << OP_1;
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        tx.vout[0].scriptPubKey = CScript() << OP_2;
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        BOOST_CHECK_EXCEPTION(mining->createNewBlock(options, /*cooldown=*/false), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    }

    {
        CTxMemPool& tx_mempool{MakeMempool()};
        LOCK(tx_mempool.cs);

        // Check the native subsidy schedule at H-1, H, and H+1 without
        // allocating millions of synthetic indexes or changing consensus.
        // This is scalar boundary coverage. The genuine-proof mining fixtures
        // and mainnet_yespower_block_submission exercise real block assembly
        // and validation at their actual heights; they do not claim full
        // template/ConnectBlock coverage at the 12,500,000-block boundary.
        const auto& consensus{m_node.chainman->GetConsensus()};
        const int halving_height{consensus.nSubsidyHalvingInterval};
        BOOST_REQUIRE_EQUAL(halving_height, 12'500'000);
        BOOST_CHECK_EQUAL(GetBlockSubsidy(halving_height - 1, consensus), CAmount{1} << 32);
        BOOST_CHECK_EQUAL(GetBlockSubsidy(halving_height, consensus), CAmount{1} << 31);
        BOOST_CHECK_EQUAL(GetBlockSubsidy(halving_height + 1, consensus), CAmount{1} << 31);
        const auto reward_template{mining->createNewBlock(options, /*cooldown=*/false)};
        BOOST_REQUIRE(reward_template);
        BOOST_REQUIRE_EQUAL(reward_template->getBlock().vtx.front()->GetValueOut(), BLOCKSUBSIDY);

        // invalid p2sh txn in tx_mempool, template creation fails
        tx.vin[0].prevout.hash = txFirst[0]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vin[0].scriptSig = CScript() << OP_1;
        tx.vout[0].nValue = BLOCKSUBSIDY - LOWFEE;
        CScript script = CScript() << OP_0;
        tx.vout[0].scriptPubKey = GetScriptForDestination(ScriptHash(script));
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
        tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHash();
        TryAddToMempool(tx_mempool, entry.Fee(LOWFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));
        BOOST_CHECK_EXCEPTION(mining->createNewBlock(options, /*cooldown=*/false), std::runtime_error, HasReason("block-script-verify-flag-failed"));


    }

    CTxMemPool& tx_mempool{MakeMempool()};
    LOCK(tx_mempool.cs);

    // non-final txs in mempool
    SetMockTime(m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() + 1);
    const int flags{LOCKTIME_VERIFY_SEQUENCE};
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.version = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = m_node.chainman->ActiveChain().Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(HIGHFEE).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail

    {
        CBlockIndex* active_chain_tip = m_node.chainman->ActiveChain().Tip();
        BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, *CreateBlockIndex(active_chain_tip->nHeight + 2, active_chain_tip))); // Sequence locks pass on 2nd block
    }

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((m_node.chainman->ActiveChain().Tip()->GetMedianTimePast()+1-m_node.chainman->ActiveChain()[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Time(Now<NodeSeconds>()).FromTx(tx));
    BOOST_CHECK(CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail

    const int SEQUENCE_LOCK_TIME = 512; // Sequence locks pass 512 seconds later
    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; ++i)
        m_node.chainman->ActiveChain().Tip()->GetAncestor(m_node.chainman->ActiveChain().Tip()->nHeight - i)->nTime += SEQUENCE_LOCK_TIME; // Trick the MedianTimePast
    {
        CBlockIndex* active_chain_tip = m_node.chainman->ActiveChain().Tip();
        BOOST_CHECK(SequenceLocks(CTransaction(tx), flags, prevheights, *CreateBlockIndex(active_chain_tip->nHeight + 1, active_chain_tip)));
    }

    for (int i = 0; i < CBlockIndex::nMedianTimeSpan; ++i) {
        CBlockIndex* ancestor{Assert(m_node.chainman->ActiveChain().Tip()->GetAncestor(m_node.chainman->ActiveChain().Tip()->nHeight - i))};
        ancestor->nTime -= SEQUENCE_LOCK_TIME; // undo tricked MTP
    }

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vin[0].nSequence = CTxIn::MAX_SEQUENCE_NONFINAL;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = m_node.chainman->ActiveChain().Tip()->nHeight + 1;
    hash = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Time(Now<NodeSeconds>()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), m_node.chainman->ActiveChain().Tip()->nHeight + 2, m_node.chainman->ActiveChain().Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // ensure tx is final for a specific case where there is no locktime and block height is zero
    tx.nLockTime = 0;
    BOOST_CHECK(IsFinalTx(CTransaction(tx), /*nBlockHeight=*/0, m_node.chainman->ActiveChain().Tip()->GetMedianTimePast()));

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.nLockTime = m_node.chainman->ActiveChain().Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Time(Now<NodeSeconds>()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(CTransaction(tx), m_node.chainman->ActiveChain().Tip()->nHeight + 2, m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = m_node.chainman->ActiveChain().Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTxAtTip(*Assert(m_node.chainman->ActiveChain().Tip()), CTransaction{tx})); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(CTransaction{tx}, tx_mempool)); // Sequence locks fail

    // These relative-nonfinal entries were force-inserted into the mempool.
    // Mainnet CSV is active, so template self-validation must reject them.
    BOOST_CHECK_EXCEPTION(mining->createNewBlock(options, /*cooldown=*/false),
                          std::runtime_error, HasReason("bad-txns-nonfinal"));
    // Model the next height with a linked index, then restore the accepted
    // chain and timestamps before running the other mining scenarios.
    auto& active_chain{m_node.chainman->ActiveChain()};
    auto& coins{m_node.chainman->ActiveChainstate().CoinsTip()};
    auto* original_tip{active_chain.Tip()};
    const uint256 next_hash{m_rng.rand256()};
    CBlockIndex next;
    struct RestoreChainState {
        CChain& chain;
        CCoinsViewCache& coins;
        CBlockIndex& tip;
        std::vector<std::pair<CBlockIndex*, uint32_t>> timestamps;
        ~RestoreChainState() EXCLUSIVE_LOCKS_REQUIRED(cs_main)
        {
            for (auto [index, time] : timestamps) index->nTime = time;
            chain.SetTip(tip);
            coins.SetBestBlock(tip.GetBlockHash());
        }
    } restore{active_chain, coins, *original_tip, {}};
    for (auto* index{original_tip}; restore.timestamps.size() < CBlockIndex::nMedianTimeSpan; index = Assert(index->pprev)) {
        restore.timestamps.emplace_back(index, index->nTime);
        index->nTime += SEQUENCE_LOCK_TIME;
    }
    next.pprev = original_tip;
    next.nHeight = original_tip->nHeight + 1;
    next.nVersion = original_tip->nVersion;
    next.nTime = original_tip->nTime + 5;
    next.nBits = original_tip->nBits;
    next.nChainWork = original_tip->nChainWork + GetBlockProof(next);
    next.phashBlock = &next_hash;
    next.BuildSkip();
    active_chain.SetTip(next);
    coins.SetBestBlock(next_hash);
    SetMockTime(m_node.chainman->ActiveChain().Tip()->GetMedianTimePast() + 1);

    auto block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    const CBlock block{block_template->getBlock()};
    BOOST_CHECK_EQUAL(block.vtx.size(), 5U);
}

void MinerTestingSetup::TestPrioritisedMining(const CScript& scriptPubKey, const std::vector<CTransactionRef>& txFirst)
{
    auto mining{MakeMining()};
    BOOST_REQUIRE(mining);

    BlockAssembler::Options options;
    options.coinbase_output_script = scriptPubKey;
    options.include_dummy_extranonce = true;

    CTxMemPool& tx_mempool{MakeMempool()};
    LOCK(tx_mempool.cs);

    TestMemPoolEntryHelper entry;

    // Test that a tx below min fee but prioritised is included
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout.resize(1);
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue; // 0 fee
    Txid hashFreePrioritisedTx = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(0).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashFreePrioritisedTx, 5 * COIN);

    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000;
    // This tx has a low fee: 1000 satoshis
    Txid hashParentTx = tx.GetHash(); // save this txid for later use
    TryAddToMempool(tx_mempool, entry.Fee(1000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[2]->GetHash();
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 10000;
    Txid hashMediumFeeTx = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(10000).Time(Now<NodeSeconds>()).SpendsCoinbase(true).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashMediumFeeTx, -5 * COIN);

    // This tx also has a low fee, but is prioritised
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue - 1000 - 1000; // 1000 satoshi fee
    Txid hashPrioritsedChild = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(1000).Time(Now<NodeSeconds>()).SpendsCoinbase(false).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashPrioritsedChild, 2 * COIN);

    // Test that transaction selection properly updates ancestor fee calculations as prioritised
    // parents get included in a block. Create a transaction with two prioritised ancestors, each
    // included by itself: FreeParent <- FreeChild <- FreeGrandchild.
    // When FreeParent is added, a modified entry will be created for FreeChild + FreeGrandchild
    // FreeParent's prioritisation should not be included in that entry.
    // When FreeChild is included, FreeChild's prioritisation should also not be included.
    tx.vin[0].prevout.hash = txFirst[3]->GetHash();
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue; // 0 fee
    Txid hashFreeParent = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(0).SpendsCoinbase(true).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashFreeParent, 10 * COIN);

    tx.vin[0].prevout.hash = hashFreeParent;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue; // 0 fee
    Txid hashFreeChild = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(0).SpendsCoinbase(false).FromTx(tx));
    tx_mempool.PrioritiseTransaction(hashFreeChild, 1 * COIN);

    tx.vin[0].prevout.hash = hashFreeChild;
    tx.vout[0].nValue = txFirst[0]->vout[0].nValue; // 0 fee
    Txid hashFreeGrandchild = tx.GetHash();
    TryAddToMempool(tx_mempool, entry.Fee(0).SpendsCoinbase(false).FromTx(tx));

    auto block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    CBlock block{block_template->getBlock()};
    BOOST_REQUIRE_EQUAL(block.vtx.size(), 6U);
    BOOST_CHECK(block.vtx[1]->GetHash() == hashFreeParent);
    BOOST_CHECK(block.vtx[2]->GetHash() == hashFreePrioritisedTx);
    BOOST_CHECK(block.vtx[3]->GetHash() == hashParentTx);
    BOOST_CHECK(block.vtx[4]->GetHash() == hashPrioritsedChild);
    BOOST_CHECK(block.vtx[5]->GetHash() == hashFreeChild);
    for (size_t i=0; i<block.vtx.size(); ++i) {
        // The FreeParent and FreeChild's prioritisations should not impact the child.
        BOOST_CHECK(block.vtx[i]->GetHash() != hashFreeGrandchild);
        // De-prioritised transaction should not be included.
        BOOST_CHECK(block.vtx[i]->GetHash() != hashMediumFeeTx);
    }
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    const auto& consensus{Assert(m_node.chainman)->GetParams().GetConsensus()};
    auto mining{MakeMining()};
    BOOST_REQUIRE(mining);

    // Note that by default, these tests run with size accounting enabled.
    CScript scriptPubKey = CScript() << "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f"_hex << OP_CHECKSIG;
    BlockAssembler::Options options;
    options.coinbase_output_script = scriptPubKey;
    options.include_dummy_extranonce = true;

    // Create and check a simple template
    std::unique_ptr<BlockTemplate> block_template = mining->createNewBlock(options, /*cooldown=*/false);
    BOOST_REQUIRE(block_template);
    {
        CBlock block{block_template->getBlock()};
        {
            std::string reason;
            std::string debug;
            BOOST_REQUIRE(!mining->checkBlock(block, {.check_pow = false}, reason, debug));
            BOOST_REQUIRE_EQUAL(reason, "bad-txnmrklroot");
            BOOST_REQUIRE_EQUAL(debug, "hashMerkleRoot mismatch");
        }

        block.hashMerkleRoot = BlockMerkleRoot(block);

        {
            std::string reason;
            std::string debug;
            BOOST_REQUIRE(mining->checkBlock(block, {.check_pow = false}, reason, debug));
            BOOST_REQUIRE_EQUAL(reason, "");
            BOOST_REQUIRE_EQUAL(debug, "");
        }

        {
            // A block template does not have proof-of-work, but it might pass
            // verification by coincidence. Grind the nonce if needed:
            while (CheckBlockProofOfWork(block, consensus)) {
                block.nNonce++;
            }

            std::string reason;
            std::string debug;
            BOOST_REQUIRE(!mining->checkBlock(block, {.check_pow = true}, reason, debug));
            BOOST_REQUIRE_EQUAL(reason, "high-hash");
            BOOST_REQUIRE_EQUAL(debug, "proof of work failed");
        }
    }

    // We can't make transactions until we have inputs
    // Therefore, load 110 blocks :)
    constexpr int NUM_BLOCKS{110};
    BOOST_REQUIRE(NUM_BLOCKS < consensus.nSubsidyHalvingInterval);
    BOOST_REQUIRE(m_node.chainman->GetParams().GetChainType() == ChainType::MAIN);
    BOOST_REQUIRE(consensus.fYespowerSugar);
    BOOST_REQUIRE_EQUAL(consensus.CSVHeight, 0);
    BOOST_REQUIRE_EQUAL(consensus.nPowTargetSpacing, 5);
    BOOST_REQUIRE_EQUAL(GetBlockSubsidy(1, consensus), CAmount{1} << 32);
    static_assert(std::size(BLOCKINFO) == NUM_BLOCKS);
    SetMockTime(std::chrono::seconds{m_node.chainman->GetParams().GenesisBlock().nTime + 5});
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    for (const auto& bi : BLOCKINFO) {
        const int current_height{mining->getTip()->height};

        /**
         * Simple block creation, nothing special yet.
         * If current_height is odd, block_template will have already been
         * set at the end of the previous loop.
         */
        if (current_height % 2 == 0) {
            block_template = mining->createNewBlock(options, /*cooldown=*/false);
            BOOST_REQUIRE(block_template);
        }

        CBlock block{block_template->getBlock()};
        CMutableTransaction txCoinbase(*block.vtx[0]);
        {
            LOCK(cs_main);
            block.nVersion = VERSIONBITS_TOP_BITS;
            block.nTime = m_node.chainman->GetParams().GenesisBlock().nTime + (current_height + 1) * 5;
            txCoinbase.version = 1;
            txCoinbase.vin[0].scriptSig = CScript{} << (current_height + 1) << bi.extranonce;
            txCoinbase.vout[0].scriptPubKey = CScript();
            block.vtx[0] = MakeTransactionRef(txCoinbase);
            if (txFirst.size() == 0)
                baseheight = current_height;
            if (txFirst.size() < 4)
                txFirst.push_back(block.vtx[0]);
            block.hashMerkleRoot = BlockMerkleRoot(block);
        }
        block.nNonce = bi.nonce;
        BOOST_REQUIRE_EQUAL(block.nBits, 0x1f3fffffU);
        BOOST_REQUIRE_EQUAL(block.vtx.front()->GetValueOut(), CAmount{1} << 32);
        BOOST_REQUIRE_MESSAGE(CheckProofOfWork(block.GetPoWHash(), block.nBits, consensus),
                              "Invalid Yespower fixture at mainnet height " << current_height + 1);
        BOOST_REQUIRE(CheckBlockProofOfWork(block, consensus, /*cache_result=*/false));
        SetMockTime(std::chrono::seconds{block.nTime});
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
        // Alternate calls between Chainman's ProcessNewBlock and submitSolution
        // via the Mining interface. The former is used by net_processing as well
        // as the submitblock RPC.
        if (current_height % 2 == 0) {
            BOOST_REQUIRE(Assert(m_node.chainman)->ProcessNewBlock(shared_pblock, /*force_processing=*/true, /*min_pow_checked=*/true, nullptr));
        } else {
            BOOST_REQUIRE(block_template->submitSolution(block.nVersion, block.nTime, block.nNonce, MakeTransactionRef(txCoinbase)));
        }
        {
            LOCK(cs_main);
            // The above calls don't guarantee the tip is actually updated, so
            // we explicitly check this.
            auto maybe_new_tip{Assert(m_node.chainman)->ActiveChain().Tip()};
            BOOST_REQUIRE_EQUAL(maybe_new_tip->GetBlockHash(), block.GetHash());
        }
        if (current_height % 2 == 0) {
            block_template = block_template->waitNext();
            BOOST_REQUIRE(block_template);
        } else {
            // This just adds coverage
            mining->waitTipChanged(block.hashPrevBlock);
        }
    }

    LOCK(cs_main);
    BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Height(), NUM_BLOCKS);
    BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Tip()->GetBlockHash(),
                        uint256{"8f65cb063f48f42e71244e5088935308eccf2c7d28666dbe3ca84fd786caf177"});
    BOOST_REQUIRE_EQUAL(txFirst.size(), 4U);
    for (const auto& tx : txFirst) BOOST_REQUIRE_EQUAL(tx->vout[0].nValue, CAmount{1} << 32);

    TestBasicMining(scriptPubKey, txFirst, baseheight);

    // Keep the mock clock aligned with the actual mined mainnet tip.
    SetMockTime(m_node.chainman->ActiveChain().Tip()->GetBlockTime());

    TestPackageSelection(scriptPubKey, txFirst);

    // Keep the mock clock aligned with the actual mined mainnet tip.
    SetMockTime(m_node.chainman->ActiveChain().Tip()->GetBlockTime());

    TestPrioritisedMining(scriptPubKey, txFirst);
}

BOOST_FIXTURE_TEST_CASE(mainnet_yespower_block_submission, TestingSetup)
{
    // Keep the inherited transaction-selection tests above and independently
    // exercise block assembly with unmodified Sugarchain mainnet consensus.
    auto& chainman{*Assert(m_node.chainman)};
    const auto& params{chainman.GetParams()};
    const auto& consensus{params.GetConsensus()};
    BOOST_REQUIRE(params.GetChainType() == ChainType::MAIN);
    BOOST_REQUIRE(consensus.fYespowerSugar);
    BOOST_REQUIRE_EQUAL(consensus.CSVHeight, 0);
    BOOST_REQUIRE_EQUAL(WITH_LOCK(cs_main, return chainman.ActiveChain().Height()), 0);

    auto mining{interfaces::MakeMining(m_node, /*wait_loaded=*/false)};
    BOOST_REQUIRE(mining);
    BlockAssembler::Options options;
    options.coinbase_output_script = CScript() << OP_TRUE;
    options.include_dummy_extranonce = true;

    const auto find_nonce = [](CBlock& block, const auto& matches) {
        for (uint64_t nonce{0}; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
            block.nNonce = static_cast<uint32_t>(nonce);
            if (matches(block)) return true;
        }
        return false;
    };

    for (int height{1}; height <= 2; ++height) {
        SetMockTime(std::chrono::seconds{params.GenesisBlock().nTime + height * 5});
        auto block_template{mining->createNewBlock(options, /*cooldown=*/false)};
        BOOST_REQUIRE(block_template);
        CBlock block{block_template->getBlock()};
        // Preserve the real coinbase and witness commitment from the assembler.
        block.hashMerkleRoot = BlockMerkleRoot(block);
        BOOST_REQUIRE_EQUAL(block.vtx.size(), 1U);
        BOOST_REQUIRE(block.vtx.front()->IsCoinBase());
        BOOST_CHECK_EQUAL(block.vtx.front()->GetValueOut(), CAmount{1} << 32);
        BOOST_CHECK_EQUAL(block.nBits, 0x1f3fffffU);

        std::string reason;
        std::string debug;
        BOOST_REQUIRE(mining->checkBlock(block, {.check_pow = false}, reason, debug));
        BOOST_CHECK(reason.empty());
        BOOST_CHECK(debug.empty());

        // A SHA256d-only solution must not pass Sugarchain's Yespower verifier.
        CBlock sha256_only{block};
        BOOST_REQUIRE(find_nonce(sha256_only, [&](const CBlock& candidate) {
            return CheckProofOfWork(candidate.GetHash(), candidate.nBits, consensus) &&
                   !CheckBlockProofOfWork(candidate, consensus, /*cache_result=*/false);
        }));
        BOOST_REQUIRE(CheckProofOfWork(sha256_only.GetHash(), sha256_only.nBits, consensus));
        BOOST_REQUIRE(!mining->checkBlock(sha256_only, {.check_pow = true}, reason, debug));
        BOOST_CHECK_EQUAL(reason, "high-hash");
        BOOST_CHECK_EQUAL(debug, "proof of work failed");

        BOOST_REQUIRE(find_nonce(block, [&](const CBlock& candidate) {
            return CheckBlockProofOfWork(candidate, consensus, /*cache_result=*/false);
        }));
        BOOST_REQUIRE(CheckProofOfWork(block.GetPoWHash(), block.nBits, consensus));
        BOOST_REQUIRE(mining->checkBlock(block, {.check_pow = true}, reason, debug));
        BOOST_CHECK(reason.empty());
        BOOST_CHECK(debug.empty());

        if (height == 1) {
            bool new_block{false};
            BOOST_REQUIRE(chainman.ProcessNewBlock(std::make_shared<const CBlock>(block),
                                                   /*force_processing=*/true,
                                                   /*min_pow_checked=*/true, &new_block));
            BOOST_CHECK(new_block);
        } else {
            BOOST_REQUIRE(block_template->submitSolution(block.nVersion, block.nTime,
                                                         block.nNonce, block.vtx.front()));
        }
        LOCK(cs_main);
        const auto* tip{chainman.ActiveChain().Tip()};
        BOOST_REQUIRE(tip);
        BOOST_CHECK_EQUAL(tip->nHeight, height);
        BOOST_CHECK_EQUAL(tip->GetBlockHash(), block.GetHash());
        BOOST_CHECK(tip->IsValid(BLOCK_VALID_SCRIPTS));
    }
}

BOOST_AUTO_TEST_SUITE_END()
