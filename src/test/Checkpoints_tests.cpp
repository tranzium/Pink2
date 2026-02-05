//
// Unit tests for block-chain checkpoints
//
#include <boost/test/unit_test.hpp>

#include "checkpoints.h"
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

BOOST_AUTO_TEST_SUITE_END()
