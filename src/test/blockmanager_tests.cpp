// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <clientversion.h>
#include <consensus/validation.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <node/kernel_notifications.h>
#include <pow.h>
#include <script/solver.h>
#include <primitives/block.h>
#include <util/chaintype.h>
#include <util/fs.h>
#include <validation.h>
#include <streams.h>
#include <undo.h>
#if defined(__APPLE__) && defined(__x86_64__)
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <test/util/common.h>
#include <test/util/logging.h>
#include <test/util/setup_common.h>

using kernel::CBlockFileInfo;
using node::STORAGE_HEADER_BYTES;
using node::BlockManager;
using node::KernelNotifications;
using node::MAX_BLOCKFILE_SIZE;

#if defined(__APPLE__) && defined(__x86_64__)
namespace node {
struct BlockManagerTestAccess {
    static size_t Pending(BlockManager& manager, bool undo = false)
    {
        LOCK(manager.cs_LastBlockFile);
        return (undo ? manager.m_undo_writer : manager.m_block_writer).m_pending_bytes;
    }
    static size_t DirtyFiles(BlockManager& manager) { return manager.m_dirty_fileinfo.size(); }
    static size_t DirtyIndexes(BlockManager& manager) { return manager.m_dirty_blockindex.size(); }
    static bool Close(BlockManager& manager, int file_num = -1)
    {
        LOCK(manager.cs_LastBlockFile);
        return manager.m_block_writer.Close(file_num);
    }
    static void MakeReadOnly(BlockManager& manager, const fs::path& path)
    {
        LOCK(manager.cs_LastBlockFile);
        FILE* reader{fsbridge::fopen(path, "rb")};
        BOOST_REQUIRE(reader);
        BOOST_REQUIRE(manager.m_block_writer.m_file);
        BOOST_REQUIRE(::dup2(::fileno(reader), ::fileno(manager.m_block_writer.m_file)) >= 0);
        BOOST_REQUIRE_EQUAL(std::fclose(reader), 0);
    }
    static bool WriteUndo(BlockManager& manager, const CBlockUndo& undo, BlockValidationState& state, CBlockIndex& index) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        return manager.WriteBlockUndo(undo, state, index);
    }
};
} // namespace node

struct BufferedBlockStorageSetup : BasicTestingSetup {
    KernelNotifications notifications{Assert(m_node.shutdown_request), m_node.exit_status, *Assert(m_node.warnings)};
    BlockManager blockman;
    explicit BufferedBlockStorageSetup(bool fast_prune = true)
        : blockman{*Assert(m_node.shutdown_signal), {
              .chainparams = Params(),
              .fast_prune = fast_prune,
              .blocks_dir = m_args.GetBlocksDirPath(),
              .notifications = notifications,
              .block_tree_db_params = {.path = m_args.GetDataDirNet() / "buffered-index", .cache_bytes = 0},
          }} {}
};
struct LargeFileBufferedBlockStorageSetup : BufferedBlockStorageSetup {
    LargeFileBufferedBlockStorageSetup() : BufferedBlockStorageSetup(false) {}
};
#endif

#if defined(WIN32) && (defined(__x86_64__) || defined(_M_X64))
namespace node {
struct BlockManagerTestAccess {
    using WriteFileCache = BlockManager::WriteFileCache;

    static int ReadFile(const BlockManager& manager)
    {
        LOCK(manager.cs_LastBlockFile);
        return manager.m_block_read_stream_num;
    }
    static bool Finalize(BlockManager& manager, int file_num)
    {
        return manager.FlushBlockFile(file_num, /*fFinalize=*/true, /*finalize_undo=*/false);
    }
    static int64_t ReadOffset(const BlockManager& manager)
    {
        LOCK(manager.cs_LastBlockFile);
        return manager.m_block_read_stream_offset;
    }
    static std::pair<int64_t, size_t> ReadAhead(const BlockManager& manager)
    {
        LOCK(manager.cs_LastBlockFile);
        return {manager.m_block_read_ahead_start, manager.m_block_read_ahead_size};
    }
    static void CloseReadStream(const BlockManager& manager)
    {
        LOCK(manager.cs_LastBlockFile);
        manager.CloseBlockReadStream();
    }
    static bool WriteUndo(BlockManager& manager, const CBlockUndo& undo, BlockValidationState& state, CBlockIndex& index) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        return manager.WriteBlockUndo(undo, state, index);
    }
};
} // namespace node

struct MainnetBlockReaderSetup : BasicTestingSetup {
    KernelNotifications notifications{Assert(m_node.shutdown_request), m_node.exit_status, *Assert(m_node.warnings)};
    const fs::path path{m_args.GetDataDirNet() / "reader-blocks"};
    BlockManager::Options ReaderOptions(bool fast_prune)
    {
        fs::create_directories(path);
        // Use a deterministic nonzero key to exercise absolute offsets after
        // seek, not just the unobfuscated case.
        const std::array key{std::byte{0x34}, std::byte{0xe1}, std::byte{0x42}, std::byte{0x73},
                             std::byte{0x55}, std::byte{0x61}, std::byte{0xd3}, std::byte{0x87}};
        AutoFile file{fsbridge::fopen(path / "xor.dat", "wb")};
        BOOST_REQUIRE(!file.IsNull());
        file.write(key);
        BOOST_REQUIRE_EQUAL(file.fclose(), 0);
        return {
            .chainparams = Params(),
            .fast_prune = fast_prune,
            .blocks_dir = path,
            .notifications = notifications,
            .block_tree_db_params = {.path = m_args.GetDataDirNet() / "reader-index", .cache_bytes = 0},
        };
    }
    BlockManager blockman;
    explicit MainnetBlockReaderSetup(bool fast_prune = true)
        : blockman{*Assert(m_node.shutdown_signal), ReaderOptions(fast_prune)} {}
};

struct LargeWindowsBlockWriteSetup : MainnetBlockReaderSetup {
    LargeWindowsBlockWriteSetup() : MainnetBlockReaderSetup(false) {}
};
#endif

// use BasicTestingSetup here for the data directory configuration, setup, and cleanup
BOOST_FIXTURE_TEST_SUITE(blockmanager_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(blockmap_keeps_addresses_and_keys_across_growth)
{
    // Retain the reference backend for differential tests; runtime assertions
    // exercise our adapter rather than retesting standard-library buckets.
    static_assert(std::is_same_v<node::BlockMap::Legacy, std::unordered_map<uint256, CBlockIndex, BlockHasher>>);
    static_assert(!std::is_nothrow_invocable_v<BlockHasher, const uint256&>);
    static_assert(std::is_nothrow_invocable_v<node::FastBlockHasher, const uint256&>);
    for (bool fast : {false, true}) {
        node::BlockMap map{fast};
        BOOST_CHECK_EQUAL(map.IsFast(), fast);
        std::vector<std::pair<const uint256*, CBlockIndex*>> original;
        for (uint64_t id = 1; id <= 20000; ++id) {
            // Identical low words also exercise full-key hashing/equality.
            const auto hash{ArithToUint256(arith_uint256{id} << 64)};
            auto [it, inserted]{map.try_emplace(hash)};
            BOOST_REQUIRE(inserted);
            it->second.phashBlock = &it->first;
            it->second.nHeight = id;
            if (!original.empty()) it->second.pprev = original.back().second;
            original.emplace_back(&it->first, &it->second);
            auto [duplicate, changed]{map.try_emplace(hash)};
            BOOST_CHECK(!changed);
            BOOST_CHECK(&duplicate->second == &it->second);
        }
        map.reserve(100000);
        BOOST_CHECK_EQUAL(map.size(), original.size());
        size_t visited{0};
        for (const auto& [key, index] : map) {
            BOOST_CHECK(index.GetBlockHash() == key);
            BOOST_CHECK(original[index.nHeight - 1].first == &key);
            BOOST_CHECK(original[index.nHeight - 1].second == &index);
            if (index.nHeight > 1) BOOST_CHECK(index.pprev == original[index.nHeight - 2].second);
            ++visited;
        }
        BOOST_CHECK_EQUAL(visited, map.size());
        const auto& constant{map};
        for (const auto& [key, index] : original) {
            BOOST_CHECK(&constant.find(*key)->second == index);
        }
        BOOST_CHECK(!constant.contains(ArithToUint256(arith_uint256{1} << 255)));
    }
}

BOOST_AUTO_TEST_CASE(blockmap_erase_reinsert_and_work)
{
    node::BlockMap map{true};
    map.reserve(6000);
    for (uint64_t id = 1; id <= 6000; ++id) {
        auto [it, inserted]{map.try_emplace(ArithToUint256(arith_uint256{id}))};
        BOOST_REQUIRE(inserted);
        it->second.nHeight = id;
        it->second.phashBlock = &it->first;
        it->second.nChainWork = arith_uint256{1} << 200;
    }
    for (auto it = map.begin(); it != map.end();) {
        if (it->second.nHeight % 2) it = map.erase(it); else ++it;
    }
    BOOST_CHECK_EQUAL(map.size(), 3000);
    for (uint64_t id = 1; id <= 6000; ++id) {
        const auto key{ArithToUint256(arith_uint256{id})};
        BOOST_CHECK_EQUAL(map.contains(key), id % 2 == 0);
        auto [it, inserted]{map.try_emplace(key)};
        BOOST_CHECK_EQUAL(inserted, id % 2 != 0);
        it->second.nHeight = id;
        it->second.phashBlock = &it->first;
    }
    map.reserve(100000);
    BOOST_CHECK_EQUAL(map.size(), 6000);
    size_t count{0};
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        BOOST_CHECK(it->second.GetBlockHash() == it->first);
        if (it->second.nHeight % 2 == 0) BOOST_CHECK(it->second.nChainWork == (arith_uint256{1} << 200));
        ++count;
    }
    BOOST_CHECK_EQUAL(count, 6000);
    for (auto it = map.begin(); it != map.end();) it = map.erase(it);
    BOOST_CHECK(map.empty());
    BOOST_CHECK(map.begin() == map.end());
    BOOST_CHECK(map.try_emplace(uint256{}).second);
}

BOOST_AUTO_TEST_CASE(startup_height_sort_preserves_indexes)
{
    FastRandomContext random{true};
    for (size_t size : {0U, 1U, 64U, 4096U, 8192U, 65535U, 65536U, 65537U}) {
        std::vector<std::unique_ptr<CBlockIndex>> storage;
        std::vector<CBlockIndex*> indexes;
        for (size_t i = 0; i < size; ++i) {
            auto index{std::make_unique<CBlockIndex>()};
            index->nHeight = static_cast<int>(static_cast<int64_t>(random.randrange(uint64_t{1} << 32)) + std::numeric_limits<int>::min());
            if (i % 37 == 0) index->nHeight = std::numeric_limits<int>::min();
            if (i % 41 == 0) index->nHeight = std::numeric_limits<int>::max();
            indexes.push_back(index.get());
            storage.push_back(std::move(index));
        }
        auto expected{indexes};
        std::sort(expected.begin(), expected.end(), node::CBlockIndexHeightOnlyComparator());
        const std::set<CBlockIndex*> pointers{indexes.begin(), indexes.end()};
        node::SortBlockIndicesByHeight(indexes);
        BOOST_CHECK(std::is_sorted(indexes.begin(), indexes.end(), node::CBlockIndexHeightOnlyComparator()));
        const std::set<CBlockIndex*> result{indexes.begin(), indexes.end()};
        BOOST_CHECK(result == pointers);
        BOOST_REQUIRE_EQUAL(indexes.size(), expected.size());
        for (size_t i = 0; i < size; ++i) BOOST_CHECK_EQUAL(indexes[i]->nHeight, expected[i]->nHeight);
    }
}

BOOST_AUTO_TEST_CASE(startup_height_sort_preserves_repeated_pointers)
{
    std::array<CBlockIndex, 17> storage;
    for (size_t i = 0; i < storage.size(); ++i) storage[i].nHeight = static_cast<int>(i) - 8;
    std::vector<CBlockIndex*> indexes;
    indexes.reserve(4096);
    for (size_t i = 0; i < 4096; ++i) indexes.push_back(&storage[(i * 11) % storage.size()]);
    const std::multiset<CBlockIndex*> expected{indexes.begin(), indexes.end()};
    node::SortBlockIndicesByHeight(indexes);
    BOOST_CHECK(std::is_sorted(indexes.begin(), indexes.end(), node::CBlockIndexHeightOnlyComparator()));
    const std::multiset<CBlockIndex*> actual{indexes.begin(), indexes.end()};
    BOOST_CHECK(actual == expected);
}

BOOST_AUTO_TEST_CASE(blockmanager_find_block_pos)
{
    const auto params {CreateChainParams(ArgsManager{}, ChainType::MAIN)};
    KernelNotifications notifications{Assert(m_node.shutdown_request), m_node.exit_status, *Assert(m_node.warnings)};
    const BlockManager::Options blockman_opts{
        .chainparams = *params,
        .blocks_dir = m_args.GetBlocksDirPath(),
        .notifications = notifications,
        .block_tree_db_params = DBParams{
            .path = m_args.GetDataDirNet() / "blocks" / "index",
            .cache_bytes = 0,
        },
    };
    BlockManager blockman{*Assert(m_node.shutdown_signal), blockman_opts};
    // simulate adding a genesis block normally
    BOOST_CHECK_EQUAL(blockman.WriteBlock(params->GenesisBlock(), 0).nPos, STORAGE_HEADER_BYTES);
    // simulate what happens during reindex
    // simulate a well-formed genesis block being found at offset 8 in the blk00000.dat file
    // the block is found at offset 8 because there is an 8 byte serialization header
    // consisting of 4 magic bytes + 4 length bytes before each block in a well-formed blk file.
    const FlatFilePos pos{0, STORAGE_HEADER_BYTES};
    blockman.UpdateBlockInfo(params->GenesisBlock(), 0, pos);
    // now simulate what happens after reindex for the first new block processed
    // the actual block contents don't matter, just that it's a block.
    // verify that the write position is at offset 0x12d.
    // this is a check to make sure that https://github.com/bitcoin/bitcoin/issues/21379 does not recur
    // 8 bytes (for serialization header) + 285 (for serialized genesis block) = 293
    // add another 8 bytes for the second block's serialization header and we get 293 + 8 = 301
    FlatFilePos actual{blockman.WriteBlock(params->GenesisBlock(), 1)};
    BOOST_CHECK_EQUAL(actual.nPos, STORAGE_HEADER_BYTES + ::GetSerializeSize(TX_WITH_WITNESS(params->GenesisBlock())) + STORAGE_HEADER_BYTES);
}

BOOST_FIXTURE_TEST_CASE(blockmanager_loadblockindex_missing_file, TestChain100Setup)
{
    LOCK(::cs_main);
    auto& chainman{*Assert(m_node.chainman)};
    auto& blockman{chainman.m_blockman};
    blockman.WriteBlockIndexDB();
    std::vector<CBlockIndex*> ordered;
    BOOST_REQUIRE(blockman.LoadBlockIndexDB(std::nullopt, &ordered));
    BOOST_REQUIRE_EQUAL(ordered.size(), blockman.m_block_index.size());
    BOOST_CHECK(std::is_sorted(ordered.begin(), ordered.end(), node::CBlockIndexHeightOnlyComparator()));
    const std::set<CBlockIndex*> unique{ordered.begin(), ordered.end()};
    BOOST_CHECK_EQUAL(unique.size(), ordered.size());
    for (auto& [hash, index] : blockman.m_block_index) {
        BOOST_CHECK(unique.contains(&index));
    }
    const auto previous{ordered};

    // Only move a file inside the test fixture, never a user's datadir.
    const auto block_path{blockman.GetBlockPosFilename(chainman.ActiveTip()->GetBlockPos())};
    auto missing_path{block_path};
    missing_path += ".missing";
    fs::rename(block_path, missing_path);
    BOOST_CHECK(!blockman.LoadBlockIndexDB(std::nullopt, &ordered));
    BOOST_CHECK(ordered == previous); // Failed loads do not publish partial output.
    fs::rename(missing_path, block_path);
    BOOST_CHECK(blockman.LoadBlockIndexDB(std::nullopt, &ordered));
    BOOST_CHECK_EQUAL(ordered.size(), previous.size()); // Reload replaces, not appends.

}

// File-presence checks cover data on non-active/invalid branches, but must not
// retain references once those entries are pruned and the index is reloaded.
BOOST_FIXTURE_TEST_CASE(blockmanager_loadblockindex_preserves_unpersisted_header, TestChain100Setup)
{
    auto& chainman{*Assert(m_node.chainman)};
    auto& blockman{chainman.m_blockman};
    CBlock block{CreateBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()), chainman.ActiveChainstate())};
    {
        LOCK(::cs_main);
        blockman.WriteBlockIndexDB();
        block.nNonce = 0;
        while (!CheckProofOfWork(block.GetHash(), block.nBits, chainman.GetConsensus())) ++block.nNonce;
        CBlockIndex* ignored_best{nullptr};
        auto* unpersisted{blockman.AddToBlockIndex(block, ignored_best)};
        for (int reload = 0; reload < 2; ++reload) {
            std::vector<CBlockIndex*> ordered;
            BOOST_REQUIRE(blockman.LoadBlockIndexDB(std::nullopt, &ordered));
            const std::set<CBlockIndex*> unique{ordered.begin(), ordered.end()};
            BOOST_CHECK_EQUAL(ordered.size(), blockman.m_block_index.size());
            BOOST_CHECK_EQUAL(unique.size(), ordered.size());
            BOOST_CHECK_EQUAL(unique.count(unpersisted), 1U);
            for (auto& [hash, index] : blockman.m_block_index) BOOST_CHECK_EQUAL(unique.count(&index), 1U);
            BOOST_CHECK(std::is_sorted(ordered.begin(), ordered.end(), node::CBlockIndexHeightOnlyComparator()));
        }
    }

    // Exercise the same unpersisted-header boundary at production scale. Seed
    // valid regtest ancestors directly to avoid running the O(N) consistency
    // scan after each seeded header; the boundary submissions below use the
    // real validation/flush path with consistency checks enabled.
    const auto& consensus{chainman.GetConsensus()};
    CBlockHeader header{block};
    auto next_header = [&] {
        header.hashPrevBlock = header.GetHash();
        ++header.nTime;
        header.nNonce = 0;
        while (!CheckProofOfWork(header.GetHash(), header.nBits, consensus)) ++header.nNonce;
    };
    {
        LOCK(cs_main);
        for (size_t i{1}; i < BlockManager::MAX_DIRTY_BLOCK_INDEX - 1; ++i) {
            next_header();
            blockman.AddToBlockIndex(header, chainman.m_best_header);
        }
        BOOST_CHECK(!blockman.BlockIndexWriteDue());
    }
    SetMockTime(header.nTime + 10);
    const auto before_boundary{header.GetHash()};
    CDiskBlockIndex disk;
    auto read_index = [&](const uint256& hash) {
        LOCK(cs_main);
        return blockman.m_block_tree_db->Read(std::make_pair(uint8_t{'b'}, hash), disk);
    };
    BOOST_CHECK(!read_index(before_boundary));
    BlockValidationState duplicate_state;
    BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({&header, 1}, true, duplicate_state));
    BOOST_CHECK(!read_index(before_boundary));

    next_header();
    auto invalid{header};
    do { ++invalid.nNonce; } while (CheckProofOfWork(invalid.GetHash(), invalid.nBits, consensus));
    BlockValidationState invalid_state;
    BOOST_CHECK(!chainman.ProcessNewBlockHeaders({&invalid, 1}, true, invalid_state));
    BOOST_CHECK(invalid_state.IsInvalid());
    BOOST_CHECK(!read_index(before_boundary));

    const auto coins_best{WITH_LOCK(cs_main, return chainman.ActiveChainstate().CoinsTip().GetBestBlock())};
    BlockValidationState boundary_state;
    BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({&header, 1}, true, boundary_state));
    BOOST_REQUIRE(read_index(header.GetHash()));
    BOOST_CHECK(disk.hashPrev == before_boundary);
    BOOST_CHECK(disk.ConstructBlockHash() == header.GetHash());
    BOOST_REQUIRE(read_index(before_boundary));
    BOOST_CHECK(disk.ConstructBlockHash() == before_boundary);
    BOOST_CHECK(!WITH_LOCK(cs_main, return blockman.BlockIndexWriteDue()));
    BOOST_CHECK(WITH_LOCK(cs_main, return chainman.ActiveChainstate().CoinsTip().GetBestBlock()) == coins_best);
    BOOST_CHECK(!WITH_LOCK(cs_main, return blockman.m_block_tree_db->HasYespowerEvidence(header, consensus)));

    // A new header after the boundary remains dirty until a later flush.
    next_header();
    BlockValidationState after_state;
    BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({&header, 1}, true, after_state));
    BOOST_CHECK(!read_index(header.GetHash()));
    WITH_LOCK(cs_main, blockman.WriteBlockIndexDB());
    BOOST_REQUIRE(read_index(header.GetHash()));
    BOOST_CHECK(disk.ConstructBlockHash() == header.GetHash());
}

BOOST_FIXTURE_TEST_CASE(blockmanager_loadblockindex_branch_file_and_pruning, TestChain100Setup)
{
    auto& chainman{*Assert(m_node.chainman)};
    auto& blockman{chainman.m_blockman};
    CBlock block{CreateBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()), chainman.ActiveChainstate())};
    LOCK(::cs_main);
    block.hashPrevBlock = chainman.ActiveChain()[99]->GetBlockHash();
    block.nNonce = 0;
    while (!CheckProofOfWork(block.GetHash(), block.nBits, chainman.GetConsensus())) ++block.nNonce;
    CBlockIndex* ignored_best{nullptr};
    auto* index{blockman.AddToBlockIndex(block, ignored_best)};
    BOOST_REQUIRE(!chainman.ActiveChain().Contains(index));
    blockman.GetBlockFileInfo(0)->nSize = MAX_BLOCKFILE_SIZE;
    const FlatFilePos pos{blockman.WriteBlock(block, index->nHeight)};
    BOOST_REQUIRE_EQUAL(pos.nFile, 1);
    index->nFile = pos.nFile;
    index->nDataPos = pos.nPos;
    index->nTx = block.vtx.size();
    index->nStatus |= BLOCK_HAVE_DATA | BLOCK_FAILED_VALID;
    blockman.WriteBlockIndexDB();
    BOOST_REQUIRE(blockman.LoadBlockIndexDB(std::nullopt));

    const auto path{blockman.GetBlockPosFilename(pos)};
    auto missing{path};
    missing += ".missing";
    fs::rename(path, missing);
    BOOST_CHECK(!blockman.LoadBlockIndexDB(std::nullopt));
    fs::rename(missing, path);
    BOOST_CHECK(blockman.LoadBlockIndexDB(std::nullopt));

    blockman.PruneOneBlockFile(pos.nFile);
    blockman.WriteBlockIndexDB();
    fs::rename(path, missing);
    BOOST_CHECK(blockman.LoadBlockIndexDB(std::nullopt));
    BOOST_CHECK(!(index->nStatus & BLOCK_HAVE_DATA));
    fs::rename(missing, path);
}

BOOST_FIXTURE_TEST_CASE(blockmanager_scan_unlink_already_pruned_files, TestChain100Setup)
{
    // Cap last block file size, and mine new block in a new block file.
    auto& chainman{*Assert(m_node.chainman)};
    auto& blockman{chainman.m_blockman};
    const CBlockIndex* old_tip{WITH_LOCK(chainman.GetMutex(), return chainman.ActiveChain().Tip())};
    WITH_LOCK(chainman.GetMutex(), blockman.GetBlockFileInfo(old_tip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE);
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    // Prune the older block file, but don't unlink it
    int file_number;
    {
        LOCK(chainman.GetMutex());
        file_number = old_tip->GetBlockPos().nFile;
        blockman.PruneOneBlockFile(file_number);
    }

    const FlatFilePos pos(file_number, 0);

    // Check that the file is not unlinked after ScanAndUnlinkAlreadyPrunedFiles
    // if m_have_pruned is not yet set
    WITH_LOCK(chainman.GetMutex(), blockman.ScanAndUnlinkAlreadyPrunedFiles());
    BOOST_CHECK(!blockman.OpenBlockFile(pos, true).IsNull());

    // Check that the file is unlinked after ScanAndUnlinkAlreadyPrunedFiles
    // once m_have_pruned is set
    blockman.m_have_pruned = true;
    WITH_LOCK(chainman.GetMutex(), blockman.ScanAndUnlinkAlreadyPrunedFiles());
    BOOST_CHECK(blockman.OpenBlockFile(pos, true).IsNull());

    // Check that calling with already pruned files doesn't cause an error
    WITH_LOCK(chainman.GetMutex(), blockman.ScanAndUnlinkAlreadyPrunedFiles());

    // Check that the new tip file has not been removed
    const CBlockIndex* new_tip{WITH_LOCK(chainman.GetMutex(), return chainman.ActiveChain().Tip())};
    BOOST_CHECK_NE(old_tip, new_tip);
    const int new_file_number{WITH_LOCK(chainman.GetMutex(), return new_tip->GetBlockPos().nFile)};
    const FlatFilePos new_pos(new_file_number, 0);
    BOOST_CHECK(!blockman.OpenBlockFile(new_pos, true).IsNull());
}

BOOST_FIXTURE_TEST_CASE(blockmanager_block_data_availability, TestChain100Setup)
{
    // The goal of the function is to return the first not pruned block in the range [upper_block, lower_block].
    LOCK(::cs_main);
    auto& chainman = m_node.chainman;
    auto& blockman = chainman->m_blockman;
    const CBlockIndex& tip = *chainman->ActiveTip();

    // Function to prune all blocks from 'last_pruned_block' down to the genesis block
    const auto& func_prune_blocks = [&](CBlockIndex* last_pruned_block)
    {
        LOCK(::cs_main);
        CBlockIndex* it = last_pruned_block;
        while (it != nullptr && it->nStatus & BLOCK_HAVE_DATA) {
            it->nStatus &= ~BLOCK_HAVE_DATA;
            it = it->pprev;
        }
    };

    // 1) Return genesis block when all blocks are available
    BOOST_CHECK_EQUAL(&blockman.GetFirstBlock(tip, BLOCK_HAVE_DATA), chainman->ActiveChain()[0]);
    BOOST_CHECK(blockman.CheckBlockDataAvailability(tip, *chainman->ActiveChain()[0]));

    // 2) Check lower_block when all blocks are available
    CBlockIndex* lower_block = chainman->ActiveChain()[tip.nHeight / 2];
    BOOST_CHECK(blockman.CheckBlockDataAvailability(tip, *lower_block));

    // Ensure we don't fail due to the expected absence of undo data in the genesis block
    CBlockIndex* upper_block = chainman->ActiveChain()[2];
    CBlockIndex* genesis = chainman->ActiveChain()[0];
    BOOST_CHECK(blockman.CheckBlockDataAvailability(*upper_block, *genesis, BlockStatus{BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO}));
    // Ensure we detect absence of undo data in the first block
    chainman->ActiveChain()[1]->nStatus &= ~BLOCK_HAVE_UNDO;
    BOOST_CHECK(!blockman.CheckBlockDataAvailability(tip, *genesis, BlockStatus{BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO}));

    // Prune half of the blocks
    int height_to_prune = tip.nHeight / 2;
    CBlockIndex* first_available_block = chainman->ActiveChain()[height_to_prune + 1];
    CBlockIndex* last_pruned_block = first_available_block->pprev;
    func_prune_blocks(last_pruned_block);

    // 3) The last block not pruned is in-between upper-block and the genesis block
    BOOST_CHECK_EQUAL(&blockman.GetFirstBlock(tip, BLOCK_HAVE_DATA), first_available_block);
    BOOST_CHECK(blockman.CheckBlockDataAvailability(tip, *first_available_block));
    BOOST_CHECK(!blockman.CheckBlockDataAvailability(tip, *last_pruned_block));

    // Simulate that the first available block is missing undo data and
    // detect this by using a status mask.
    first_available_block->nStatus &= ~BLOCK_HAVE_UNDO;
    BOOST_CHECK(!blockman.CheckBlockDataAvailability(tip, *first_available_block, BlockStatus{BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO}));
    BOOST_CHECK(blockman.CheckBlockDataAvailability(tip, *first_available_block, BlockStatus{BLOCK_HAVE_DATA}));
}

BOOST_FIXTURE_TEST_CASE(blockmanager_block_data_part, TestChain100Setup)
{
    LOCK(::cs_main);
    auto& chainman{m_node.chainman};
    auto& blockman{chainman->m_blockman};
    const CBlockIndex& tip{*chainman->ActiveTip()};
    const FlatFilePos tip_block_pos{tip.GetBlockPos()};

    auto block{blockman.ReadRawBlock(tip_block_pos)};
    BOOST_REQUIRE(block);
    BOOST_REQUIRE_GE(block->size(), 200);

    const auto expect_part{[&](size_t offset, size_t size) {
        auto res{blockman.ReadRawBlock(tip_block_pos, std::pair{offset, size})};
        BOOST_CHECK(res);
        const auto& part{res.value()};
        BOOST_CHECK_EQUAL_COLLECTIONS(part.begin(), part.end(), block->begin() + offset, block->begin() + offset + size);
    }};

    expect_part(0, 20);
    expect_part(0, block->size() - 1);
    expect_part(0, block->size() - 10);
    expect_part(0, block->size());
    expect_part(1, block->size() - 1);
    expect_part(10, 20);
    expect_part(block->size() - 1, 1);
#if defined(WIN32) && (defined(__x86_64__) || defined(_M_X64))
    // Regtest also retains the native reader across range reads.
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::ReadFile(blockman), tip_block_pos.nFile);
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::ReadOffset(blockman), tip_block_pos.nPos + block->size());
#endif
}

#if defined(WIN32) && (defined(__x86_64__) || defined(_M_X64))
BOOST_FIXTURE_TEST_CASE(windows_mainnet_block_reader_append_seeks_and_short_read, MainnetBlockReaderSetup)
{
    using Access = node::BlockManagerTestAccess;
    CBlock block;
    block.nVersion = 7;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    DataStream first_expected;
    first_expected << TX_WITH_WITNESS(block);
    const auto initial{blockman.ReadRawBlock(first)};
    BOOST_REQUIRE(initial);
    BOOST_CHECK(std::ranges::equal(*initial, first_expected));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), first.nFile);
    BOOST_CHECK_EQUAL(Access::ReadOffset(blockman), first.nPos + first_expected.size());

    block.nVersion = 8;
    block.nNonce = 0x94265ecf;
    const auto second{blockman.WriteBlock(block, 2)};
    BOOST_REQUIRE(!second.IsNull());
    BOOST_REQUIRE_EQUAL(first.nFile, second.nFile);
    BOOST_REQUIRE_LT(second.nPos, 4096U);
    DataStream second_expected;
    second_expected << TX_WITH_WITNESS(block);
    const auto appended{blockman.ReadRawBlock(second)};
    BOOST_REQUIRE(appended);
    BOOST_CHECK(std::ranges::equal(*appended, second_expected));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), second.nFile);
    BOOST_CHECK_EQUAL(Access::ReadOffset(blockman), second.nPos + second_expected.size());

    std::atomic_bool good{true};
    const auto read_parts = [&](FlatFilePos position, const DataStream& expected, size_t offset) {
        for (unsigned i{0}; i < 64; ++i) {
            const auto bytes{blockman.ReadRawBlock(position, std::pair{offset, size_t{13}})};
            if (!bytes || !std::ranges::equal(*bytes, std::span{expected}.subspan(offset, 13))) {
                good.store(false, std::memory_order_relaxed);
                return;
            }
        }
    };
    std::jthread low{[&] { read_parts(first, first_expected, 3); }};
    std::jthread high{[&] { read_parts(second, second_expected, 5); }};
    low.join();
    high.join();
    BOOST_CHECK(good.load(std::memory_order_relaxed));

    const auto invalid_part{blockman.ReadRawBlock(second, std::pair{second_expected.size(), size_t{1}})};
    BOOST_REQUIRE(!invalid_part);
    BOOST_CHECK_EQUAL(invalid_part.error(), node::ReadRawError::BadPartRange);
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), second.nFile);

    BOOST_REQUIRE(Access::Finalize(blockman, second.nFile));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), -1);
    BOOST_CHECK_EQUAL(Access::ReadOffset(blockman), 0);
    const auto filename{blockman.GetBlockPosFilename(second)};
    fs::resize_file(filename, fs::file_size(filename) - 1);
    const auto missing{blockman.ReadRawBlock(second)};
    BOOST_REQUIRE(!missing);
    BOOST_CHECK_EQUAL(missing.error(), node::ReadRawError::IO);
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), -1);
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
}

BOOST_AUTO_TEST_CASE(windows_cached_writer_position_and_obfuscation)
{
    constexpr std::array nonzero_key{std::byte{0x47}, std::byte{0x15}, std::byte{0xcb}, std::byte{0x28},
                                    std::byte{0xae}, std::byte{0x67}, std::byte{0x90}, std::byte{0x03}};
    constexpr std::array first{std::byte{1}, std::byte{2}, std::byte{3}};
    constexpr std::array second{std::byte{4}, std::byte{5}, std::byte{6}};
    constexpr std::array last{std::byte{7}, std::byte{8}, std::byte{9}};
    for (bool obfuscated : {false, true}) {
        const auto key{obfuscated ? nonzero_key : decltype(nonzero_key){}};
        const Obfuscation obfuscation{key};
        const FlatFileSeq files{m_args.GetDataDirNet() / (obfuscated ? "writer-xor" : "writer-plain"),
                                "blk", 4096};
        node::BlockManagerTestAccess::WriteFileCache writer;
        const auto check_bytes = [&](const FlatFilePos& pos, std::span<const std::byte> bytes) {
            // Read through a different handle without using production deobfuscation.
            AutoFile input{files.Open(pos, /*read_only=*/true)};
            BOOST_REQUIRE(!input.IsNull());
            std::vector<std::byte> physical(bytes.size());
            input.read(physical);
            for (size_t i{0}; i < bytes.size(); ++i) {
                BOOST_CHECK(physical[i] == (bytes[i] ^ key[(pos.nPos + i) % key.size()]));
            }
        };
        const auto write = [&](const FlatFilePos& pos, std::span<const std::byte> bytes) {
            AutoFile stream{writer.Open(files, pos, obfuscation)};
            BOOST_REQUIRE(!stream.IsNull());
            BOOST_CHECK_EQUAL(stream.tell(), pos.nPos);
            stream.write(bytes);
            BOOST_REQUIRE(writer.Keep(stream, pos.nFile));
            check_bytes(pos, bytes);
        };

        write({0, 0}, first);
        write({0, 3}, second); // Adjacent append.
        write({0, 17}, last);  // Forward seek over a gap.
        write({0, 2}, last);   // Backward seek and overwrite.
        write({1, 0}, second); // Rotation must discard the first file's cursor.
        BOOST_REQUIRE(writer.Close(0)); // Closing a different file must leave the current cursor valid.
        write({1, 3}, first);
        BOOST_REQUIRE(writer.Close(1));
        BOOST_REQUIRE(files.Flush({1, 6}, /*finalize=*/true));
        write({1, 6}, last); // Reopen the finalized file with its actual offset.
        BOOST_REQUIRE(writer.Close());
        check_bytes({0, 0}, std::span{first}.first(2));
        check_bytes({0, 2}, last);
        check_bytes({0, 5}, std::span{second}.last(1));
        check_bytes({0, 17}, last);
        check_bytes({1, 0}, second);
        check_bytes({1, 3}, first);
        check_bytes({1, 6}, last);
    }
}

BOOST_FIXTURE_TEST_CASE(windows_block_and_undo_write_buffer_boundaries, LargeWindowsBlockWriteSetup)
{
    LOCK(cs_main);
    constexpr size_t buffer_cap{64 * 1024};
    const uint256 previous_hash{Params().GenesisBlock().GetHash()};
    CBlockIndex previous;
    previous.phashBlock = &previous_hash;

    // Small records bracket one record exceeding the buffer capacity in both sequences.
    std::array<FlatFilePos, 3> positions;
    std::array<CBlockIndex, 3> indexes;
    std::array<DataStream, 3> expected_blocks;
    std::array<DataStream, 3> expected_undo;
    for (size_t i{0}; i < positions.size(); ++i) {
        const bool large{i == 1};
        const int height{static_cast<int>(i + 1)};
        CBlock block;
        block.nNonce = static_cast<uint32_t>(height);
        CMutableTransaction tx;
        tx.vin.resize(1);
        auto& script{tx.vin[0].scriptSig};
        script.resize(large ? 96 * 1024 : 13);
        for (size_t j{0}; j < script.size(); ++j) {
            script[j] = static_cast<unsigned char>((j + i) % 251);
        }
        block.vtx.push_back(MakeTransactionRef(tx));
        expected_blocks[i] << TX_WITH_WITNESS(block);
        BOOST_REQUIRE_EQUAL(expected_blocks[i].size() + STORAGE_HEADER_BYTES > buffer_cap, large);
        positions[i] = blockman.WriteBlock(block, height);
        BOOST_REQUIRE(!positions[i].IsNull());

        CBlockUndo undo;
        auto& prevouts{undo.vtxundo.emplace_back().vprevout};
        CScript undo_script;
        undo_script.resize(24);
        std::ranges::fill(undo_script, OP_TRUE);
        for (size_t j{0}; j < (large ? 4096U : 1U); ++j) {
            prevouts.emplace_back(CTxOut{static_cast<CAmount>(j + height), undo_script}, height, false);
        }
        expected_undo[i] << undo;
        BOOST_REQUIRE_EQUAL(expected_undo[i].size() + node::UNDO_DATA_DISK_OVERHEAD > buffer_cap, large);
        auto& index{indexes[i]};
        index.pprev = &previous;
        index.nFile = positions[i].nFile;
        index.nHeight = height;
        BlockValidationState state;
        BOOST_REQUIRE(node::BlockManagerTestAccess::WriteUndo(blockman, undo, state, index));
    }

    for (size_t i{0}; i < positions.size(); ++i) {
        BOOST_CHECK_EQUAL(positions[i].nFile, positions[0].nFile);
        if (i > 0) {
            BOOST_CHECK_EQUAL(positions[i].nPos, positions[i - 1].nPos + expected_blocks[i - 1].size() + STORAGE_HEADER_BYTES);
            BOOST_CHECK_EQUAL(indexes[i].GetUndoPos().nPos, indexes[i - 1].GetUndoPos().nPos + expected_undo[i - 1].size() + node::UNDO_DATA_DISK_OVERHEAD);
        }
        const auto block{blockman.ReadRawBlock(positions[i])};
        BOOST_REQUIRE(block);
        BOOST_CHECK(std::ranges::equal(*block, expected_blocks[i]));

        CBlockUndo undo;
        BOOST_REQUIRE(blockman.ReadBlockUndo(undo, indexes[i]));
        DataStream serialized_undo;
        serialized_undo << undo;
        BOOST_CHECK(std::ranges::equal(serialized_undo, expected_undo[i]));
    }
}

BOOST_FIXTURE_TEST_CASE(windows_raw_read_cache_bounds_and_visibility, LargeWindowsBlockWriteSetup)
{
    using Access = node::BlockManagerTestAccess;
    constexpr size_t buffer_size{64 * 1024};
    CBlock small;
    small.nVersion = 7;
    DataStream expected_small;
    expected_small << TX_WITH_WITNESS(small);
    const auto first{blockman.WriteBlock(small, 1)};
    BOOST_REQUIRE(!first.IsNull());
    const auto first_raw{blockman.ReadRawBlock(first)};
    BOOST_REQUIRE(first_raw);
    BOOST_CHECK(std::ranges::equal(*first_raw, expected_small));
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);

    const auto second{blockman.WriteBlock(small, 2)};
    BOOST_REQUIRE(!second.IsNull());
    BOOST_REQUIRE_EQUAL(second.nFile, first.nFile);
    BOOST_REQUIRE(blockman.ReadRawBlock(second));
    const size_t written{second.nPos + expected_small.size()};
    BOOST_REQUIRE_GT(fs::file_size(blockman.GetBlockPosFilename(second)), written);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).first, second.nPos - STORAGE_HEADER_BYTES);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, expected_small.size() + STORAGE_HEADER_BYTES);

    CBlock large;
    CMutableTransaction tx;
    tx.vin.resize(1);
    auto& script{tx.vin[0].scriptSig};
    script.resize(96 * 1024);
    for (size_t i{0}; i < script.size(); ++i) {
        script[i] = static_cast<unsigned char>(i % 251);
    }
    large.vtx.push_back(MakeTransactionRef(tx));
    DataStream expected_large;
    expected_large << TX_WITH_WITNESS(large);
    const auto large_pos{blockman.WriteBlock(large, 3)};
    BOOST_REQUIRE(!large_pos.IsNull());
    BOOST_REQUIRE_EQUAL(large_pos.nFile, first.nFile);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);

    Access::CloseReadStream(blockman);
    const auto random{blockman.ReadRawBlock(large_pos)};
    BOOST_REQUIRE(random);
    BOOST_CHECK(std::ranges::equal(*random, expected_large));
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);

    // A partial miss at an adjacent frame must not trigger a speculative read.
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
    const auto partial_miss{blockman.ReadRawBlock(second, std::pair{size_t{4}, size_t{13}})};
    BOOST_REQUIRE(partial_miss);
    BOOST_CHECK(std::ranges::equal(*partial_miss, std::span{expected_small}.subspan(4, 13)));
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);

    Access::CloseReadStream(blockman);
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
    BOOST_REQUIRE(blockman.ReadRawBlock(second));
    const auto [window_start, window_size]{Access::ReadAhead(blockman)};
    BOOST_REQUIRE_EQUAL(window_size, buffer_size);
    BOOST_CHECK_EQUAL(window_start, second.nPos - STORAGE_HEADER_BYTES);

    // A partial hit spanning the window keeps the cached prefix and reads the remainder.
    const size_t offset{static_cast<size_t>(window_start) + buffer_size - large_pos.nPos - 11};
    constexpr size_t length{29};
    const auto part{blockman.ReadRawBlock(large_pos, std::pair{offset, length})};
    BOOST_REQUIRE(part);
    BOOST_CHECK(std::ranges::equal(*part, std::span{expected_large}.subspan(offset, length)));
    BOOST_CHECK_EQUAL(Access::ReadOffset(blockman), large_pos.nPos + offset + length);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).first, window_start);
    const auto full{blockman.ReadRawBlock(large_pos)};
    BOOST_REQUIRE(full);
    BOOST_CHECK(std::ranges::equal(*full, expected_large));

    const auto last{blockman.WriteBlock(small, 4)};
    BOOST_REQUIRE(!last.IsNull());
    BOOST_REQUIRE_EQUAL(last.nFile, first.nFile);
    const auto appended{blockman.ReadRawBlock(last)};
    BOOST_REQUIRE(appended);
    BOOST_CHECK(std::ranges::equal(*appended, expected_small));
    const auto [start, bytes]{Access::ReadAhead(blockman)};
    const auto logical_end{last.nPos + expected_small.size()};
    BOOST_CHECK_EQUAL(start + bytes, logical_end);
    BOOST_REQUIRE_GT(fs::file_size(blockman.GetBlockPosFilename(last)), logical_end);

    // A direct writable handle can mutate data after another exact read.
    AutoFile writer{blockman.OpenBlockFile(last, /*fReadOnly=*/false)};
    BOOST_REQUIRE(!writer.IsNull());
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);
    BOOST_REQUIRE(blockman.ReadRawBlock(large_pos));
    BOOST_REQUIRE(blockman.ReadRawBlock(last));
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);
    small.nVersion = 8;
    DataStream replacement;
    replacement << TX_WITH_WITNESS(small);
    writer.write(std::span{replacement});
    const bool committed{writer.Commit()};
    const auto replaced{blockman.ReadRawBlock(last)};
    BOOST_REQUIRE_EQUAL(writer.fclose(), 0);
    BOOST_REQUIRE(committed);
    BOOST_REQUIRE(replaced);
    BOOST_CHECK(std::ranges::equal(*replaced, replacement));
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);
}

BOOST_FIXTURE_TEST_CASE(windows_mainnet_block_reader_rotation_prune_and_reload, MainnetBlockReaderSetup)
{
    using Access = node::BlockManagerTestAccess;
    CBlock block;
    block.nVersion = 10;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), first.nFile);
    const auto adjacent{blockman.WriteBlock(block, 2)};
    BOOST_REQUIRE(!adjacent.IsNull());
    BOOST_REQUIRE_EQUAL(adjacent.nFile, first.nFile);
    BOOST_REQUIRE(blockman.ReadRawBlock(adjacent));
    BOOST_CHECK_GT(Access::ReadAhead(blockman).second, 0U);

    blockman.GetBlockFileInfo(first.nFile)->nSize = 0x10000;
    block.nVersion = 11;
    const auto second{blockman.WriteBlock(block, 3)};
    BOOST_REQUIRE(!second.IsNull());
    BOOST_REQUIRE_NE(first.nFile, second.nFile);
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), -1);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);
    BOOST_REQUIRE(blockman.ReadRawBlock(second));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), second.nFile);
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), first.nFile);
    BOOST_REQUIRE(blockman.ReadRawBlock(adjacent));
    BOOST_CHECK_GT(Access::ReadAhead(blockman).second, 0U);
    blockman.UnlinkPrunedFiles({first.nFile});
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), -1);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);
    BOOST_CHECK(!fs::exists(blockman.GetBlockPosFilename(first)));
    BOOST_CHECK(!blockman.ReadRawBlock(first));
    BOOST_REQUIRE(blockman.ReadRawBlock(second));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), second.nFile);
    const auto adjacent_second{blockman.WriteBlock(block, 4)};
    BOOST_REQUIRE(!adjacent_second.IsNull());
    BOOST_REQUIRE_EQUAL(adjacent_second.nFile, second.nFile);
    BOOST_REQUIRE(blockman.ReadRawBlock(adjacent_second));
    BOOST_CHECK_GT(Access::ReadAhead(blockman).second, 0U);

    BOOST_REQUIRE(WITH_LOCK(cs_main, return blockman.LoadBlockIndexDB(std::nullopt)));
    BOOST_CHECK_EQUAL(Access::ReadFile(blockman), -1);
    BOOST_CHECK_EQUAL(Access::ReadAhead(blockman).second, 0U);
    BOOST_REQUIRE(blockman.ReadRawBlock(second));
}
#endif

BOOST_FIXTURE_TEST_CASE(blockmanager_block_data_part_error, TestChain100Setup)
{
    LOCK(::cs_main);
    auto& chainman{m_node.chainman};
    auto& blockman{chainman->m_blockman};
    const CBlockIndex& tip{*chainman->ActiveTip()};
    const FlatFilePos tip_block_pos{tip.GetBlockPos()};

    auto block{blockman.ReadRawBlock(tip_block_pos)};
    BOOST_REQUIRE(block);
    BOOST_REQUIRE_GE(block->size(), 200);

    const auto expect_part_error{[&](size_t offset, size_t size) {
        auto res{blockman.ReadRawBlock(tip_block_pos, std::pair{offset, size})};
        BOOST_CHECK(!res);
        BOOST_CHECK_EQUAL(res.error(), node::ReadRawError::BadPartRange);
    }};

    expect_part_error(0, 0);
    expect_part_error(0, block->size() + 1);
    expect_part_error(0, std::numeric_limits<size_t>::max());
    expect_part_error(1, block->size());
    expect_part_error(2, block->size() - 1);
    expect_part_error(block->size() - 1, 2);
    expect_part_error(block->size() - 2, 3);
    expect_part_error(block->size() + 1, 0);
    expect_part_error(block->size() + 1, 1);
    expect_part_error(block->size() + 2, 2);
    expect_part_error(block->size(), 0);
    expect_part_error(block->size(), 1);
    expect_part_error(std::numeric_limits<size_t>::max(), 1);
    expect_part_error(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max());
}

BOOST_FIXTURE_TEST_CASE(blockmanager_readblock_hash_mismatch, TestingSetup)
{
    CBlockIndex index;
    {
        LOCK(cs_main);
        const auto tip{m_node.chainman->ActiveTip()};
        index.nStatus = tip->nStatus;
        index.nDataPos = tip->nDataPos;
        index.phashBlock = &uint256::ONE; // mismatched block hash
    }

    ASSERT_DEBUG_LOG("GetHash() doesn't match index");
    CBlock block;
    BOOST_CHECK(!m_node.chainman->m_blockman.ReadBlock(block, index));
}

BOOST_AUTO_TEST_CASE(blockmanager_flush_block_file)
{
    KernelNotifications notifications{Assert(m_node.shutdown_request), m_node.exit_status, *Assert(m_node.warnings)};
    node::BlockManager::Options blockman_opts{
        .chainparams = Params(),
        .blocks_dir = m_args.GetBlocksDirPath(),
        .notifications = notifications,
        .block_tree_db_params = DBParams{
            .path = m_args.GetDataDirNet() / "blocks" / "index",
            .cache_bytes = 0,
        },
    };
    struct DirtyBlockManager : BlockManager {
        using BlockManager::BlockManager;
        using BlockManager::m_dirty_blockindex;
    };
    DirtyBlockManager blockman{*Assert(m_node.shutdown_signal), blockman_opts};

    // Test blocks with no transactions, not even a coinbase
    CBlock block1;
    block1.nVersion = 1;
    CBlock block2;
    block2.nVersion = 2;
    CBlock block3;
    block3.nVersion = 3;

    // They are 80 bytes header + 1 byte 0x00 for vtx length
    constexpr int TEST_BLOCK_SIZE{81};

    // Blockstore is empty
    BOOST_CHECK_EQUAL(blockman.CalculateCurrentUsage(), 0);

    // Write the first block to a new location.
    FlatFilePos pos1{blockman.WriteBlock(block1, /*nHeight=*/1)};

    // Write second block
    FlatFilePos pos2{blockman.WriteBlock(block2, /*nHeight=*/2)};

    // Two blocks in the file
    BOOST_CHECK_EQUAL(blockman.CalculateCurrentUsage(), (TEST_BLOCK_SIZE + STORAGE_HEADER_BYTES) * 2);

    // First two blocks are written as expected
    // Errors are expected because block data is junk, thrown AFTER successful read
    CBlock read_block;
    BOOST_CHECK_EQUAL(read_block.nVersion, 0);
    {
        ASSERT_DEBUG_LOG("Errors in block header");
        BOOST_CHECK(!blockman.ReadBlock(read_block, pos1, {}));
        BOOST_CHECK_EQUAL(read_block.nVersion, 1);
    }
    {
        ASSERT_DEBUG_LOG("Errors in block header");
        BOOST_CHECK(!blockman.ReadBlock(read_block, pos2, {}));
        BOOST_CHECK_EQUAL(read_block.nVersion, 2);
    }

    // During reindex, the flat file block storage will not be written to.
    // UpdateBlockInfo will, however, update the blockfile metadata.
    // Verify this behavior by attempting (and failing) to write block 3 data
    // to block 2 location.
    CBlockFileInfo* block_data = blockman.GetBlockFileInfo(0);
    BOOST_CHECK_EQUAL(block_data->nBlocks, 2);
    blockman.UpdateBlockInfo(block3, /*nHeight=*/3, /*pos=*/pos2);
    // Metadata is updated...
    BOOST_CHECK_EQUAL(block_data->nBlocks, 3);
    // ...but there are still only two blocks in the file
    BOOST_CHECK_EQUAL(blockman.CalculateCurrentUsage(), (TEST_BLOCK_SIZE + STORAGE_HEADER_BYTES) * 2);

    // Block 2 was not overwritten:
    BOOST_CHECK(!blockman.ReadBlock(read_block, pos2, {}));
    BOOST_CHECK_EQUAL(read_block.nVersion, 2);

    // Exercise real index persistence across pool release and subsequent reuse.
    // AddToBlockIndex alone does not establish genuine Yespower evidence.
    LOCK(cs_main);
    CBlockIndex* best_header{nullptr};
    CBlockHeader header{Params().GenesisBlock()};
    std::vector<std::pair<uint256, CBlockIndex*>> saved;
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int i = 0; i < 1000; ++i) {
            if (!saved.empty()) header.hashPrevBlock = saved.back().first;
            ++header.nNonce;
            auto* index{blockman.AddToBlockIndex(header, best_header)};
            const auto dirty_size{blockman.m_dirty_blockindex.size()};
            BOOST_CHECK(blockman.AddToBlockIndex(header, best_header) == index);
            BOOST_CHECK_EQUAL(blockman.m_dirty_blockindex.size(), dirty_size);
            BOOST_CHECK(!index->HasCurrentProcessYespowerEvidence());
            saved.emplace_back(header.GetHash(), index);
        }
        BOOST_CHECK_EQUAL(blockman.m_dirty_blockindex.size(), 1000U);
        blockman.WriteBlockIndexDB();
        BOOST_CHECK(blockman.m_dirty_blockindex.empty());
        // Shutdown can flush the index twice without any intervening changes.
        blockman.WriteBlockIndexDB();
        BOOST_CHECK(blockman.m_dirty_blockindex.empty());
        blockman.WriteBlockIndexDB(); // Releasing an already-empty pool is safe.
    }
    // Reopen the persisted DB after all dirty set nodes have been freed.
    blockman.m_block_tree_db.reset();
    blockman.m_block_tree_db = std::make_unique<kernel::BlockTreeDB>(blockman_opts.block_tree_db_params);
    for (const auto& [hash, index] : saved) {
        CDiskBlockIndex disk;
        BOOST_REQUIRE(blockman.m_block_tree_db->Read(std::make_pair(uint8_t{'b'}, hash), disk));
        BOOST_CHECK(disk.ConstructBlockHash() == hash);
        BOOST_CHECK_EQUAL(disk.nHeight, index->nHeight);
        BOOST_CHECK_EQUAL(disk.nStatus, index->nStatus);
        BOOST_CHECK(disk.hashPrev == (index->pprev ? index->pprev->GetBlockHash() : uint256{}));
        BOOST_CHECK(!blockman.m_block_tree_db->HasYespowerEvidence(index->GetBlockHeader(), Params().GetConsensus()));
    }
}

#if defined(__APPLE__) && defined(__x86_64__)
BOOST_FIXTURE_TEST_CASE(buffered_block_read_visibility, BufferedBlockStorageSetup)
{
    CBlock block;
    block.nVersion = 7;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    BOOST_REQUIRE_GT(node::BlockManagerTestAccess::Pending(blockman), 0U);
    // The independent indexed reader must see the pending, XOR-obfuscated record.
    auto file{blockman.OpenBlockFile(first, true)};
    BOOST_REQUIRE(!file.IsNull());
    CBlock read;
    file >> TX_WITH_WITNESS(read);
    BOOST_CHECK_EQUAL(read.nVersion, 7);
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::Pending(blockman), 0U);
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
    block.nVersion = 8;
    const auto second{blockman.WriteBlock(block, 2)};
    BOOST_REQUIRE_GT(node::BlockManagerTestAccess::Pending(blockman), 0U);
    // This also exercises a previously cached positional read handle.
    const auto raw{blockman.ReadRawBlock(second)};
    BOOST_REQUIRE(raw);
    SpanReader{*raw} >> TX_WITH_WITNESS(read);
    BOOST_CHECK_EQUAL(read.nVersion, 8);
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::Pending(blockman), 0U);
}

BOOST_FIXTURE_TEST_CASE(buffered_undo_read_visibility, BufferedBlockStorageSetup)
{
    LOCK(cs_main);
    CBlock block;
    const auto pos{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!pos.IsNull());
    const auto previous_hash{Params().GenesisBlock().GetHash()};
    CBlockIndex previous;
    previous.phashBlock = &previous_hash;
    CBlockIndex index;
    index.pprev = &previous;
    index.nFile = pos.nFile;
    index.nHeight = 1;
    CBlockUndo undo;
    undo.vtxundo.emplace_back();
    BlockValidationState state;
    BOOST_REQUIRE(node::BlockManagerTestAccess::WriteUndo(blockman, undo, state, index));
    BOOST_REQUIRE_GT(node::BlockManagerTestAccess::Pending(blockman, true), 0U);
    CBlockUndo read;
    BOOST_REQUIRE(blockman.ReadBlockUndo(read, index));
    BOOST_CHECK_EQUAL(read.vtxundo.size(), undo.vtxundo.size());
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::Pending(blockman, true), 0U);
}

BOOST_FIXTURE_TEST_CASE(buffered_index_failure_retains_dirty_state, BufferedBlockStorageSetup)
{
    LOCK(cs_main);
    CBlock block;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    blockman.WriteBlockIndexDB();
    CBlockFileInfo checkpoint;
    BOOST_REQUIRE(blockman.m_block_tree_db->ReadBlockFileInfo(first.nFile, checkpoint));
    BOOST_CHECK_EQUAL(checkpoint.nBlocks, 1U);
    BOOST_REQUIRE(!blockman.WriteBlock(block, 2).IsNull());
    CBlockIndex* best_header{nullptr};
    blockman.AddToBlockIndex(Params().GenesisBlock(), best_header);
    const auto dirty_indexes{node::BlockManagerTestAccess::DirtyIndexes(blockman)};
    BOOST_REQUIRE_GT(dirty_indexes, 0U);
    const auto dirty_files{node::BlockManagerTestAccess::DirtyFiles(blockman)};
    BOOST_REQUIRE_GT(dirty_files, 0U);
    node::BlockManagerTestAccess::MakeReadOnly(blockman, blockman.GetBlockPosFilename(first));
    BOOST_CHECK_THROW(blockman.WriteBlockIndexDB(), std::runtime_error);
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::DirtyFiles(blockman), dirty_files);
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::DirtyIndexes(blockman), dirty_indexes);
    CBlockFileInfo persisted;
    BOOST_REQUIRE(blockman.m_block_tree_db->ReadBlockFileInfo(first.nFile, persisted));
    BOOST_CHECK_EQUAL(persisted.nBlocks, checkpoint.nBlocks);
    BOOST_CHECK_EQUAL(persisted.nSize, checkpoint.nSize);
    BOOST_CHECK(!node::BlockManagerTestAccess::Close(blockman));
    // A closed stream, another file number, and a retry must not hide the failure.
    BOOST_CHECK(!node::BlockManagerTestAccess::Close(blockman, first.nFile + 1));
    BOOST_CHECK_THROW(blockman.WriteBlockIndexDB(), std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(buffered_write_exception_is_sticky, LargeFileBufferedBlockStorageSetup)
{
    CBlock block;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    node::BlockManagerTestAccess::MakeReadOnly(blockman, blockman.GetBlockPosFilename(first));
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(128 * 1024);
    block.vtx.push_back(MakeTransactionRef(tx));
    // A large explicit serialization flush fails before Keep(), without rotating.
    BOOST_CHECK_THROW(blockman.WriteBlock(block, 2), std::ios_base::failure);
    LOCK(cs_main);
    BOOST_CHECK_THROW(blockman.WriteBlockIndexDB(), std::runtime_error);
    BOOST_CHECK(!node::BlockManagerTestAccess::Close(blockman));
}

BOOST_FIXTURE_TEST_CASE(buffered_large_record_is_readable, LargeFileBufferedBlockStorageSetup)
{
    CBlock block;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(96 * 1024);
    block.vtx.push_back(MakeTransactionRef(tx));
    const auto pos{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!pos.IsNull());
    BOOST_CHECK_EQUAL(node::BlockManagerTestAccess::Pending(blockman), 0U);
    const auto raw{blockman.ReadRawBlock(pos)};
    BOOST_REQUIRE(raw);
    CBlock read;
    SpanReader{*raw} >> TX_WITH_WITNESS(read);
    BOOST_REQUIRE_EQUAL(read.vtx.size(), 1U);
    BOOST_CHECK(read.vtx[0]->GetHash() == block.vtx[0]->GetHash());
}

BOOST_FIXTURE_TEST_CASE(buffered_rotation_and_pruning, BufferedBlockStorageSetup)
{
    CBlock block;
    block.nVersion = 9;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    blockman.GetBlockFileInfo(first.nFile)->nSize = 0x10000;
    const auto second{blockman.WriteBlock(block, 2)};
    BOOST_REQUIRE(!second.IsNull());
    BOOST_CHECK_NE(first.nFile, second.nFile);
    BOOST_REQUIRE(blockman.ReadRawBlock(first));
    BOOST_REQUIRE(blockman.ReadRawBlock(second));
    blockman.UnlinkPrunedFiles({first.nFile});
    BOOST_CHECK(!fs::exists(blockman.GetBlockPosFilename(first)));
    BOOST_CHECK(fs::exists(blockman.GetBlockPosFilename(second)));
}

BOOST_FIXTURE_TEST_CASE(buffered_rotation_failure_is_sticky, BufferedBlockStorageSetup)
{
    CBlock block;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    node::BlockManagerTestAccess::MakeReadOnly(blockman, blockman.GetBlockPosFilename(first));
    blockman.GetBlockFileInfo(first.nFile)->nSize = 0x10000;
    BOOST_CHECK(blockman.WriteBlock(block, 2).IsNull());
    LOCK(cs_main);
    BOOST_CHECK_THROW(blockman.WriteBlockIndexDB(), std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(buffered_pruning_failure_aborts, BufferedBlockStorageSetup)
{
    CBlock block;
    const auto first{blockman.WriteBlock(block, 1)};
    BOOST_REQUIRE(!first.IsNull());
    const auto path{blockman.GetBlockPosFilename(first)};
    node::BlockManagerTestAccess::MakeReadOnly(blockman, path);
    BOOST_CHECK_THROW(blockman.UnlinkPrunedFiles({first.nFile}), std::runtime_error);
    BOOST_CHECK(fs::exists(path));
    LOCK(cs_main);
    BOOST_CHECK_THROW(blockman.WriteBlockIndexDB(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(buffered_serialization_failure_is_not_retried)
{
    struct FailingWriter {
        unsigned attempts{0};
        void write_buffer(std::span<std::byte>)
        {
            ++attempts;
            throw std::ios_base::failure("injected write failure");
        }
    } writer;
    const auto attempt{[&] {
        BufferedWriter buffer{writer, 1};
        buffer << uint8_t{42};
    }};
    BOOST_CHECK_THROW(attempt(), std::ios_base::failure);
    BOOST_CHECK_EQUAL(writer.attempts, 1U);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
