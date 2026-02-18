// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tier K: Edge case tests for script evaluation, mempool operations,
// GetMinFee, IsStandard, and CheckTransaction boundary conditions.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "script.h"
#include "wallet.h"
#include "util.h"
#include <vector>
#include <set>

using namespace std;

// ============================================================
// Suite 1: Script evaluation limits
// ============================================================
BOOST_AUTO_TEST_SUITE(script_eval_limits)

// A dummy transaction for EvalScript calls (not actually validated for sigs)
static CTransaction MakeDummyTx()
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256(0);
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    return tx;
}

BOOST_AUTO_TEST_CASE(script_under_10000_bytes_accepted)
{
    // Script of exactly 10000 bytes should be accepted (check is > 10000).
    // Build using 19 x 493-byte pushes (OP_PUSHDATA2 + 2-byte len + 490 data)
    // = 9367 bytes, plus 633 OP_1 bytes = 10000 total.
    // Stack: 19 + 633 = 652 items (under 1000 limit), no counted opcodes.
    CScript script;
    for (int i = 0; i < 19; i++) {
        vector<unsigned char> data(490, static_cast<unsigned char>(i));
        script << data;
    }
    // 19 * 493 = 9367; need 10000 - 9367 = 633 more bytes
    while (script.size() < 10000)
        script << OP_1;
    BOOST_CHECK_EQUAL(script.size(), 10000u);

    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(EvalScript(stack, script, tx, 0, 0));
    BOOST_CHECK(!stack.empty());
}

BOOST_AUTO_TEST_CASE(script_10001_bytes_rejected)
{
    // A script exceeding 10000 bytes must be rejected.
    // Same structure but one extra byte.
    CScript script;
    for (int i = 0; i < 19; i++) {
        vector<unsigned char> data(490, static_cast<unsigned char>(i));
        script << data;
    }
    while (script.size() < 10001)
        script << OP_1;
    BOOST_CHECK_EQUAL(script.size(), 10001u);

    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(!EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(script_op_count_exactly_201_accepted)
{
    // Opcodes > OP_16 increment nOpCount. Exactly 201 should be accepted.
    // OP_NOP (0x61) is > OP_16 and does nothing.
    CScript script;
    for (int i = 0; i < 201; i++)
        script << OP_NOP;
    // Push a true value so the stack isn't empty
    script << OP_1;

    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(script_op_count_202_rejected)
{
    // 202 counted opcodes must be rejected.
    CScript script;
    for (int i = 0; i < 202; i++)
        script << OP_NOP;
    script << OP_1;

    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(!EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(script_element_exactly_520_bytes_accepted)
{
    // Push an element of exactly MAX_SCRIPT_ELEMENT_SIZE (520) bytes.
    vector<unsigned char> data(520, 0x42);
    CScript script;
    script << data;

    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(EvalScript(stack, script, tx, 0, 0));
    BOOST_CHECK_EQUAL(stack.size(), 1u);
    BOOST_CHECK_EQUAL(stack[0].size(), 520u);
}

BOOST_AUTO_TEST_CASE(script_element_521_bytes_rejected)
{
    // Push an element of 521 bytes — exceeds MAX_SCRIPT_ELEMENT_SIZE.
    vector<unsigned char> data(521, 0x42);
    CScript script;
    script << data;

    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(!EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(op_1_does_not_count_toward_opcount)
{
    // OP_1 through OP_16 should not increment nOpCount.
    // 9999 OP_1s + 201 OP_NOPs = 10200 bytes, well within 10000?
    // No — 201 + 9999 = 10200 > 10000. Keep it smaller.
    // 201 OP_NOPs + 1 OP_1 = 202 bytes, fine.
    CScript script;
    for (int i = 0; i < 201; i++)
        script << OP_NOP;
    // These OP_1s should NOT push nOpCount above 201
    for (int i = 0; i < 50; i++)
        script << OP_1;

    // Total = 252 bytes, well under 10000
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    BOOST_CHECK(EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(empty_script_accepted_with_empty_stack)
{
    CScript script;
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx();
    // Empty script succeeds (returns true) but stack is empty
    BOOST_CHECK(EvalScript(stack, script, tx, 0, 0));
    BOOST_CHECK(stack.empty());
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 2: Disabled opcodes
// ============================================================
BOOST_AUTO_TEST_SUITE(script_disabled_opcodes)

static CTransaction MakeDummyTx2()
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256(0);
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    return tx;
}

// Helper: create a script that pushes two values then uses the disabled opcode
static CScript MakeDisabledOpcodeScript(opcodetype op)
{
    CScript script;
    // Push two operands so binary ops have something to work with
    vector<unsigned char> a(4, 0x01);
    vector<unsigned char> b(4, 0x02);
    script << a << b << op;
    return script;
}

BOOST_AUTO_TEST_CASE(disabled_op_cat)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_CAT), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_substr)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_SUBSTR), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_left)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_LEFT), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_mul)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_MUL), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_div)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_DIV), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_mod)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_MOD), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_lshift)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_LSHIFT), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_2mul)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_2MUL), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_invert)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_INVERT), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_and)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_AND), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_or)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_OR), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_xor)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_XOR), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_right)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_RIGHT), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_2div)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_2DIV), tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(disabled_op_rshift)
{
    vector<vector<unsigned char>> stack;
    CTransaction tx = MakeDummyTx2();
    BOOST_CHECK(!EvalScript(stack, MakeDisabledOpcodeScript(OP_RSHIFT), tx, 0, 0));
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 3: OP_RETURN handling
// ============================================================
BOOST_AUTO_TEST_SUITE(script_op_return)

BOOST_AUTO_TEST_CASE(op_return_terminates_script_with_false)
{
    // OP_RETURN immediately returns false from EvalScript
    CScript script;
    script << OP_1 << OP_RETURN;

    vector<vector<unsigned char>> stack;
    CTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    BOOST_CHECK(!EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(op_return_with_data_is_unspendable)
{
    // OP_RETURN followed by data is a standard null-data output
    // but EvalScript always fails on it
    CScript script;
    vector<unsigned char> data(40, 0xAB);
    script << OP_RETURN << data;

    vector<vector<unsigned char>> stack;
    CTransaction tx;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    BOOST_CHECK(!EvalScript(stack, script, tx, 0, 0));
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 4: IsStandard edge cases
// ============================================================
BOOST_AUTO_TEST_SUITE(tx_is_standard)

BOOST_AUTO_TEST_CASE(standard_version_1_accepted)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1; // push-only, canonical, small

    // Pay to a standard pubkey hash
    CScript standard_output;
    standard_output << OP_DUP << OP_HASH160;
    vector<unsigned char> hash20(20, 0x42);
    standard_output << hash20 << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = standard_output;

    BOOST_CHECK(tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(version_too_high_rejected)
{
    // Version > CURRENT_VERSION + 1 is always non-standard
    CTransaction tx;
    tx.nVersion = 3; // CURRENT_VERSION is 1, so 3 > 1+1
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1;

    CScript standard_output;
    standard_output << OP_DUP << OP_HASH160;
    vector<unsigned char> hash20(20, 0x42);
    standard_output << hash20 << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = standard_output;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(scriptsig_over_500_bytes_rejected)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;

    // Build a scriptSig > 500 bytes using canonical pushes
    // Push a 490-byte blob (needs PUSHDATA2? No — OP_PUSHDATA2 for > 255 bytes)
    // Serialized: OP_PUSHDATA2 (1) + length (2) + data (490) = 493 bytes
    // Add another small push to go over 500
    vector<unsigned char> bigdata(490, 0x42);
    tx.vin[0].scriptSig << bigdata;
    vector<unsigned char> extra(10, 0x43);
    tx.vin[0].scriptSig << extra;
    // Total scriptSig > 500 bytes

    CScript standard_output;
    standard_output << OP_DUP << OP_HASH160;
    vector<unsigned char> hash20(20, 0x42);
    standard_output << hash20 << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = standard_output;

    BOOST_CHECK(tx.vin[0].scriptSig.size() > 500);
    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(zero_value_output_rejected)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1;

    CScript standard_output;
    standard_output << OP_DUP << OP_HASH160;
    vector<unsigned char> hash20(20, 0x42);
    standard_output << hash20 << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0; // zero value
    tx.vout[0].scriptPubKey = standard_output;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(nonstandard_output_script_rejected)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1;

    // A script that doesn't match any standard template
    CScript nonstandard;
    nonstandard << OP_1 << OP_2 << OP_ADD;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = nonstandard;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(op_return_single_data_output_is_standard)
{
    // A single OP_RETURN output with data <= MAX_OP_RETURN_RELAY is standard
    // IF there is at least one non-OP_RETURN output (nDataOut <= nTxnOut)
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1;

    // Normal output
    CScript standard_output;
    standard_output << OP_DUP << OP_HASH160;
    vector<unsigned char> hash20(20, 0x42);
    standard_output << hash20 << OP_EQUALVERIFY << OP_CHECKSIG;

    // OP_RETURN output
    CScript op_return_output;
    vector<unsigned char> nulldata(40, 0xBB);
    op_return_output << OP_RETURN << nulldata;

    tx.vout.resize(2);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = standard_output;
    tx.vout[1].nValue = 0;
    tx.vout[1].scriptPubKey = op_return_output;

    BOOST_CHECK(tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(op_return_only_output_nonstandard)
{
    // Only OP_RETURN outputs → nDataOut(1) > nTxnOut(0) → non-standard
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1;

    CScript op_return_output;
    vector<unsigned char> nulldata(20, 0xCC);
    op_return_output << OP_RETURN << nulldata;

    tx.vout.resize(1);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey = op_return_output;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_CASE(multiple_op_return_exceeding_txn_count_nonstandard)
{
    // 2 OP_RETURN outputs + 1 normal = nDataOut(2) > nTxnOut(1) → non-standard
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = static_cast<unsigned int>(GetAdjustedTime());
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig << OP_1;

    CScript standard_output;
    standard_output << OP_DUP << OP_HASH160;
    vector<unsigned char> hash20(20, 0x42);
    standard_output << hash20 << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript op_return_1;
    op_return_1 << OP_RETURN << vector<unsigned char>(10, 0xAA);
    CScript op_return_2;
    op_return_2 << OP_RETURN << vector<unsigned char>(10, 0xBB);

    tx.vout.resize(3);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey = standard_output;
    tx.vout[1].nValue = 0;
    tx.vout[1].scriptPubKey = op_return_1;
    tx.vout[2].nValue = 0;
    tx.vout[2].scriptPubKey = op_return_2;

    BOOST_CHECK(!tx.IsStandard());
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 5: CheckTransaction edge cases
// ============================================================
BOOST_AUTO_TEST_SUITE(check_transaction_edges)

BOOST_AUTO_TEST_CASE(empty_vin_rejected)
{
    CTransaction tx;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    // vin is empty
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(empty_vout_rejected)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    // vout is empty
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(negative_output_value_rejected)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = -1;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(output_exceeding_max_money_rejected)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = MAX_MONEY + 1;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(output_total_overflow_rejected)
{
    // Two outputs that individually are within range but sum exceeds MAX_MONEY
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(2);
    tx.vout[0].nValue = MAX_MONEY;
    tx.vout[0].scriptPubKey << OP_1;
    tx.vout[1].nValue = 1; // pushes total over MAX_MONEY
    tx.vout[1].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(duplicate_inputs_rejected)
{
    CTransaction tx;
    tx.vin.resize(2);
    // Both inputs reference the same outpoint
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[1].prevout.hash = tx.vin[0].prevout.hash;
    tx.vin[1].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(coinbase_script_too_small_rejected)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull(); // coinbase
    tx.vin[0].scriptSig.resize(1); // < 2 bytes
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(coinbase_script_too_large_rejected)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull(); // coinbase
    tx.vin[0].scriptSig.resize(201); // > 200 bytes
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(non_coinbase_with_null_prevout_rejected)
{
    CTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vin[1].prevout.SetNull(); // null prevout in non-coinbase
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(!tx.CheckTransaction());
}

BOOST_AUTO_TEST_CASE(valid_transaction_accepted)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;
    BOOST_CHECK(tx.CheckTransaction());
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 6: GetMinFee edge cases
// ============================================================
BOOST_AUTO_TEST_SUITE(tx_getminfee)

BOOST_AUTO_TEST_CASE(getminfee_small_tx)
{
    // A small transaction: fee = (1 + nBytes/1000) * MIN_TX_FEE
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    // 250 bytes, nBlockSize=0 (not in block context)
    int64_t fee = tx.GetMinFee(0, GMF_RELAY, 250);
    // (1 + 250/1000) * MIN_TX_FEE = 1 * 10000 = 10000
    BOOST_CHECK_EQUAL(fee, MIN_TX_FEE);
}

BOOST_AUTO_TEST_CASE(getminfee_1500_bytes)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    // 1500 bytes: (1 + 1500/1000) * 10000 = 2 * 10000 = 20000
    int64_t fee = tx.GetMinFee(0, GMF_RELAY, 1500);
    BOOST_CHECK_EQUAL(fee, 2 * MIN_TX_FEE);
}

BOOST_AUTO_TEST_CASE(getminfee_dust_output_forces_base_fee)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = CENT - 1; // dust: less than CENT
    tx.vout[0].scriptPubKey << OP_1;

    // Even for small tx, dust output forces at least MIN_TX_FEE
    int64_t fee = tx.GetMinFee(0, GMF_RELAY, 250);
    BOOST_CHECK(fee >= MIN_TX_FEE);
}

BOOST_AUTO_TEST_CASE(getminfee_full_block_returns_max_money)
{
    // When nNewBlockSize >= MAX_BLOCK_SIZE_GEN, fee = MAX_MONEY
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    // nBlockSize + nBytes >= MAX_BLOCK_SIZE_GEN (500000)
    // nBlockSize = 499000, nBytes = 1500 → nNewBlockSize = 500500 ≥ 500000
    int64_t fee = tx.GetMinFee(499000, GMF_BLOCK, 1500);
    BOOST_CHECK_EQUAL(fee, MAX_MONEY);
}

BOOST_AUTO_TEST_CASE(getminfee_half_full_block_increases_fee)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    // When block is over half full, fee increases
    // nBlockSize = 0: no increase
    int64_t fee_empty = tx.GetMinFee(0, GMF_BLOCK, 250);
    // nBlockSize = 300000: nNewBlockSize = 300250 >= 250000 → fee multiplied
    int64_t fee_half = tx.GetMinFee(300000, GMF_BLOCK, 250);
    BOOST_CHECK(fee_half > fee_empty);
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 7: Mempool operations
// ============================================================
BOOST_AUTO_TEST_SUITE(mempool_operations)

BOOST_AUTO_TEST_CASE(mempool_add_and_query)
{
    mempool.clear();

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("1111111111111111111111111111111111111111111111111111111111111111");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    uint256 txhash = tx.GetHash();
    mempool.addUnchecked(txhash, tx);

    vector<uint256> hashes;
    mempool.queryHashes(hashes);
    BOOST_CHECK_EQUAL(hashes.size(), 1u);
    BOOST_CHECK(hashes[0] == txhash);

    mempool.clear();
}

BOOST_AUTO_TEST_CASE(mempool_remove_nonrecursive)
{
    mempool.clear();

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("2222222222222222222222222222222222222222222222222222222222222222");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    uint256 txhash = tx.GetHash();
    mempool.addUnchecked(txhash, tx);

    mempool.remove(tx, false);

    vector<uint256> hashes;
    mempool.queryHashes(hashes);
    BOOST_CHECK(hashes.empty());
}

BOOST_AUTO_TEST_CASE(mempool_remove_recursive_chain)
{
    mempool.clear();

    CTransaction parent;
    parent.vin.resize(1);
    parent.vin[0].prevout.hash.SetHex("3333333333333333333333333333333333333333333333333333333333333333");
    parent.vin[0].prevout.n = 0;
    parent.vout.resize(1);
    parent.vout[0].nValue = COIN;
    parent.vout[0].scriptPubKey << OP_1;

    uint256 parentHash = parent.GetHash();
    mempool.addUnchecked(parentHash, parent);

    CTransaction child;
    child.vin.resize(1);
    child.vin[0].prevout.hash = parentHash;
    child.vin[0].prevout.n = 0;
    child.vout.resize(1);
    child.vout[0].nValue = COIN / 2;
    child.vout[0].scriptPubKey << OP_1;

    uint256 childHash = child.GetHash();
    mempool.addUnchecked(childHash, child);

    vector<uint256> hashes;
    mempool.queryHashes(hashes);
    BOOST_CHECK_EQUAL(hashes.size(), 2u);

    mempool.remove(parent, true);

    hashes.clear();
    mempool.queryHashes(hashes);
    BOOST_CHECK(hashes.empty());
}

BOOST_AUTO_TEST_CASE(mempool_remove_conflicts)
{
    mempool.clear();

    CTransaction poolTx;
    poolTx.vin.resize(1);
    poolTx.vin[0].prevout.hash.SetHex("4444444444444444444444444444444444444444444444444444444444444444");
    poolTx.vin[0].prevout.n = 0;
    poolTx.vout.resize(1);
    poolTx.vout[0].nValue = COIN;
    poolTx.vout[0].scriptPubKey << OP_1;

    uint256 poolHash = poolTx.GetHash();
    mempool.addUnchecked(poolHash, poolTx);

    CTransaction conflicting;
    conflicting.vin.resize(1);
    conflicting.vin[0].prevout.hash.SetHex("4444444444444444444444444444444444444444444444444444444444444444");
    conflicting.vin[0].prevout.n = 0;
    conflicting.vout.resize(1);
    conflicting.vout[0].nValue = COIN / 2;
    conflicting.vout[0].scriptPubKey << OP_1;

    mempool.removeConflicts(conflicting);

    vector<uint256> hashes;
    mempool.queryHashes(hashes);
    BOOST_CHECK(hashes.empty());
}

BOOST_AUTO_TEST_CASE(mempool_clear_all)
{
    mempool.clear();

    for (int i = 0; i < 5; i++) {
        CTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = static_cast<unsigned int>(i);
        tx.vin[0].prevout.hash.SetHex("5555555555555555555555555555555555555555555555555555555555555555");
        tx.vout.resize(1);
        tx.vout[0].nValue = COIN;
        tx.vout[0].scriptPubKey << OP_1;
        mempool.addUnchecked(tx.GetHash(), tx);
    }

    vector<uint256> hashes;
    mempool.queryHashes(hashes);
    BOOST_CHECK_EQUAL(hashes.size(), 5u);

    mempool.clear();

    hashes.clear();
    mempool.queryHashes(hashes);
    BOOST_CHECK(hashes.empty());
}

BOOST_AUTO_TEST_CASE(mempool_remove_nonexistent_is_noop)
{
    mempool.clear();

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("6666666666666666666666666666666666666666666666666666666666666666");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    mempool.remove(tx, false);
    mempool.remove(tx, true);
    mempool.removeConflicts(tx);

    vector<uint256> hashes;
    mempool.queryHashes(hashes);
    BOOST_CHECK(hashes.empty());
}

BOOST_AUTO_TEST_CASE(mempool_mapnexttx_tracking)
{
    mempool.clear();

    CTransaction tx;
    tx.vin.resize(2);
    tx.vin[0].prevout.hash.SetHex("7777777777777777777777777777777777777777777777777777777777777777");
    tx.vin[0].prevout.n = 0;
    tx.vin[1].prevout.hash.SetHex("8888888888888888888888888888888888888888888888888888888888888888");
    tx.vin[1].prevout.n = 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    tx.vout[0].scriptPubKey << OP_1;

    uint256 txhash = tx.GetHash();
    mempool.addUnchecked(txhash, tx);

    BOOST_CHECK(mempool.mapNextTx.count(tx.vin[0].prevout));
    BOOST_CHECK(mempool.mapNextTx.count(tx.vin[1].prevout));
    BOOST_CHECK_EQUAL(mempool.mapNextTx.size(), 2u);

    mempool.remove(tx, false);
    BOOST_CHECK(mempool.mapNextTx.empty());
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 8: SigOpCount
// ============================================================
BOOST_AUTO_TEST_SUITE(sigop_count)

BOOST_AUTO_TEST_CASE(sigop_count_checksig)
{
    CScript script;
    script << OP_CHECKSIG;
    BOOST_CHECK_EQUAL(script.GetSigOpCount(false), 1u);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(true), 1u);
}

BOOST_AUTO_TEST_CASE(sigop_count_checksigverify)
{
    CScript script;
    script << OP_CHECKSIGVERIFY;
    BOOST_CHECK_EQUAL(script.GetSigOpCount(false), 1u);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(true), 1u);
}

BOOST_AUTO_TEST_CASE(sigop_count_checkmultisig_not_accurate)
{
    // Without fAccurate, OP_CHECKMULTISIG counts as 20
    CScript script;
    script << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(script.GetSigOpCount(false), 20u);
}

BOOST_AUTO_TEST_CASE(sigop_count_checkmultisig_accurate_with_n)
{
    // With fAccurate and preceding OP_N, counts as N
    CScript script;
    script << OP_3 << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(script.GetSigOpCount(true), 3u);
}

BOOST_AUTO_TEST_CASE(sigop_count_checkmultisig_accurate_without_n)
{
    // With fAccurate but no preceding OP_N, still counts as 20
    CScript script;
    script << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(script.GetSigOpCount(true), 20u);
}

BOOST_AUTO_TEST_CASE(sigop_count_multiple_ops)
{
    CScript script;
    script << OP_CHECKSIG << OP_CHECKSIG << OP_2 << OP_CHECKMULTISIG;
    // 1 + 1 + 20 (not accurate) = 22
    BOOST_CHECK_EQUAL(script.GetSigOpCount(false), 22u);
    // 1 + 1 + 2 (accurate, preceded by OP_2) = 4
    BOOST_CHECK_EQUAL(script.GetSigOpCount(true), 4u);
}

BOOST_AUTO_TEST_CASE(sigop_count_empty_script)
{
    CScript script;
    BOOST_CHECK_EQUAL(script.GetSigOpCount(false), 0u);
    BOOST_CHECK_EQUAL(script.GetSigOpCount(true), 0u);
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================
// Suite 9: VerifyScript behavior
// ============================================================
BOOST_AUTO_TEST_SUITE(verify_script_behavior)

BOOST_AUTO_TEST_CASE(verify_script_true_result)
{
    // scriptSig pushes OP_TRUE, scriptPubKey checks OP_TRUE remains
    CScript scriptSig;
    scriptSig << OP_TRUE;

    CScript scriptPubKey;
    // scriptPubKey does nothing — stack still has TRUE
    // (empty scriptPubKey doesn't execute anything; stack preserved from scriptSig)

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("aaaa111111111111111111111111111111111111111111111111111111111111");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    // Empty scriptPubKey: after evaluating, stack has [TRUE] from scriptSig
    BOOST_CHECK(VerifyScript(scriptSig, scriptPubKey, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(verify_script_false_result)
{
    CScript scriptSig;
    scriptSig << OP_FALSE;

    CScript scriptPubKey;
    // Stack ends with FALSE → fails

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("bbbb111111111111111111111111111111111111111111111111111111111111");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    BOOST_CHECK(!VerifyScript(scriptSig, scriptPubKey, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(verify_script_empty_stack_fails)
{
    CScript scriptSig;
    // Empty scriptSig pushes nothing

    CScript scriptPubKey;
    // Empty scriptPubKey too — stack is empty → fail

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("cccc111111111111111111111111111111111111111111111111111111111111");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    BOOST_CHECK(!VerifyScript(scriptSig, scriptPubKey, tx, 0, 0));
}

BOOST_AUTO_TEST_CASE(verify_script_op_return_in_pubkey_fails)
{
    CScript scriptSig;
    scriptSig << OP_TRUE;

    CScript scriptPubKey;
    scriptPubKey << OP_RETURN;

    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash.SetHex("dddd111111111111111111111111111111111111111111111111111111111111");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;

    // OP_RETURN in scriptPubKey causes EvalScript to return false
    BOOST_CHECK(!VerifyScript(scriptSig, scriptPubKey, tx, 0, 0));
}

BOOST_AUTO_TEST_SUITE_END()
