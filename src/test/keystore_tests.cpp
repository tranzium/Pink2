// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Unit tests for CBasicKeyStore — key and script storage operations.

#include <boost/test/unit_test.hpp>

#include "keystore.h"
#include "key.h"
#include "script.h"

#include <set>

BOOST_AUTO_TEST_SUITE(keystore_tests)

// ============================================================================
// CBasicKeyStore — key operations
// ============================================================================

BOOST_AUTO_TEST_CASE(basic_add_have_key)
{
    CBasicKeyStore store;
    CKey key;
    key.MakeNewKey(true);
    CKeyID id = key.GetPubKey().GetID();

    BOOST_CHECK(!store.HaveKey(id));
    BOOST_CHECK(store.AddKey(key));
    BOOST_CHECK(store.HaveKey(id));
}

BOOST_AUTO_TEST_CASE(basic_get_key)
{
    CBasicKeyStore store;
    CKey key;
    key.MakeNewKey(true);
    CKeyID id = key.GetPubKey().GetID();

    store.AddKey(key);

    CKey keyOut;
    BOOST_CHECK(store.GetKey(id, keyOut));

    bool fCompressed1 = false, fCompressed2 = false;
    CSecret s1 = key.GetSecret(fCompressed1);
    CSecret s2 = keyOut.GetSecret(fCompressed2);
    BOOST_CHECK(s1 == s2);
    BOOST_CHECK_EQUAL(fCompressed1, fCompressed2);
}

BOOST_AUTO_TEST_CASE(basic_missing_key)
{
    CBasicKeyStore store;
    CKey key;
    key.MakeNewKey(true);
    CKeyID id = key.GetPubKey().GetID();

    // Never added — should not be found
    BOOST_CHECK(!store.HaveKey(id));

    CKey keyOut;
    BOOST_CHECK(!store.GetKey(id, keyOut));
}

BOOST_AUTO_TEST_CASE(basic_get_keys_set)
{
    CBasicKeyStore store;
    std::set<CKeyID> expected;

    for (int i = 0; i < 3; i++)
    {
        CKey key;
        key.MakeNewKey(true);
        CKeyID id = key.GetPubKey().GetID();
        store.AddKey(key);
        expected.insert(id);
    }

    std::set<CKeyID> result;
    store.GetKeys(result);
    BOOST_CHECK_EQUAL(result.size(), 3u);
    BOOST_CHECK(result == expected);
}

BOOST_AUTO_TEST_CASE(basic_get_pubkey)
{
    CBasicKeyStore store;
    CKey key;
    key.MakeNewKey(true);
    CPubKey expectedPub = key.GetPubKey();
    CKeyID id = expectedPub.GetID();

    store.AddKey(key);

    CPubKey pubOut;
    BOOST_CHECK(store.GetPubKey(id, pubOut));
    BOOST_CHECK(pubOut == expectedPub);
}

BOOST_AUTO_TEST_CASE(basic_multiple_keys)
{
    CBasicKeyStore store;
    std::vector<CKeyID> ids;

    for (int i = 0; i < 5; i++)
    {
        CKey key;
        key.MakeNewKey(i % 2 == 0); // alternate compressed/uncompressed
        CKeyID id = key.GetPubKey().GetID();
        store.AddKey(key);
        ids.push_back(id);
    }

    for (const CKeyID& id : ids)
        BOOST_CHECK(store.HaveKey(id));

    std::set<CKeyID> allKeys;
    store.GetKeys(allKeys);
    BOOST_CHECK_EQUAL(allKeys.size(), 5u);
}

BOOST_AUTO_TEST_CASE(basic_overwrite_key)
{
    CBasicKeyStore store;
    CKey key;
    key.MakeNewKey(true);
    CKeyID id = key.GetPubKey().GetID();

    // Add same key twice — should not corrupt
    BOOST_CHECK(store.AddKey(key));
    BOOST_CHECK(store.AddKey(key));
    BOOST_CHECK(store.HaveKey(id));

    CKey keyOut;
    BOOST_CHECK(store.GetKey(id, keyOut));

    bool c1, c2;
    BOOST_CHECK(key.GetSecret(c1) == keyOut.GetSecret(c2));
}

// ============================================================================
// CBasicKeyStore — script operations
// ============================================================================

BOOST_AUTO_TEST_CASE(basic_add_cscript)
{
    CBasicKeyStore store;
    CScript script;
    script << OP_DUP << OP_HASH160;

    CScriptID id = CScriptID(Hash160(script));

    BOOST_CHECK(!store.HaveCScript(id));
    BOOST_CHECK(store.AddCScript(script));
    BOOST_CHECK(store.HaveCScript(id));
}

BOOST_AUTO_TEST_CASE(basic_get_cscript)
{
    CBasicKeyStore store;
    CScript script;
    script << OP_1 << OP_2 << OP_CHECKMULTISIG;

    CScriptID id = CScriptID(Hash160(script));
    store.AddCScript(script);

    CScript scriptOut;
    BOOST_CHECK(store.GetCScript(id, scriptOut));
    BOOST_CHECK(script == scriptOut);
}

BOOST_AUTO_TEST_CASE(basic_missing_cscript)
{
    CBasicKeyStore store;
    CScriptID fakeID(uint160(42));

    BOOST_CHECK(!store.HaveCScript(fakeID));

    CScript scriptOut;
    BOOST_CHECK(!store.GetCScript(fakeID, scriptOut));
}

BOOST_AUTO_TEST_CASE(basic_multiple_scripts)
{
    CBasicKeyStore store;

    for (int i = 0; i < 3; i++)
    {
        CScript script;
        script << (i + 1) << OP_DROP;

        store.AddCScript(script);

        CScriptID id = CScriptID(Hash160(script));
        BOOST_CHECK(store.HaveCScript(id));

        CScript out;
        BOOST_CHECK(store.GetCScript(id, out));
        BOOST_CHECK(script == out);
    }
}

BOOST_AUTO_TEST_CASE(basic_key_script_independent)
{
    CBasicKeyStore store;

    // Add a key
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyID = key.GetPubKey().GetID();
    store.AddKey(key);

    // Add a script
    CScript script;
    script << OP_1 << OP_EQUAL;
    CScriptID scriptID = CScriptID(Hash160(script));
    store.AddCScript(script);

    // Both should be independently retrievable
    BOOST_CHECK(store.HaveKey(keyID));
    BOOST_CHECK(store.HaveCScript(scriptID));

    // Key operations don't return scripts and vice versa
    CScript scriptOut;
    CKey keyOut;
    BOOST_CHECK(store.GetKey(keyID, keyOut));
    BOOST_CHECK(store.GetCScript(scriptID, scriptOut));
}

BOOST_AUTO_TEST_SUITE_END()
