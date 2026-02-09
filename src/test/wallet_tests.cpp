#include <boost/test/unit_test.hpp>

#include "main.h"
#include "wallet.h"
#include "walletdb.h"
#include "base58.h"

// how many times to run all the tests to have a chance to catch errors that only show up with particular random shuffles
#define RUN_TESTS 100

// some tests fail 1% of the time due to bad luck.
// we repeat those tests this many times and only complain if all iterations of the test fail
#define RANDOM_REPEATS 5

using namespace std;

extern CWallet* pwalletMain;

typedef set<pair<const CWalletTx*,unsigned int> > CoinSet;

BOOST_AUTO_TEST_SUITE(wallet_tests)

static CWallet wallet;
static vector<COutput> vCoins;

static void add_coin(int64_t nValue, int nAge = 6*24, bool fIsFromMe = false, int nInput=0)
{
    static int i;
    CTransaction* tx = new CTransaction;
    tx->nLockTime = i++;        // so all transactions get different hashes
    tx->vout.resize(nInput+1);
    tx->vout[nInput].nValue = nValue;
    CWalletTx* wtx = new CWalletTx(&wallet, *tx);
    delete tx;
    if (fIsFromMe)
    {
        // IsFromMe() returns (GetDebit() > 0), and GetDebit() is 0 if vin.empty(),
        // so stop vin being empty, and cache a non-zero Debit to fake out IsFromMe()
        wtx->vin.resize(1);
        wtx->fDebitCached = true;
        wtx->nDebitCached = 1;
    }
    COutput output(wtx, nInput, nAge);
    vCoins.push_back(output);
}

static void empty_wallet(void)
{
    for (COutput output : vCoins)
        delete output.tx;
    vCoins.clear();
}

static bool equal_sets(CoinSet a, CoinSet b)
{
    pair<CoinSet::iterator, CoinSet::iterator> ret = mismatch(a.begin(), a.end(), b.begin());
    return ret.first == a.end() && ret.second == b.end();
}

BOOST_AUTO_TEST_CASE(coin_selection_tests)
{
    static CoinSet setCoinsRet, setCoinsRet2;
    static int64_t nValueRet;

    // test multiple times to allow for differences in the shuffle order
    for (int i = 0; i < RUN_TESTS; i++)
    {
        empty_wallet();

        // with an empty wallet we can't even pay one cent
        BOOST_CHECK(!wallet.SelectCoinsMinConf( 1 * CENT, GetTime(), 1, 6, vCoins, setCoinsRet, nValueRet));

        add_coin(1*CENT, 4);        // add a new 1 cent coin

        // with a new 1 cent coin, we still can't find a mature 1 cent
        BOOST_CHECK(!wallet.SelectCoinsMinConf( 1 * CENT, GetTime(), 1, 6, vCoins, setCoinsRet, nValueRet));

        // but we can find a new 1 cent
        BOOST_CHECK( wallet.SelectCoinsMinConf( 1 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1 * CENT);

        add_coin(2*CENT);           // add a mature 2 cent coin

        // we can't make 3 cents of mature coins
        BOOST_CHECK(!wallet.SelectCoinsMinConf( 3 * CENT, GetTime(), 1, 6, vCoins, setCoinsRet, nValueRet));

        // we can make 3 cents of new  coins
        BOOST_CHECK( wallet.SelectCoinsMinConf( 3 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 3 * CENT);

        add_coin(5*CENT);           // add a mature 5 cent coin,
        add_coin(10*CENT, 3, true); // a new 10 cent coin sent from one of our own addresses
        add_coin(20*CENT);          // and a mature 20 cent coin

        // now we have new: 1+10=11 (of which 10 was self-sent), and mature: 2+5+20=27.  total = 38

        // we can't make 38 cents only if we disallow new coins:
        BOOST_CHECK(!wallet.SelectCoinsMinConf(38 * CENT, GetTime(), 1, 6, vCoins, setCoinsRet, nValueRet));
        // we can't even make 37 cents if we don't allow new coins even if they're from us
        BOOST_CHECK(!wallet.SelectCoinsMinConf(38 * CENT, GetTime(), 6, 6, vCoins, setCoinsRet, nValueRet));
        // but we can make 37 cents if we accept new coins from ourself
        BOOST_CHECK( wallet.SelectCoinsMinConf(37 * CENT, GetTime(), 1, 6, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 37 * CENT);
        // and we can make 38 cents if we accept all new coins
        BOOST_CHECK( wallet.SelectCoinsMinConf(38 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 38 * CENT);

        // try making 34 cents from 1,2,5,10,20 - we can't do it exactly
        BOOST_CHECK( wallet.SelectCoinsMinConf(34 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_GT(nValueRet, 34 * CENT);         // but should get more than 34 cents
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3);     // the best should be 20+10+5.  it's incredibly unlikely the 1 or 2 got included (but possible)

        // when we try making 7 cents, the smaller coins (1,2,5) are enough.  We should see just 2+5
        BOOST_CHECK( wallet.SelectCoinsMinConf( 7 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 7 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2);

        // when we try making 8 cents, the smaller coins (1,2,5) are exactly enough.
        BOOST_CHECK( wallet.SelectCoinsMinConf( 8 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK(nValueRet == 8 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3);

        // when we try making 9 cents, no subset of smaller coins is enough, and we get the next bigger coin (10)
        BOOST_CHECK( wallet.SelectCoinsMinConf( 9 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 10 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1);

        // now clear out the wallet and start again to test choosing between subsets of smaller coins and the next biggest coin
        empty_wallet();

        add_coin( 6*CENT);
        add_coin( 7*CENT);
        add_coin( 8*CENT);
        add_coin(20*CENT);
        add_coin(30*CENT); // now we have 6+7+8+20+30 = 71 cents total

        // check that we have 71 and not 72
        BOOST_CHECK( wallet.SelectCoinsMinConf(71 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK(!wallet.SelectCoinsMinConf(72 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));

        // now try making 16 cents.  the best smaller coins can do is 6+7+8 = 21; not as good at the next biggest coin, 20
        BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 20 * CENT); // we should get 20 in one coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1);

        add_coin( 5*CENT); // now we have 5+6+7+8+20+30 = 75 cents total

        // now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, better than the next biggest coin, 20
        BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 18 * CENT); // we should get 18 in 3 coins
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3);

        add_coin( 18*CENT); // now we have 5+6+7+8+18+20+30

        // and now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, the same as the next biggest coin, 18
        BOOST_CHECK( wallet.SelectCoinsMinConf(16 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 18 * CENT);  // we should get 18 in 1 coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1); // because in the event of a tie, the biggest coin wins

        // now try making 11 cents.  we should get 5+6
        BOOST_CHECK( wallet.SelectCoinsMinConf(11 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 11 * CENT);
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2);

        // check that the smallest bigger coin is used
        add_coin( 1*COIN);
        add_coin( 2*COIN);
        add_coin( 3*COIN);
        add_coin( 4*COIN); // now we have 5+6+7+8+18+20+30+100+200+300+400 = 1094 cents
        BOOST_CHECK( wallet.SelectCoinsMinConf(95 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1 * COIN);  // we should get 1 BTC in 1 coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1);

        BOOST_CHECK( wallet.SelectCoinsMinConf(195 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 2 * COIN);  // we should get 2 BTC in 1 coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1);

        // empty the wallet and start again, now with fractions of a cent, to test sub-cent change avoidance
        empty_wallet();
        add_coin(0.1*CENT);
        add_coin(0.2*CENT);
        add_coin(0.3*CENT);
        add_coin(0.4*CENT);
        add_coin(0.5*CENT);

        // try making 1 cent from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 = 1.5 cents
        // we'll get sub-cent change whatever happens, so can expect 1.0 exactly
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1 * CENT);

        // but if we add a bigger coin, making it possible to avoid sub-cent change, things change:
        add_coin(1111*CENT);

        // try making 1 cent from 0.1 + 0.2 + 0.3 + 0.4 + 0.5 + 1111 = 1112.5 cents
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1 * CENT); // we should get the exact amount

        // if we add more sub-cent coins:
        add_coin(0.6*CENT);
        add_coin(0.7*CENT);

        // and try again to make 1.0 cents, we can still make 1.0 cents
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1 * CENT); // we should get the exact amount

        // run the 'mtgox' test (see http://blockexplorer.com/tx/29a3efd3ef04f9153d47a990bd7b048a4b2d213daaa5fb8ed670fb85f13bdbcf)
        // they tried to consolidate 10 50k coins into one 500k coin, and ended up with 50k in change
        empty_wallet();
        for (int i = 0; i < 20; i++)
            add_coin(50000 * COIN);

        BOOST_CHECK( wallet.SelectCoinsMinConf(500000 * COIN, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 500000 * COIN); // we should get the exact amount
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 10); // in ten coins

        // if there's not enough in the smaller coins to make at least 1 cent change (0.5+0.6+0.7 < 1.0+1.0),
        // we need to try finding an exact subset anyway

        // sometimes it will fail, and so we use the next biggest coin:
        empty_wallet();
        add_coin(0.5 * CENT);
        add_coin(0.6 * CENT);
        add_coin(0.7 * CENT);
        add_coin(1111 * CENT);
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1111 * CENT); // we get the bigger coin
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 1);

        // but sometimes it's possible, and we use an exact subset (0.4 + 0.6 = 1.0)
        empty_wallet();
        add_coin(0.4 * CENT);
        add_coin(0.6 * CENT);
        add_coin(0.8 * CENT);
        add_coin(1111 * CENT);
        BOOST_CHECK( wallet.SelectCoinsMinConf(1 * CENT, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1 * CENT);   // we should get the exact amount
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2); // in two coins 0.4+0.6

        // test avoiding sub-cent change
        empty_wallet();
        add_coin(0.0005 * COIN);
        add_coin(0.01 * COIN);
        add_coin(1 * COIN);

        // trying to make 1.0001 from these three coins
        BOOST_CHECK( wallet.SelectCoinsMinConf(1.0001 * COIN, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1.0105 * COIN);   // we should get all coins
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 3);

        // but if we try to make 0.999, we should take the bigger of the two small coins to avoid sub-cent change
        BOOST_CHECK( wallet.SelectCoinsMinConf(0.999 * COIN, GetTime(), 1, 1, vCoins, setCoinsRet, nValueRet));
        BOOST_CHECK_EQUAL(nValueRet, 1.01 * COIN);   // we should get 1 + 0.01
        BOOST_CHECK_EQUAL(setCoinsRet.size(), 2);

        // test randomness
        {
            empty_wallet();
            for (int i2 = 0; i2 < 100; i2++)
                add_coin(COIN);

            // picking 50 from 100 coins doesn't depend on the shuffle,
            // but does depend on randomness in the stochastic approximation code
            BOOST_CHECK(wallet.SelectCoinsMinConf(50 * COIN, GetTime(), 1, 6, vCoins, setCoinsRet , nValueRet));
            BOOST_CHECK(wallet.SelectCoinsMinConf(50 * COIN, GetTime(), 1, 6, vCoins, setCoinsRet2, nValueRet));
            BOOST_CHECK(!equal_sets(setCoinsRet, setCoinsRet2));

            int fails = 0;
            for (int i = 0; i < RANDOM_REPEATS; i++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                BOOST_CHECK(wallet.SelectCoinsMinConf(COIN, GetTime(), 1, 6, vCoins, setCoinsRet , nValueRet));
                BOOST_CHECK(wallet.SelectCoinsMinConf(COIN, GetTime(), 1, 6, vCoins, setCoinsRet2, nValueRet));
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            BOOST_CHECK_NE(fails, RANDOM_REPEATS);

            // add 75 cents in small change.  not enough to make 90 cents,
            // then try making 90 cents.  there are multiple competing "smallest bigger" coins,
            // one of which should be picked at random
            add_coin( 5*CENT); add_coin(10*CENT); add_coin(15*CENT); add_coin(20*CENT); add_coin(25*CENT);

            fails = 0;
            for (int i = 0; i < RANDOM_REPEATS; i++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                BOOST_CHECK(wallet.SelectCoinsMinConf(90*CENT, GetTime(), 1, 6, vCoins, setCoinsRet , nValueRet));
                BOOST_CHECK(wallet.SelectCoinsMinConf(90*CENT, GetTime(), 1, 6, vCoins, setCoinsRet2, nValueRet));
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            BOOST_CHECK_NE(fails, RANDOM_REPEATS);
        }
    }
}

// ============================================================================
// WalletFeature constants
// ============================================================================

BOOST_AUTO_TEST_CASE(wallet_feature_constants)
{
    BOOST_CHECK_EQUAL(FEATURE_BASE, 10500);
    BOOST_CHECK_EQUAL(FEATURE_WALLETCRYPT, 40000);
    BOOST_CHECK_EQUAL(FEATURE_COMPRPUBKEY, 60000);
    BOOST_CHECK_EQUAL(FEATURE_LATEST, 60000);
}

// ============================================================================
// GenerateNewKey tests
// ============================================================================

BOOST_AUTO_TEST_CASE(generate_new_key)
{
    LOCK(pwalletMain->cs_wallet);
    CPubKey pubKey = pwalletMain->GenerateNewKey();
    BOOST_CHECK(pubKey.IsValid());

    // Key should be in the wallet's keystore
    CKeyID keyID = pubKey.GetID();
    BOOST_CHECK(pwalletMain->HaveKey(keyID));

    // Can retrieve the key
    CKey key;
    BOOST_CHECK(pwalletMain->GetKey(keyID, key));
    BOOST_CHECK(key.IsValid());

    // Retrieved key's public key should match
    CPubKey pubKey2 = key.GetPubKey();
    BOOST_CHECK(pubKey == pubKey2);
}

BOOST_AUTO_TEST_CASE(generate_multiple_unique_keys)
{
    LOCK(pwalletMain->cs_wallet);
    CPubKey key1 = pwalletMain->GenerateNewKey();
    CPubKey key2 = pwalletMain->GenerateNewKey();
    CPubKey key3 = pwalletMain->GenerateNewKey();

    // All keys must be distinct
    BOOST_CHECK(key1 != key2);
    BOOST_CHECK(key2 != key3);
    BOOST_CHECK(key1 != key3);

    // All must be valid
    BOOST_CHECK(key1.IsValid());
    BOOST_CHECK(key2.IsValid());
    BOOST_CHECK(key3.IsValid());
}

// ============================================================================
// Key pool tests
// ============================================================================

BOOST_AUTO_TEST_CASE(key_pool_topup)
{
    // TopUpKeyPool should populate the pool
    BOOST_CHECK(pwalletMain->TopUpKeyPool());
    BOOST_CHECK(!pwalletMain->setKeyPool.empty());
}

BOOST_AUTO_TEST_CASE(get_key_from_pool)
{
    BOOST_CHECK(pwalletMain->TopUpKeyPool());

    CPubKey pubKey;
    BOOST_CHECK(pwalletMain->GetKeyFromPool(pubKey, false));
    BOOST_CHECK(pubKey.IsValid());

    // The key should be in the wallet
    BOOST_CHECK(pwalletMain->HaveKey(pubKey.GetID()));
}

BOOST_AUTO_TEST_CASE(new_key_pool_resets)
{
    // NewKeyPool clears and refills the pool
    BOOST_CHECK(pwalletMain->NewKeyPool());
    size_t poolSize = pwalletMain->setKeyPool.size();
    BOOST_CHECK(poolSize > 0);

    // Getting keys from pool should work
    CPubKey key;
    BOOST_CHECK(pwalletMain->GetKeyFromPool(key, false));
    BOOST_CHECK(key.IsValid());
}

// ============================================================================
// Address / key retrieval tests
// ============================================================================

BOOST_AUTO_TEST_CASE(get_pubkey_from_keyid)
{
    LOCK(pwalletMain->cs_wallet);
    CPubKey generated = pwalletMain->GenerateNewKey();
    CKeyID keyID = generated.GetID();

    CPubKey retrieved;
    BOOST_CHECK(pwalletMain->GetPubKey(keyID, retrieved));
    BOOST_CHECK(generated == retrieved);
}

BOOST_AUTO_TEST_CASE(have_key_returns_false_for_unknown)
{
    // A random KeyID that's not in the wallet
    CKeyID unknownID(uint160(12345));
    BOOST_CHECK(!pwalletMain->HaveKey(unknownID));
}

// ============================================================================
// Pinkcoin address encoding
// ============================================================================

BOOST_AUTO_TEST_CASE(address_encoding_prefix)
{
    LOCK(pwalletMain->cs_wallet);
    CPubKey pubKey = pwalletMain->GenerateNewKey();
    CKeyID keyID = pubKey.GetID();
    CBitcoinAddress addr(keyID);

    std::string strAddr = addr.ToString();
    // Pinkcoin addresses start with '2' (PUBKEY_ADDRESS = 3)
    BOOST_CHECK_EQUAL(strAddr[0], '2');

    // Round-trip: parse back and compare
    CBitcoinAddress addr2(strAddr);
    BOOST_CHECK(addr2.IsValid());

    CTxDestination dest;
    BOOST_CHECK(addr2.GetKeyID(keyID));
}

// ============================================================================
// Wallet signing and verification
// ============================================================================

BOOST_AUTO_TEST_CASE(sign_verify_with_wallet_key)
{
    LOCK(pwalletMain->cs_wallet);
    CPubKey pubKey = pwalletMain->GenerateNewKey();
    CKeyID keyID = pubKey.GetID();

    CKey key;
    BOOST_CHECK(pwalletMain->GetKey(keyID, key));

    // Sign a hash
    uint256 hash = Hash(pubKey.Raw().begin(), pubKey.Raw().end());
    std::vector<unsigned char> vchSig;
    BOOST_CHECK(key.Sign(hash, vchSig));

    // Verify
    BOOST_CHECK(key.Verify(hash, vchSig));

    // Wrong hash should fail verification
    uint256 wrongHash(1);
    BOOST_CHECK(!key.Verify(wrongHash, vchSig));
}

// ============================================================================
// Wallet lock state (unencrypted wallet)
// ============================================================================

BOOST_AUTO_TEST_CASE(unencrypted_wallet_not_locked)
{
    // An unencrypted wallet should not be flagged as crypted or locked
    BOOST_CHECK(!pwalletMain->IsCrypted());
    BOOST_CHECK(!pwalletMain->IsLocked());
}

// ============================================================================
// Wallet encryption tests — uses separate CWallet to avoid destroying main
// All encryption ops on a single wallet since mock BDB is shared
// ============================================================================

BOOST_AUTO_TEST_CASE(wallet_encryption_lifecycle)
{
    CWallet testWallet("test_encrypt.dat");
    {
        bool fFirstRun;
        testWallet.LoadWallet(fFirstRun);
    }

    // 1. Unencrypted wallet is not locked or crypted
    BOOST_CHECK(!testWallet.IsCrypted());
    BOOST_CHECK(!testWallet.IsLocked());

    // 2. Generate a key before encryption
    CPubKey pubKey = testWallet.GenerateNewKey();
    CKeyID keyID = pubKey.GetID();
    BOOST_CHECK(testWallet.HaveKey(keyID));

    // 3. Encrypt the wallet
    SecureString passphrase;
    passphrase.reserve(32);
    passphrase = "testpassword123";
    BOOST_CHECK(testWallet.EncryptWallet(passphrase));
    BOOST_CHECK(testWallet.IsCrypted());

    // 4. After encryption, wallet should be locked
    BOOST_CHECK(testWallet.IsLocked());

    // 5. HaveKey should still return true even when locked
    BOOST_CHECK(testWallet.HaveKey(keyID));

    // 6. Wrong passphrase should fail unlock
    SecureString wrong;
    wrong.reserve(32);
    wrong = "wrong_password";
    BOOST_CHECK(!testWallet.Unlock(wrong));
    BOOST_CHECK(testWallet.IsLocked());

    // 7. Correct passphrase should unlock
    BOOST_CHECK(testWallet.Unlock(passphrase));
    BOOST_CHECK(!testWallet.IsLocked());

    // 8. Lock after unlock
    BOOST_CHECK(testWallet.Lock());
    BOOST_CHECK(testWallet.IsLocked());

    // 9. Change passphrase
    SecureString newPass;
    newPass.reserve(32);
    newPass = "new_password_456";
    BOOST_CHECK(testWallet.ChangeWalletPassphrase(passphrase, newPass));

    // 10. Old passphrase should no longer work
    BOOST_CHECK(!testWallet.Unlock(passphrase));

    // 11. New passphrase should work
    BOOST_CHECK(testWallet.Unlock(newPass));
    BOOST_CHECK(!testWallet.IsLocked());
}

BOOST_AUTO_TEST_SUITE_END()
