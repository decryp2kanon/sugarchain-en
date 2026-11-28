// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <headerssync.h>
#include <net_processing.h>
#include <pow.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/time.h>
#include <validation.h>

#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

using State = HeadersSyncState::State;

// Standard set of checks common to all scenarios. Macro keeps failure lines at the call-site.
#define CHECK_RESULT(result_expression, hss, exp_state, exp_success, exp_request_more,                   \
                     exp_headers_size, exp_pow_validated_prev, exp_locator_hash)                         \
    do {                                                                                                 \
        const auto result{result_expression};                                                            \
        BOOST_REQUIRE_EQUAL(hss.GetState(), exp_state);                                                  \
        BOOST_CHECK_EQUAL(result.success, exp_success);                                                  \
        BOOST_CHECK_EQUAL(result.request_more, exp_request_more);                                        \
        BOOST_CHECK_EQUAL(result.pow_validated_headers.size(), exp_headers_size);                        \
        const std::optional<uint256> pow_validated_prev_opt{exp_pow_validated_prev};                     \
        if (pow_validated_prev_opt) {                                                                    \
            BOOST_CHECK_EQUAL(result.pow_validated_headers.at(0).hashPrevBlock, pow_validated_prev_opt); \
        } else {                                                                                         \
            BOOST_CHECK_EQUAL(exp_headers_size, 0);                                                      \
        }                                                                                                \
        const std::optional<uint256> locator_hash_opt{exp_locator_hash};                                 \
        if (locator_hash_opt) {                                                                          \
            BOOST_CHECK_EQUAL(hss.NextHeadersRequestLocator().vHave.at(0), locator_hash_opt);            \
        } else {                                                                                         \
            BOOST_CHECK_EQUAL(exp_state, State::FINAL);                                                  \
        }                                                                                                \
    } while (false)

constexpr size_t TARGET_BLOCKS{15'000};
constexpr arith_uint256 CHAIN_WORK{TARGET_BLOCKS * 2};

// Subtract MAX_HEADERS_RESULTS (2000 headers/message) + an arbitrary smaller
// value (123) so our redownload buffer is well below the number of blocks
// required to reach the CHAIN_WORK threshold, to behave similarly to mainnet.
constexpr size_t REDOWNLOAD_BUFFER_SIZE{TARGET_BLOCKS - (MAX_HEADERS_RESULTS + 123)};
constexpr size_t COMMITMENT_PERIOD{600}; // Somewhat close to mainnet.

struct HeadersGeneratorSetup : public RegTestingSetup {
    const CBlock& genesis{Params().GenesisBlock()};
    CBlockIndex& chain_start{WITH_LOCK(::cs_main, return *Assert(m_node.chainman->m_blockman.LookupBlockIndex(genesis.GetHash())))};

    // Generate headers for two different chains (using differing merkle roots
    // to ensure the headers are different).
    const std::vector<CBlockHeader>& FirstChain()
    {
        // Block header hash target is half of max uint256 (2**256 / 2), expressible
        // roughly as the coefficient 0x7fffff with the exponent 0x20 (32 bytes).
        // This implies around every 2nd hash attempt should succeed, which
        // is why CHAIN_WORK == TARGET_BLOCKS * 2.
        assert(genesis.nBits == 0x207fffff);

        // Subtract 1 since the genesis block also contributes work so we reach
        // the CHAIN_WORK target.
        static const auto first_chain{GenerateHeaders(/*count=*/TARGET_BLOCKS - 1, genesis.GetHash(),
                genesis.nVersion, genesis.nTime, /*merkle_root=*/uint256::ZERO, genesis.nBits)};
        return first_chain;
    }
    const std::vector<CBlockHeader>& SecondChain()
    {
        // Subtract 2 to keep total work below the target.
        static const auto second_chain{GenerateHeaders(/*count=*/TARGET_BLOCKS - 2, genesis.GetHash(),
                genesis.nVersion, genesis.nTime, /*merkle_root=*/uint256::ONE, genesis.nBits)};
        return second_chain;
    }

    HeadersSyncState CreateState()
    {
        return {/*id=*/0,
                Params().GetConsensus(),
                HeadersSyncParams{
                    .commitment_period = COMMITMENT_PERIOD,
                    .redownload_buffer_size = REDOWNLOAD_BUFFER_SIZE,
                },
                chain_start,
                /*minimum_required_work=*/CHAIN_WORK};
    }

private:
    /** Search for a nonce to meet (regtest) proof of work */
    void FindProofOfWork(CBlockHeader& starting_header);
    /**
     * Generate headers in a chain that build off a given starting hash, using
     * the given nVersion, advancing time by 1 second from the starting
     * prev_time, and with a fixed merkle root hash.
     */
    std::vector<CBlockHeader> GenerateHeaders(size_t count,
            uint256 prev_hash, int32_t nVersion, uint32_t prev_time,
            const uint256& merkle_root, uint32_t nBits);
};

void HeadersGeneratorSetup::FindProofOfWork(CBlockHeader& starting_header)
{
    while (!CheckProofOfWork(starting_header.GetHash(), starting_header.nBits, Params().GetConsensus())) {
        ++starting_header.nNonce;
    }
}

std::vector<CBlockHeader> HeadersGeneratorSetup::GenerateHeaders(
        const size_t count, uint256 prev_hash, const int32_t nVersion,
        uint32_t prev_time, const uint256& merkle_root, const uint32_t nBits)
{
    std::vector<CBlockHeader> headers(count);
    for (auto& next_header : headers) {
        next_header.nVersion = nVersion;
        next_header.hashPrevBlock = prev_hash;
        next_header.hashMerkleRoot = merkle_root;
        next_header.nTime = ++prev_time;
        next_header.nBits = nBits;

        FindProofOfWork(next_header);
        prev_hash = next_header.GetHash();
    }
    return headers;
}

// In this test, we construct two sets of headers from genesis, one with
// sufficient proof of work and one without.
// 1. We deliver the first set of headers and verify that the headers sync state
//    updates to the REDOWNLOAD phase successfully.
//    Then we deliver the second set of headers and verify that they fail
//    processing (presumably due to commitments not matching).
// 2. Verify that repeating with the first set of headers in both phases is
//    successful.
// 3. Repeat the second set of headers in both phases to demonstrate behavior
//    when the chain a peer provides has too little work.
BOOST_FIXTURE_TEST_SUITE(headers_sync_chainwork_tests, HeadersGeneratorSetup)

BOOST_AUTO_TEST_CASE(initial_headers_locator_aligns_shared_low_work_sync)
{
    const auto mainnet{CChainParams::Main()};
    const auto testnet{CChainParams::TestNet()};
    const auto testnet4{CChainParams::TestNet4()};
    const auto signet{CChainParams::SigNet({})};
    constexpr arith_uint256 minimum_work{100};
    std::vector<CBlockIndex> index(2783);
    for (size_t height{0}; height < index.size(); ++height) {
        auto& entry{index[height]};
        entry.nHeight = static_cast<int>(height);
        entry.nChainWork = minimum_work - 1;
        if (height > 0) entry.pprev = &index[height - 1];
        entry.BuildSkip();
    }

    struct Start { int best; int parent; int aligned; };
    for (const auto& [best, parent, aligned] : {Start{0, 0, 0}, Start{1, 0, 0}, Start{1999, 1998, 0},
            Start{2000, 1999, 0}, Start{2001, 2000, 2000}, Start{2782, 2781, 2000}}) {
        BOOST_TEST_CONTEXT("best header height=" << best) {
            const auto choose = [&](const CChainParams& network, uint32_t size, bool align, bool regtest_opt_in = false) {
                return &GetInitialHeadersSyncStart(index[parent], index[best], network, minimum_work, size, align, regtest_opt_in);
            };
            BOOST_CHECK_EQUAL(choose(*mainnet, MAX_HEADERS_RESULTS, true), &index[aligned]);
            BOOST_CHECK_EQUAL(choose(*mainnet, MAX_HEADERS_RESULTS, false), &index[parent]);
            BOOST_CHECK_EQUAL(choose(*testnet, MAX_HEADERS_RESULTS, true), &index[aligned]);
            BOOST_CHECK_EQUAL(choose(*testnet, MAX_HEADERS_RESULTS, false), &index[parent]);
            BOOST_CHECK_EQUAL(choose(*testnet4, MAX_HEADERS_RESULTS, true, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(*signet, MAX_HEADERS_RESULTS, true, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(Params(), MAX_HEADERS_RESULTS, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(Params(), MAX_HEADERS_RESULTS, true, true), &index[aligned]);
            BOOST_CHECK_EQUAL(choose(Params(), MAX_HEADERS_RESULTS, false, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(*mainnet, 0, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(*mainnet, 1, true), &index[parent]);
            index[best].nChainWork = minimum_work;
            BOOST_CHECK_EQUAL(choose(*mainnet, MAX_HEADERS_RESULTS, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(*testnet, MAX_HEADERS_RESULTS, true), &index[parent]);
            BOOST_CHECK_EQUAL(choose(Params(), MAX_HEADERS_RESULTS, true, true), &index[parent]);
            index[best].nChainWork = minimum_work - 1;
        }
    }
    BOOST_CHECK_EQUAL(&GetInitialHeadersSyncStart(index[2781], index.back(), *mainnet, minimum_work, 997, true), &index[1994]);

    // Announcement and unconnecting-header requests ordinarily start at the
    // best header itself. Their alignment keeps an exact boundary in place.
    for (const auto& [best, aligned] : {std::pair{0, 0}, {782, 0}, {1999, 0}, {2000, 2000}, {2782, 2000}}) {
        const auto choose = [&](const CChainParams& network, bool can_align) {
            return &GetInitialHeadersSyncStart(index[best], index[best], network, minimum_work, MAX_HEADERS_RESULTS, can_align);
        };
        BOOST_CHECK_EQUAL(choose(*mainnet, true), &index[aligned]);
        BOOST_CHECK_EQUAL(choose(*mainnet, false), &index[best]);
        BOOST_CHECK_EQUAL(choose(*testnet, true), &index[aligned]);
    }
}

BOOST_AUTO_TEST_CASE(initial_headers_locator_keeps_aligned_anchor_on_best_fork)
{
    const auto mainnet{CChainParams::Main()};
    constexpr arith_uint256 minimum_work{100};
    std::vector<CBlockIndex> trunk(2783);
    std::vector<CBlockIndex> fork(trunk.size() - 1999);
    for (size_t height{0}; height < trunk.size(); ++height) {
        auto& entry{trunk[height]};
        entry.nHeight = static_cast<int>(height);
        entry.nChainWork = minimum_work - 1;
        if (height > 0) entry.pprev = &trunk[height - 1];
        entry.BuildSkip();
    }
    auto* previous{&trunk[1998]};
    for (auto& entry : fork) {
        entry.nHeight = previous->nHeight + 1;
        entry.nChainWork = minimum_work - 1;
        entry.pprev = previous;
        entry.BuildSkip();
        previous = &entry;
    }

    const auto& anchor{GetInitialHeadersSyncStart(*fork.back().pprev, fork.back(), *mainnet, minimum_work, MAX_HEADERS_RESULTS, true)};
    BOOST_CHECK_EQUAL(anchor.nHeight, 2000);
    BOOST_CHECK_EQUAL(&anchor, &fork[1]);
    BOOST_CHECK_NE(&anchor, &trunk[2000]);
    BOOST_CHECK_EQUAL(&GetInitialHeadersSyncStart(fork[0], fork[1], *mainnet, minimum_work, MAX_HEADERS_RESULTS, true), &trunk[0]);
    BOOST_CHECK_EQUAL(&GetInitialHeadersSyncStart(fork.back(), fork.back(), *mainnet, minimum_work, MAX_HEADERS_RESULTS, true), &fork[1]);
}

BOOST_AUTO_TEST_CASE(advancing_presync_preserves_network_wait_allowance)
{
    struct ResetClock {
        ~ResetClock() { MockableSteadyClock::ClearMockTime(); }
    } reset_clock;
    const auto now = [] {
        return std::chrono::duration_cast<std::chrono::microseconds>(MockableSteadyClock::now().time_since_epoch());
    };
    MockableSteadyClock::SetMockTime(1s);
    auto deadline{now() + 4s};
    std::chrono::microseconds remaining_credit{2s};

    auto state{CreateState()};
    const std::span chain{FirstChain()};
    BOOST_REQUIRE(state.ProcessNextHeaders(chain.first(1), /*full_headers_message=*/true).request_more);
    const auto previous_height{state.GetPresyncHeight()};

    MockableSteadyClock::SetMockTime(4500ms);
    const auto start{now()};
    MockableSteadyClock::SetMockTime(5500ms);
    BOOST_CHECK(now() > deadline);
    const auto advanced{state.ProcessNextHeaders(chain.subspan(1, 1), /*full_headers_message=*/true)};
    BOOST_REQUIRE(advanced.success && advanced.request_more);
    state.AccountForLocalPresyncProofTime(previous_height, now() - start, deadline, remaining_credit);
    BOOST_CHECK(deadline - now() == 500ms);
    BOOST_CHECK(remaining_credit == 1s);

    MockableSteadyClock::SetMockTime(6001ms);
    BOOST_CHECK(now() > deadline);
}

BOOST_AUTO_TEST_CASE(presync_lead_preview_preserves_state_and_work_floor)
{
    auto state{CreateState()};
    const std::span chain{FirstChain()};
    constexpr size_t group{MAX_HEADERS_RESULTS};
    BOOST_REQUIRE(state.ProcessNextHeaders(chain.first(group), /*full_headers_message=*/true).request_more);
    const auto before_work{state.GetPresyncWork()};
    const auto before_height{state.GetPresyncHeight()};
    const auto before_locator{state.NextHeadersRequestLocator()};
    const auto next{chain.subspan(group, group)};

    BOOST_CHECK(!state.PreviewPresyncLead({}, /*full_headers_message=*/true));
    BOOST_CHECK(!state.PreviewPresyncLead(next, /*full_headers_message=*/false));
    BOOST_CHECK(!state.PreviewPresyncLead(chain.subspan(group + 1, group), /*full_headers_message=*/true));
    auto corrupt{std::vector<CBlockHeader>{next.begin(), next.end()}};
    corrupt.back().hashPrevBlock.SetNull();
    BOOST_CHECK(!state.PreviewPresyncLead(corrupt, /*full_headers_message=*/true));
    corrupt.back() = next.back();
    corrupt.back().nBits = 0;
    BOOST_CHECK(!state.PreviewPresyncLead(corrupt, /*full_headers_message=*/true));

    const auto preview{state.PreviewPresyncLead(next, /*full_headers_message=*/true)};
    BOOST_REQUIRE(preview);
    BOOST_CHECK_EQUAL(preview->vHave.front(), next.back().GetHash());
    BOOST_CHECK_EQUAL(state.GetPresyncHeight(), before_height);
    BOOST_CHECK_EQUAL(state.GetPresyncWork().GetHex(), before_work.GetHex());
    BOOST_CHECK(state.NextHeadersRequestLocator().vHave == before_locator.vHave);
    BOOST_REQUIRE(state.ProcessNextHeaders(next, /*full_headers_message=*/true).request_more);
    BOOST_CHECK(state.NextHeadersRequestLocator().vHave == preview->vHave);

    const auto remainder{chain.subspan(2 * group)};
    BOOST_CHECK(!state.PreviewPresyncLead(remainder, /*full_headers_message=*/true));
    BOOST_REQUIRE(state.ProcessNextHeaders(remainder, /*full_headers_message=*/true).request_more);
    BOOST_CHECK(state.GetState() == State::REDOWNLOAD);
    BOOST_CHECK_EQUAL(state.NextHeadersRequestLocator().vHave.front(), chain_start.GetBlockHash());
    BOOST_CHECK(!state.PreviewPresyncLead(next, /*full_headers_message=*/true));
}

BOOST_AUTO_TEST_CASE(presync_proof_credit_requires_live_advancement)
{
    auto state{CreateState()};
    const std::span chain{SecondChain()};
    BOOST_REQUIRE(state.ProcessNextHeaders(chain.first(1), /*full_headers_message=*/true).request_more);
    const auto previous_height{state.GetPresyncHeight()};
    std::chrono::microseconds deadline{10s};
    std::chrono::microseconds remaining_credit{2s};

    state.AccountForLocalPresyncProofTime(previous_height, 1s, deadline, remaining_credit);
    BOOST_CHECK(deadline == 10s);
    BOOST_CHECK(remaining_credit == 2s);

    const auto invalid{state.ProcessNextHeaders(chain.first(1), /*full_headers_message=*/true)};
    BOOST_CHECK(!invalid.success);
    state.AccountForLocalPresyncProofTime(previous_height, 1s, deadline, remaining_credit);
    BOOST_CHECK(deadline == 10s);
    BOOST_CHECK(remaining_credit == 2s);

    auto incomplete_state{CreateState()};
    BOOST_REQUIRE(incomplete_state.ProcessNextHeaders(chain.first(1), /*full_headers_message=*/true).request_more);
    const auto incomplete_before{incomplete_state.GetPresyncHeight()};
    const auto incomplete{incomplete_state.ProcessNextHeaders(chain.subspan(1, 1), /*full_headers_message=*/false)};
    BOOST_CHECK(incomplete.success && !incomplete.request_more);
    incomplete_state.AccountForLocalPresyncProofTime(incomplete_before, 1s, deadline, remaining_credit);
    BOOST_CHECK(deadline == 10s);
    BOOST_CHECK(remaining_credit == 2s);
}

BOOST_AUTO_TEST_CASE(presync_proof_credit_is_bounded_and_keeps_deadline_sentinels)
{
    auto state{CreateState()};
    const std::span chain{FirstChain()};
    BOOST_REQUIRE(state.ProcessNextHeaders(chain.first(1), /*full_headers_message=*/true).request_more);
    const auto initial_height{state.GetPresyncHeight()};
    BOOST_REQUIRE(state.ProcessNextHeaders(chain.subspan(1, 1), /*full_headers_message=*/true).request_more);
    std::chrono::microseconds deadline{10s};
    std::chrono::microseconds remaining_credit{2s};
    state.AccountForLocalPresyncProofTime(initial_height, 1s, deadline, remaining_credit);
    BOOST_CHECK(deadline == 11s);
    BOOST_CHECK(remaining_credit == 1s);

    const auto previous_height{state.GetPresyncHeight()};
    const auto transition{state.ProcessNextHeaders(chain.subspan(2), /*full_headers_message=*/false)};
    BOOST_REQUIRE(transition.success && transition.request_more);
    BOOST_CHECK(state.GetState() == State::REDOWNLOAD);
    state.AccountForLocalPresyncProofTime(previous_height, -1us, deadline, remaining_credit);
    BOOST_CHECK(deadline == 11s);
    state.AccountForLocalPresyncProofTime(previous_height, 3s, deadline, remaining_credit);
    BOOST_CHECK(deadline == 12s);
    BOOST_CHECK(remaining_credit == 0us);
    state.AccountForLocalPresyncProofTime(previous_height, 3s, deadline, remaining_credit);
    BOOST_CHECK(deadline == 12s);

    constexpr auto disabled{std::chrono::microseconds::max()};
    deadline = disabled - 2us;
    remaining_credit = 3us;
    state.AccountForLocalPresyncProofTime(previous_height, 3us, deadline, remaining_credit);
    BOOST_CHECK(deadline == disabled - 1us);
    BOOST_CHECK(remaining_credit == 2us);

    deadline = disabled;
    state.AccountForLocalPresyncProofTime(previous_height, 3us, deadline, remaining_credit);
    BOOST_CHECK(deadline == disabled);
    BOOST_CHECK(remaining_credit == 2us);
    deadline = 0us;
    state.AccountForLocalPresyncProofTime(previous_height, 3us, deadline, remaining_credit);
    BOOST_CHECK(deadline == 0us);
    BOOST_CHECK(remaining_credit == 2us);
}

BOOST_AUTO_TEST_CASE(sneaky_redownload)
{
    const auto& first_chain{FirstChain()};
    const auto& second_chain{SecondChain()};

    // Feed the first chain to HeadersSyncState, by delivering 1 header
    // initially and then the rest.
    HeadersSyncState hss{CreateState()};

    // Just feed one header and check state.
    // Pretend the message is still "full", so we don't abort.
    CHECK_RESULT(hss.ProcessNextHeaders({{first_chain.front()}}, /*full_headers_message=*/true),
        hss, /*exp_state=*/State::PRESYNC,
        /*exp_success=*/true, /*exp_request_more=*/true,
        /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
        /*exp_locator_hash=*/first_chain.front().GetHash());

    // This chain should look valid, and we should have met the proof-of-work
    // requirement during PRESYNC and transitioned to REDOWNLOAD.
    CHECK_RESULT(hss.ProcessNextHeaders(std::span{first_chain}.subspan(1), true),
        hss, /*exp_state=*/State::REDOWNLOAD,
        /*exp_success=*/true, /*exp_request_more=*/true,
        /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
        /*exp_locator_hash=*/genesis.GetHash());

    // Below is the number of commitment bits that must randomly match between
    // the two chains for this test to spuriously fail. 1 / 2^25 =
    // 1 in 33'554'432 (somewhat less due to HeadersSyncState::m_commit_offset).
    static_assert(TARGET_BLOCKS / COMMITMENT_PERIOD == 25);

    // Try to sneakily feed back the second chain during REDOWNLOAD.
    CHECK_RESULT(hss.ProcessNextHeaders(second_chain, true),
        hss, /*exp_state=*/State::FINAL,
        /*exp_success=*/false, // Foiled! We detected mismatching headers.
        /*exp_request_more=*/false,
        /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
        /*exp_locator_hash=*/std::nullopt);
}

BOOST_AUTO_TEST_CASE(happy_path)
{
    const auto& first_chain{FirstChain()};

    // Headers message that moves us to the next state doesn't need to be full.
    for (const bool full_headers_message : {false, true}) {
        // This time we feed the first chain twice.
        HeadersSyncState hss{CreateState()};

        // Sufficient work transitions us from PRESYNC to REDOWNLOAD:
        const auto genesis_hash{genesis.GetHash()};
        CHECK_RESULT(hss.ProcessNextHeaders(first_chain, full_headers_message),
            hss, /*exp_state=*/State::REDOWNLOAD,
            /*exp_success=*/true, /*exp_request_more=*/true,
            /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
            /*exp_locator_hash=*/genesis_hash);

        // Process only so that the internal threshold isn't exceeded, meaning
        // validated headers shouldn't be returned yet:
        CHECK_RESULT(hss.ProcessNextHeaders({first_chain.begin(), REDOWNLOAD_BUFFER_SIZE}, true),
            hss, /*exp_state=*/State::REDOWNLOAD,
            /*exp_success=*/true, /*exp_request_more=*/true,
            /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
            /*exp_locator_hash=*/first_chain[REDOWNLOAD_BUFFER_SIZE - 1].GetHash());

        // We start receiving headers for permanent storage before completing:
        CHECK_RESULT(hss.ProcessNextHeaders({{first_chain[REDOWNLOAD_BUFFER_SIZE]}}, true),
            hss, /*exp_state=*/State::REDOWNLOAD,
            /*exp_success=*/true, /*exp_request_more=*/true,
            /*exp_headers_size=*/1, /*exp_pow_validated_prev=*/genesis_hash,
            /*exp_locator_hash=*/first_chain[REDOWNLOAD_BUFFER_SIZE].GetHash());

        // Feed in remaining headers, meeting the work threshold again and
        // completing the REDOWNLOAD phase:
        CHECK_RESULT(hss.ProcessNextHeaders({first_chain.begin() + REDOWNLOAD_BUFFER_SIZE + 1, first_chain.end()}, full_headers_message),
            hss, /*exp_state=*/State::FINAL,
            /*exp_success=*/true, /*exp_request_more=*/false,
            // All headers except the one already returned above:
            /*exp_headers_size=*/first_chain.size() - 1, /*exp_pow_validated_prev=*/first_chain.front().GetHash(),
            /*exp_locator_hash=*/std::nullopt);
    }
}

BOOST_AUTO_TEST_CASE(too_little_work)
{
    const auto& second_chain{SecondChain()};

    // Verify that just trying to process the second chain would not succeed
    // (too little work).
    HeadersSyncState hss{CreateState()};
    BOOST_REQUIRE_EQUAL(hss.GetState(), State::PRESYNC);

    // Pretend just the first message is "full", so we don't abort.
    CHECK_RESULT(hss.ProcessNextHeaders({{second_chain.front()}}, true),
        hss, /*exp_state=*/State::PRESYNC,
        /*exp_success=*/true, /*exp_request_more=*/true,
        /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
        /*exp_locator_hash=*/second_chain.front().GetHash());

    // Tell the sync logic that the headers message was not full, implying no
    // more headers can be requested. For a low-work-chain, this should cause
    // the sync to end with no headers for acceptance.
    CHECK_RESULT(hss.ProcessNextHeaders(std::span{second_chain}.subspan(1), false),
        hss, /*exp_state=*/State::FINAL,
        // Nevertheless, no validation errors should have been detected with the
        // chain:
        /*exp_success=*/true,
        /*exp_request_more=*/false,
        /*exp_headers_size=*/0, /*exp_pow_validated_prev=*/std::nullopt,
        /*exp_locator_hash=*/std::nullopt);
}

BOOST_AUTO_TEST_SUITE_END()
