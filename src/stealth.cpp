// Copyright (c) 2014 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "stealth.h"
#include "base58.h"

#include <openssl/rand.h>
#include <openssl/evp.h>

#include <secp256k1.h>

// Defined in key.cpp — lazily initializes the secp256k1 context
extern secp256k1_context* GetContext();

//const uint8_t stealth_version_byte = 0x2a;
const uint8_t stealth_version_byte = 0x28;


bool CStealthAddress::SetEncoded(const std::string& encodedAddress)
{
    data_chunk raw;

    if (!DecodeBase58(encodedAddress, raw))
    {
        if (fDebug)
            printf("CStealthAddress::SetEncoded DecodeBase58 falied.\n");
        return false;
    };

    if (!VerifyChecksum(raw))
    {
        if (fDebug)
            printf("CStealthAddress::SetEncoded verify_checksum falied.\n");
        return false;
    };

    if (raw.size() < 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
    {
        if (fDebug)
            printf("CStealthAddress::SetEncoded() too few bytes provided.\n");
        return false;
    };


    uint8_t* p = &raw[0];
    uint8_t version = *p++;

    if (version != stealth_version_byte)
    {
        printf("CStealthAddress::SetEncoded version mismatch 0x%x != 0x%x.\n", version, stealth_version_byte);
        return false;
    };

    options = *p++;

    scan_pubkey.resize(33);
    memcpy(&scan_pubkey[0], p, 33);
    p += 33;
    //uint8_t spend_pubkeys = *p++;
    p++;

    spend_pubkey.resize(33);
    memcpy(&spend_pubkey[0], p, 33);

    return true;
};

std::string CStealthAddress::Encoded() const
{
    // https://wiki.unsystem.net/index.php/DarkWallet/Stealth#Address_format
    // [version] [options] [scan_key] [N] ... [Nsigs] [prefix_length] ...

    data_chunk raw;
    raw.push_back(stealth_version_byte);

    raw.push_back(options);

    raw.insert(raw.end(), scan_pubkey.begin(), scan_pubkey.end());
    raw.push_back(1); // number of spend pubkeys
    raw.insert(raw.end(), spend_pubkey.begin(), spend_pubkey.end());
    raw.push_back(0); // number of signatures
    raw.push_back(0); // ?

    AppendChecksum(raw);

    return EncodeBase58(raw);
};


uint32_t BitcoinChecksum(uint8_t* p, uint32_t nBytes)
{
    if (!p || nBytes == 0)
        return 0;

    uint8_t hash1[32];
    EVP_Digest(p, nBytes, (uint8_t*)hash1, nullptr, EVP_sha256(), nullptr);
    uint8_t hash2[32];
    EVP_Digest((uint8_t*)hash1, sizeof(hash1), (uint8_t*)hash2, nullptr, EVP_sha256(), nullptr);

    // -- checksum is the 1st 4 bytes of the hash
    uint32_t checksum = from_little_endian<uint32_t>(&hash2[0]);

    return checksum;
};

void AppendChecksum(data_chunk& data)
{
    uint32_t checksum = BitcoinChecksum(&data[0], data.size());

    // -- to_little_endian
    std::vector<uint8_t> tmp(4);

    //memcpy(&tmp[0], &checksum, 4);
    for (int i = 0; i < 4; ++i)
    {
        tmp[i] = checksum & 0xFF;
        checksum >>= 8;
    };

    data.insert(data.end(), tmp.begin(), tmp.end());
};

bool VerifyChecksum(const data_chunk& data)
{
    if (data.size() < 4)
        return false;

    uint32_t checksum = from_little_endian<uint32_t>(data.end() - 4);

    return BitcoinChecksum((uint8_t*)&data[0], data.size()-4) == checksum;
};


int GenerateRandomSecret(ec_secret& out)
{
    RandAddSeedPerfmon();

    static uint256 max("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140");
    static uint256 min(16000); // increase? min valid key is 1

    uint256 test;

    int i;
    // -- check max, try max 32 times
    for (i = 0; i < 32; ++i)
    {
        RAND_bytes((unsigned char*) test.begin(), 32);
        if (test > min && test < max)
        {
            memcpy(&out.e[0], test.begin(), 32);
            break;
        };
    };

    if (i > 31)
    {
        printf("Error: GenerateRandomSecret failed to generate a valid key.\n");
        return 1;
    };

    return 0;
};

int SecretToPublicKey(const ec_secret& secret, ec_point& out)
{
    // -- public key = private * G
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(GetContext(), &pubkey, &secret.e[0]))
    {
        printf("SecretToPublicKey(): secp256k1_ec_pubkey_create failed.\n");
        return 1;
    }

    out.resize(ec_compressed_size);
    size_t outlen = ec_compressed_size;
    secp256k1_ec_pubkey_serialize(GetContext(), &out[0], &outlen, &pubkey, SECP256K1_EC_COMPRESSED);

    return 0;
};


int StealthSecret(ec_secret& secret, ec_point& pubkey, const ec_point& pkSpend, ec_secret& sharedSOut, ec_point& pkOut)
{
    /*
    send:
        secret = ephem_secret, pubkey = scan_pubkey
    receive:
        secret = scan_secret, pubkey = ephem_pubkey
        c = H(dP)

    Q = public scan key (EC point, 33 bytes)
    d = private scan key (integer, 32 bytes)
    R = public spend key
    f = private spend key

    Sender (has Q and R, not d or f):
    P = eG
    c = H(eQ) = H(dP)
    R' = R + cG
    */

    // -- Parse the scan/ephem public key
    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_parse(GetContext(), &pk, &pubkey[0], pubkey.size()))
    {
        printf("StealthSecret(): secp256k1_ec_pubkey_parse failed\n");
        return 1;
    }

    // -- Compute shared point: secret * pubkey (scalar multiplication)
    // secp256k1_ec_pubkey_tweak_mul computes: pk = secret * pk
    if (!secp256k1_ec_pubkey_tweak_mul(GetContext(), &pk, &secret.e[0]))
    {
        printf("StealthSecret(): secp256k1_ec_pubkey_tweak_mul failed\n");
        return 1;
    }

    // -- Serialize shared point to compressed bytes
    std::vector<uint8_t> vchOutQ(ec_compressed_size);
    size_t outlen = ec_compressed_size;
    secp256k1_ec_pubkey_serialize(GetContext(), &vchOutQ[0], &outlen, &pk, SECP256K1_EC_COMPRESSED);

    // -- Hash compressed shared point: c = H(eQ)
    EVP_Digest(&vchOutQ[0], vchOutQ.size(), &sharedSOut.e[0], nullptr, EVP_sha256(), nullptr);

    // -- Compute cG (shared secret scalar * generator)
    secp256k1_pubkey cG;
    if (!secp256k1_ec_pubkey_create(GetContext(), &cG, &sharedSOut.e[0]))
    {
        printf("StealthSecret(): secp256k1_ec_pubkey_create for cG failed\n");
        return 1;
    }

    // -- Parse spend public key R
    secp256k1_pubkey R;
    if (!secp256k1_ec_pubkey_parse(GetContext(), &R, &pkSpend[0], pkSpend.size()))
    {
        printf("StealthSecret(): secp256k1_ec_pubkey_parse for R failed\n");
        return 1;
    }

    // -- Compute R' = R + cG (point addition)
    const secp256k1_pubkey* pubkeys[2] = { &R, &cG };
    secp256k1_pubkey Rout;
    if (!secp256k1_ec_pubkey_combine(GetContext(), &Rout, pubkeys, 2))
    {
        printf("StealthSecret(): secp256k1_ec_pubkey_combine failed\n");
        return 1;
    }

    // -- Serialize R' to compressed bytes
    pkOut.resize(ec_compressed_size);
    outlen = ec_compressed_size;
    secp256k1_ec_pubkey_serialize(GetContext(), &pkOut[0], &outlen, &Rout, SECP256K1_EC_COMPRESSED);

    return 0;
};


int StealthSecretSpend(ec_secret& scanSecret, ec_point& ephemPubkey, ec_secret& spendSecret, ec_secret& secretOut)
{
    /*
    c  = H(dP)
    R' = R + cG     [without decrypting wallet]
       = (f + c)G   [after decryption of wallet]
    */

    // -- Compute shared point: scanSecret * ephemPubkey
    secp256k1_pubkey pk;
    if (!secp256k1_ec_pubkey_parse(GetContext(), &pk, &ephemPubkey[0], ephemPubkey.size()))
    {
        printf("StealthSecretSpend(): secp256k1_ec_pubkey_parse failed\n");
        return 1;
    }

    if (!secp256k1_ec_pubkey_tweak_mul(GetContext(), &pk, &scanSecret.e[0]))
    {
        printf("StealthSecretSpend(): secp256k1_ec_pubkey_tweak_mul failed\n");
        return 1;
    }

    // -- Serialize shared point to compressed bytes
    unsigned char vchOutP[ec_compressed_size];
    size_t outlen = ec_compressed_size;
    secp256k1_ec_pubkey_serialize(GetContext(), vchOutP, &outlen, &pk, SECP256K1_EC_COMPRESSED);

    // -- c = H(dP)
    uint8_t hash1[32];
    EVP_Digest(vchOutP, ec_compressed_size, (uint8_t*)hash1, nullptr, EVP_sha256(), nullptr);

    // -- secretOut = (spendSecret + c) mod order
    memcpy(&secretOut.e[0], &spendSecret.e[0], ec_secret_size);
    if (!secp256k1_ec_seckey_tweak_add(GetContext(), &secretOut.e[0], hash1))
    {
        printf("StealthSecretSpend(): secp256k1_ec_seckey_tweak_add failed (result is zero).\n");
        return 1;
    }

    // Verify result is non-zero
    if (!secp256k1_ec_seckey_verify(GetContext(), &secretOut.e[0]))
    {
        printf("StealthSecretSpend(): result is invalid.\n");
        return 1;
    }

    return 0;
};


int StealthSharedToSecretSpend(ec_secret& sharedS, ec_secret& spendSecret, ec_secret& secretOut)
{
    // -- secretOut = (spendSecret + sharedS) mod order
    memcpy(&secretOut.e[0], &spendSecret.e[0], ec_secret_size);
    if (!secp256k1_ec_seckey_tweak_add(GetContext(), &secretOut.e[0], &sharedS.e[0]))
    {
        printf("StealthSharedToSecretSpend(): secp256k1_ec_seckey_tweak_add failed.\n");
        return 1;
    }

    if (!secp256k1_ec_seckey_verify(GetContext(), &secretOut.e[0]))
    {
        printf("StealthSharedToSecretSpend(): result is invalid.\n");
        return 1;
    }

    return 0;
};

bool IsStealthAddress(const std::string& encodedAddress)
{
    data_chunk raw;

    if (!DecodeBase58(encodedAddress, raw))
    {
        //printf("IsStealthAddress DecodeBase58 falied.\n");
        return false;
    };

    if (!VerifyChecksum(raw))
    {
        //printf("IsStealthAddress verify_checksum falied.\n");
        return false;
    };

    if (raw.size() < 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
    {
        //printf("IsStealthAddress too few bytes provided.\n");
        return false;
    };


    uint8_t* p = &raw[0];
    uint8_t version = *p++;

    if (version != stealth_version_byte)
    {
        //printf("IsStealthAddress version mismatch 0x%x != 0x%x.\n", version, stealth_version_byte);
        return false;
    };

    return true;
};
