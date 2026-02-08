// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "crypter.h"
#include "wallet.h"
#include "uint256.h"
#include "util.h"

#include <string>

BOOST_AUTO_TEST_SUITE(crypter_tests)

// ============================================================================
// CCrypter constants
// ============================================================================

BOOST_AUTO_TEST_CASE(crypto_constants)
{
    BOOST_CHECK_EQUAL(WALLET_CRYPTO_KEY_SIZE, 32u);
    BOOST_CHECK_EQUAL(WALLET_CRYPTO_SALT_SIZE, 8u);
}

// ============================================================================
// CMasterKey construction
// ============================================================================

BOOST_AUTO_TEST_CASE(masterkey_default_constructor)
{
    CMasterKey mk;
    // Default constructor uses scrypt method (1) with 25000 iterations
    BOOST_CHECK_EQUAL(mk.nDerivationMethod, 1u);
    BOOST_CHECK_EQUAL(mk.nDeriveIterations, 25000u);
    BOOST_CHECK(mk.vchCryptedKey.empty());
    BOOST_CHECK(mk.vchSalt.empty());
    BOOST_CHECK(mk.vchOtherDerivationParameters.empty());
}

BOOST_AUTO_TEST_CASE(masterkey_sha512_constructor)
{
    CMasterKey mk(0);
    BOOST_CHECK_EQUAL(mk.nDerivationMethod, 0u);
    BOOST_CHECK_EQUAL(mk.nDeriveIterations, 25000u);
}

BOOST_AUTO_TEST_CASE(masterkey_scrypt_constructor)
{
    CMasterKey mk(1);
    BOOST_CHECK_EQUAL(mk.nDerivationMethod, 1u);
    BOOST_CHECK_EQUAL(mk.nDeriveIterations, 10000u);
}

BOOST_AUTO_TEST_CASE(masterkey_serialization_roundtrip)
{
    CMasterKey mk;
    mk.vchCryptedKey.assign(32, 0xAB);
    mk.vchSalt.assign(WALLET_CRYPTO_SALT_SIZE, 0xCD);
    mk.nDerivationMethod = 1;
    mk.nDeriveIterations = 50000;

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << mk;

    CMasterKey mk2;
    ss >> mk2;

    BOOST_CHECK(mk2.vchCryptedKey == mk.vchCryptedKey);
    BOOST_CHECK(mk2.vchSalt == mk.vchSalt);
    BOOST_CHECK_EQUAL(mk2.nDerivationMethod, mk.nDerivationMethod);
    BOOST_CHECK_EQUAL(mk2.nDeriveIterations, mk.nDeriveIterations);
}

// ============================================================================
// CCrypter SetKeyFromPassphrase
// ============================================================================

BOOST_AUTO_TEST_CASE(set_key_from_passphrase_sha512)
{
    CCrypter crypt;
    SecureString passphrase("test passphrase");
    std::vector<unsigned char> salt(WALLET_CRYPTO_SALT_SIZE, 0x42);

    // Method 0 = EVP_sha512
    BOOST_CHECK(crypt.SetKeyFromPassphrase(passphrase, salt, 100, 0));
}

BOOST_AUTO_TEST_CASE(set_key_from_passphrase_scrypt)
{
    CCrypter crypt;
    SecureString passphrase("test passphrase");
    std::vector<unsigned char> salt(WALLET_CRYPTO_SALT_SIZE, 0x42);

    // Method 1 = scrypt + sha512
    BOOST_CHECK(crypt.SetKeyFromPassphrase(passphrase, salt, 100, 1));
}

BOOST_AUTO_TEST_CASE(set_key_rejects_zero_rounds)
{
    CCrypter crypt;
    SecureString passphrase("test");
    std::vector<unsigned char> salt(WALLET_CRYPTO_SALT_SIZE, 0x42);

    // nRounds < 1 should fail
    BOOST_CHECK(!crypt.SetKeyFromPassphrase(passphrase, salt, 0, 0));
}

BOOST_AUTO_TEST_CASE(set_key_rejects_wrong_salt_size)
{
    CCrypter crypt;
    SecureString passphrase("test");

    // Salt must be exactly WALLET_CRYPTO_SALT_SIZE (8) bytes
    std::vector<unsigned char> badSalt(4, 0x42);
    BOOST_CHECK(!crypt.SetKeyFromPassphrase(passphrase, badSalt, 100, 0));

    std::vector<unsigned char> bigSalt(16, 0x42);
    BOOST_CHECK(!crypt.SetKeyFromPassphrase(passphrase, bigSalt, 100, 0));
}

// ============================================================================
// CCrypter encrypt/decrypt round-trip
// ============================================================================

BOOST_AUTO_TEST_CASE(encrypt_decrypt_roundtrip_sha512)
{
    CCrypter crypt;
    SecureString passphrase("my wallet passphrase");
    std::vector<unsigned char> salt(WALLET_CRYPTO_SALT_SIZE, 0x55);

    BOOST_CHECK(crypt.SetKeyFromPassphrase(passphrase, salt, 100, 0));

    // Encrypt
    CKeyingMaterial plaintext(32, 0xAA);
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypt.Encrypt(plaintext, ciphertext));

    // Ciphertext should differ from plaintext
    BOOST_CHECK(ciphertext.size() > 0);
    BOOST_CHECK(ciphertext != std::vector<unsigned char>(plaintext.begin(), plaintext.end()));

    // Decrypt
    CKeyingMaterial decrypted;
    BOOST_CHECK(crypt.Decrypt(ciphertext, decrypted));

    // Round-trip: decrypted must equal original plaintext
    BOOST_CHECK(decrypted == plaintext);
}

BOOST_AUTO_TEST_CASE(encrypt_decrypt_roundtrip_scrypt)
{
    CCrypter crypt;
    SecureString passphrase("scrypt passphrase test");
    std::vector<unsigned char> salt(WALLET_CRYPTO_SALT_SIZE, 0x77);

    BOOST_CHECK(crypt.SetKeyFromPassphrase(passphrase, salt, 100, 1));

    CKeyingMaterial plaintext(32, 0xBB);
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypt.Encrypt(plaintext, ciphertext));

    CKeyingMaterial decrypted;
    BOOST_CHECK(crypt.Decrypt(ciphertext, decrypted));
    BOOST_CHECK(decrypted == plaintext);
}

BOOST_AUTO_TEST_CASE(wrong_passphrase_fails_decrypt)
{
    std::vector<unsigned char> salt(WALLET_CRYPTO_SALT_SIZE, 0x33);

    // Encrypt with passphrase A
    CCrypter cryptA;
    SecureString passphraseA("correct passphrase");
    BOOST_CHECK(cryptA.SetKeyFromPassphrase(passphraseA, salt, 100, 0));

    CKeyingMaterial plaintext(32, 0xCC);
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(cryptA.Encrypt(plaintext, ciphertext));

    // Try to decrypt with passphrase B
    CCrypter cryptB;
    SecureString passphraseB("wrong passphrase");
    BOOST_CHECK(cryptB.SetKeyFromPassphrase(passphraseB, salt, 100, 0));

    CKeyingMaterial decrypted;
    // Decrypt may succeed (AES padding can sometimes pass) but result will differ
    // or it may fail entirely — either way, it must not match the original
    if (cryptB.Decrypt(ciphertext, decrypted)) {
        BOOST_CHECK(decrypted != plaintext);
    }
    // If Decrypt returns false, that's also a valid "wrong passphrase" outcome
}

BOOST_AUTO_TEST_CASE(different_salts_produce_different_ciphertext)
{
    SecureString passphrase("same passphrase");
    CKeyingMaterial plaintext(32, 0xDD);

    // Encrypt with salt A
    CCrypter cryptA;
    std::vector<unsigned char> saltA(WALLET_CRYPTO_SALT_SIZE, 0x11);
    BOOST_CHECK(cryptA.SetKeyFromPassphrase(passphrase, saltA, 100, 0));
    std::vector<unsigned char> ciphertextA;
    BOOST_CHECK(cryptA.Encrypt(plaintext, ciphertextA));

    // Encrypt with salt B
    CCrypter cryptB;
    std::vector<unsigned char> saltB(WALLET_CRYPTO_SALT_SIZE, 0x22);
    BOOST_CHECK(cryptB.SetKeyFromPassphrase(passphrase, saltB, 100, 0));
    std::vector<unsigned char> ciphertextB;
    BOOST_CHECK(cryptB.Encrypt(plaintext, ciphertextB));

    // Different salts should produce different ciphertext
    BOOST_CHECK(ciphertextA != ciphertextB);
}

// ============================================================================
// CCrypter SetKey (direct key/IV)
// ============================================================================

BOOST_AUTO_TEST_CASE(set_key_direct)
{
    CCrypter crypt;
    CKeyingMaterial key(WALLET_CRYPTO_KEY_SIZE, 0xAA);
    std::vector<unsigned char> iv(WALLET_CRYPTO_KEY_SIZE, 0xBB);

    BOOST_CHECK(crypt.SetKey(key, iv));

    CKeyingMaterial plaintext(64, 0xEE);
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypt.Encrypt(plaintext, ciphertext));

    CKeyingMaterial decrypted;
    BOOST_CHECK(crypt.Decrypt(ciphertext, decrypted));
    BOOST_CHECK(decrypted == plaintext);
}

BOOST_AUTO_TEST_CASE(set_key_rejects_wrong_sizes)
{
    CCrypter crypt;

    // Key too small
    CKeyingMaterial shortKey(16, 0xAA);
    std::vector<unsigned char> iv(WALLET_CRYPTO_KEY_SIZE, 0xBB);
    BOOST_CHECK(!crypt.SetKey(shortKey, iv));

    // IV too small
    CKeyingMaterial goodKey(WALLET_CRYPTO_KEY_SIZE, 0xAA);
    std::vector<unsigned char> shortIV(16, 0xBB);
    BOOST_CHECK(!crypt.SetKey(goodKey, shortIV));
}

// ============================================================================
// EncryptSecret / DecryptSecret (wallet private key encryption)
// ============================================================================

BOOST_AUTO_TEST_CASE(encrypt_decrypt_secret_roundtrip)
{
    // Generate a real key to get a realistic secret
    CKey key;
    key.MakeNewKey(true);
    bool fCompressed;
    CSecret secret = key.GetSecret(fCompressed);
    BOOST_CHECK_EQUAL(secret.size(), 32u);

    // Create a master key
    CKeyingMaterial vMasterKey(WALLET_CRYPTO_KEY_SIZE);
    for (unsigned int i = 0; i < WALLET_CRYPTO_KEY_SIZE; i++)
        vMasterKey[i] = (unsigned char)(i * 7 + 3);

    // Use the public key hash as IV (same as production code)
    CPubKey pubKey = key.GetPubKey();
    uint256 nIV = pubKey.GetHash();

    // Encrypt
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(EncryptSecret(vMasterKey, secret, nIV, ciphertext));
    BOOST_CHECK(!ciphertext.empty());

    // Decrypt
    CSecret decrypted;
    BOOST_CHECK(DecryptSecret(vMasterKey, ciphertext, nIV, decrypted));

    // Round-trip: must recover the original secret
    BOOST_CHECK(decrypted == secret);
}

BOOST_AUTO_TEST_CASE(encrypt_secret_different_iv)
{
    CSecret secret(32, 0xFF);

    CKeyingMaterial vMasterKey(WALLET_CRYPTO_KEY_SIZE, 0x42);

    uint256 iv1(1);
    uint256 iv2(2);

    std::vector<unsigned char> cipher1, cipher2;
    BOOST_CHECK(EncryptSecret(vMasterKey, secret, iv1, cipher1));
    BOOST_CHECK(EncryptSecret(vMasterKey, secret, iv2, cipher2));

    // Different IVs should produce different ciphertext
    BOOST_CHECK(cipher1 != cipher2);
}

BOOST_AUTO_TEST_CASE(decrypt_with_wrong_key_fails)
{
    CSecret secret(32, 0xAA);

    CKeyingMaterial vMasterKey1(WALLET_CRYPTO_KEY_SIZE, 0x11);
    CKeyingMaterial vMasterKey2(WALLET_CRYPTO_KEY_SIZE, 0x22);
    uint256 nIV(42);

    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(EncryptSecret(vMasterKey1, secret, nIV, ciphertext));

    // Decrypt with wrong key
    CSecret decrypted;
    if (DecryptSecret(vMasterKey2, ciphertext, nIV, decrypted)) {
        // If decryption "succeeds" with wrong key, result must differ
        BOOST_CHECK(decrypted != secret);
    }
}

// ============================================================================
// CKeyMetadata
// ============================================================================

BOOST_AUTO_TEST_CASE(keymetadata_default)
{
    CKeyMetadata meta;
    BOOST_CHECK_EQUAL(meta.nVersion, 1);
    BOOST_CHECK_EQUAL(meta.nCreateTime, 0);
}

BOOST_AUTO_TEST_CASE(keymetadata_with_time)
{
    int64_t now = 1700000000;
    CKeyMetadata meta(now);
    BOOST_CHECK_EQUAL(meta.nVersion, 1);
    BOOST_CHECK_EQUAL(meta.nCreateTime, now);
}

BOOST_AUTO_TEST_CASE(keymetadata_serialization)
{
    CKeyMetadata meta(1700000000);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << meta;

    CKeyMetadata meta2;
    ss >> meta2;

    BOOST_CHECK_EQUAL(meta2.nVersion, meta.nVersion);
    BOOST_CHECK_EQUAL(meta2.nCreateTime, meta.nCreateTime);
}

BOOST_AUTO_TEST_SUITE_END()
