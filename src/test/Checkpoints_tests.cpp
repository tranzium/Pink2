//
// Unit tests for block-chain checkpoints
//
#include <boost/test/unit_test.hpp>

#include "checkpoints.h"
#include "main.h"
#include "util.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(Checkpoints_tests)

BOOST_AUTO_TEST_CASE(sanity)
{
    // Use actual Pinkcoin checkpoints
    uint256 p50000 = uint256("0x000000000f794eac0e68cbd44f803cf6efb5eb31ce88444f9dd5b9f183c71b47");
    uint256 p150000 = uint256("0x00000000016f84533d463b761dc14eb2efcc2f79ef6e799930da0880bd86bdaa");
    BOOST_CHECK(Checkpoints::CheckHardened(50000, p50000));
    BOOST_CHECK(Checkpoints::CheckHardened(150000, p150000));

    // Wrong hashes at checkpoints should fail:
    BOOST_CHECK(!Checkpoints::CheckHardened(50000, p150000));
    BOOST_CHECK(!Checkpoints::CheckHardened(150000, p50000));

    // ... but any hash not at a checkpoint should succeed:
    BOOST_CHECK(Checkpoints::CheckHardened(50000+1, p150000));
    BOOST_CHECK(Checkpoints::CheckHardened(150000+1, p50000));

    BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate() >= 728000);
}

// ============================================================================
// Pin ALL 15 hardened checkpoints
// ============================================================================

BOOST_AUTO_TEST_CASE(checkpoint_genesis)
{
    BOOST_CHECK(Checkpoints::CheckHardened(0, hashGenesisBlock));
}

BOOST_AUTO_TEST_CASE(checkpoint_all_hardened)
{
    // All 15 hardened checkpoints from checkpoints.cpp lines 26-41
    struct { int height; const char* hash; } entries[] = {
        {      0, "00000f79b700e6444665c4d090c9b8833664c4e2597c7087a6ba6391b956cc89" },
        {  50000, "000000000f794eac0e68cbd44f803cf6efb5eb31ce88444f9dd5b9f183c71b47" },
        { 150000, "00000000016f84533d463b761dc14eb2efcc2f79ef6e799930da0880bd86bdaa" },
        { 250000, "0000000000432587031f1a05ff8ca58a9dd2e19d34c4b473068b5a06307011c6" },
        { 320000, "6d4459626f9f36ff37cc63cc30f10d3d5864e109554dbadce6c84b038008769e" },
        { 400000, "6577afbb84a390974330442dc2f28959b7ec992b0717d11451f6e916eea81ab4" },
        { 500000, "6ad56723745d551909fb772be2a23d4ad6ad5eddc93a47773f0631e3c341c936" },
        { 580000, "0dc57093e52d8148e4da6660530e22e433dda6555d8c64bef781d92832a628a6" },
        { 590000, "5727cf32aaba496383e1fd8fbc182eb7b2fbaa87150d2e965a2d9fee9ac7f270" },
        { 600000, "000000000011c3a26936c1726549311b49dc1922d1ca8efeadc7133c435437da" },
        { 610000, "0000000000090088e2b22d5ef14f2e63fb64b0ca6656a7b7d4b3e37da25f5d59" },
        { 620000, "000000000000c4ba0b8b56e707858ec912014f7538f0fe3dd67f4ceac169fbc7" },
        { 630000, "000000000023154bdf6cb7546ba146d9b973591cdd2e12638b711af66c004a1c" },
        { 640000, "0000000000256123ab4a8ac67758f0a779dd31fb853f54bfc97ca6b97c653320" },
        { 728000, "00000000002984a1ea9ce4967fca19765a55724234e8479a7f7644c22ee5b30c" },
    };

    for (const auto& e : entries)
    {
        uint256 hash(e.hash);
        BOOST_CHECK_MESSAGE(Checkpoints::CheckHardened(e.height, hash),
            "Checkpoint failed at height " + std::to_string(e.height));
    }
}

BOOST_AUTO_TEST_CASE(checkpoint_wrong_hash)
{
    uint256 wrong("0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    // Wrong hash at a known checkpoint height should fail
    BOOST_CHECK(!Checkpoints::CheckHardened(50000, wrong));
    BOOST_CHECK(!Checkpoints::CheckHardened(728000, wrong));
}

BOOST_AUTO_TEST_CASE(checkpoint_non_checkpoint_height)
{
    // Heights that are NOT checkpoints → any hash passes
    uint256 arbitrary("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    BOOST_CHECK(Checkpoints::CheckHardened(1, arbitrary));
    BOOST_CHECK(Checkpoints::CheckHardened(99999, arbitrary));
    BOOST_CHECK(Checkpoints::CheckHardened(729000, arbitrary));
}

BOOST_AUTO_TEST_CASE(total_blocks_estimate)
{
    // Last checkpoint is at height 728000
    BOOST_CHECK_EQUAL(Checkpoints::GetTotalBlocksEstimate(), 728000);
}

BOOST_AUTO_TEST_SUITE_END()
