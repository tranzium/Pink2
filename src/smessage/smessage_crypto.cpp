// Copyright (c) 2014 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Cryptographic operations extracted from smessage.cpp:
//   SecMsgCrypter methods, SecureMsgValidate, SecureMsgSetHash,
//   SecureMspinkcrypt, SecureMsgDecrypt

#include "smessage/smessage.h"

#include <stdint.h>
#include <string.h>

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <secp256k1.h>
#include <secp256k1_ecdh.h>

#include "base58.h"
#include "init.h" // pwalletMain


// On 64 bit system ld is 64bits
#ifdef IS_ARCH_64
#undef  PRId64
#undef  PRIu64
#undef  PRIx64
#define  PRId64  "ld"
#define  PRIu64  "lu"
#define  PRIx64  "lx"
#endif // IS_ARCH_64


bool SecMsgCrypter::SetKey(const std::vector<unsigned char>& vchNewKey, unsigned char* chNewIV)
{

    if (vchNewKey.size() < sizeof(chKey))
        return false;

    return SetKey(&vchNewKey[0], chNewIV);
};

bool SecMsgCrypter::SetKey(const unsigned char* chNewKey, unsigned char* chNewIV)
{
    // -- for EVP_aes_256_cbc() key must be 256 bit, iv must be 128 bit.
    memcpy(&chKey[0], chNewKey, sizeof(chKey));
    memcpy(chIV, chNewIV, sizeof(chIV));

    fKeySet = true;
    return true;
};

bool SecMsgCrypter::Encrypt(unsigned char* chPlaintext, uint32_t nPlain, std::vector<unsigned char> &vchCiphertext)
{
    if (!fKeySet)
        return false;

    // -- max ciphertext len for a n bytes of plaintext is n + AES_BLOCK_SIZE - 1 bytes
    int nLen = nPlain;

    int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
    vchCiphertext = std::vector<unsigned char> (nCLen);

    bool fOk = true;

    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new());
    if(!ctx)
        throw std::runtime_error("Error allocating cipher context");

    if (fOk) fOk = EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, &chKey[0], &chIV[0]);
    if (fOk) fOk = EVP_EncryptUpdate(ctx.get(), &vchCiphertext[0], &nCLen, chPlaintext, nLen);
    if (fOk) fOk = EVP_EncryptFinal_ex(ctx.get(), (&vchCiphertext[0])+nCLen, &nFLen);

    if (!fOk)
        return false;

    vchCiphertext.resize(nCLen + nFLen);

    return true;
};

bool SecMsgCrypter::Decrypt(unsigned char* chCiphertext, uint32_t nCipher, std::vector<unsigned char>& vchPlaintext)
{
    if (!fKeySet)
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    int nPLen = nCipher, nFLen = 0;

    vchPlaintext.resize(nCipher);

    bool fOk = true;

    EVP_CIPHER_CTX_ptr ctx(EVP_CIPHER_CTX_new());
    if(!ctx)
        throw std::runtime_error("Error allocating cipher context");

    if (fOk) fOk = EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, &chKey[0], &chIV[0]);
    if (fOk) fOk = EVP_DecryptUpdate(ctx.get(), &vchPlaintext[0], &nPLen, &chCiphertext[0], nCipher);
    if (fOk) fOk = EVP_DecryptFinal_ex(ctx.get(), (&vchPlaintext[0])+nPLen, &nFLen);

    if (!fOk)
        return false;

    vchPlaintext.resize(nPLen + nFLen);

    return true;
};

int SecureMsgValidate(unsigned char *pHeader, unsigned char *pPayload, uint32_t nPayload)
{
    /*
    returns
        0 success
        1 error
        2 invalid hash
        3 checksum mismatch
        4 invalid version
        5 payload is too large
    */
    SecureMessage* psmsg = (SecureMessage*) pHeader;

    if (psmsg->version[0] != 1)
        return 4;

    if (nPayload > SMSG_MAX_MSG_WORST)
        return 5;

    unsigned char civ[32];
    unsigned char sha256Hash[32];
    int rv = 2; // invalid

    uint32_t nonse;
    memcpy(&nonse, &psmsg->nonse[0], 4);

    if (fDebugSmsg)
        printf("SecureMsgValidate() nonse %u.\n", nonse);

    for (int i = 0; i < 32; i+=4)
        memcpy(civ+i, &nonse, 4);


    EVP_MAC_ptr mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr));
    EVP_MAC_CTX_ptr ctx(EVP_MAC_CTX_new(mac.get()));
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };
    size_t outlen = 0;
    if (!ctx
        || !EVP_MAC_init(ctx.get(), &civ[0], 32, params)
        || !EVP_MAC_update(ctx.get(), reinterpret_cast<const unsigned char*>(pHeader)+4, SMSG_HDR_LEN-4)
        || !EVP_MAC_update(ctx.get(), reinterpret_cast<const unsigned char*>(pPayload), nPayload)
        || !EVP_MAC_update(ctx.get(), pPayload, nPayload)
        || !EVP_MAC_final(ctx.get(), sha256Hash, &outlen, sizeof(sha256Hash))
        || outlen != 32)
    {
        if (fDebugSmsg)
            printf("HMAC error.\n");
        rv = 1; // error
    } else
    {
        if (sha256Hash[31] == 0
            && sha256Hash[30] == 0
            && (~(sha256Hash[29]) & ((1<<0) | (1<<1) | (1<<2)) ))
        {
            if (fDebugSmsg)
                printf("Hash Valid.\n");
            rv = 0; // smsg is valid
        };

        if (memcmp(psmsg->hash, sha256Hash, 4) != 0)
        {
             if (fDebugSmsg)
                printf("Checksum mismatch.\n");
            rv = 3; // checksum mismatch
        }
    }

    return rv;
};

int SecureMsgSetHash(unsigned char *pHeader, unsigned char *pPayload, uint32_t nPayload)
{
    /*  proof of work and checksum

        May run in a thread, if shutdown detected, return.

        returns:
            0 success
            1 error
            2 stopped due to node shutdown

    */

    SecureMessage* psmsg = (SecureMessage*) pHeader;

    int64_t nStart = GetTimeMillis();
    unsigned char civ[32];
    unsigned char sha256Hash[32];

    //std::vector<unsigned char> vchHash;
    //vchHash.resize(32);

    bool found = false;

    EVP_MAC_ptr mac(EVP_MAC_fetch(nullptr, "HMAC", nullptr));
    EVP_MAC_CTX_ptr ctx(EVP_MAC_CTX_new(mac.get()));
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };

    uint32_t nonse = 0;

    // -- break for EVP_MAC cleanup
    for (;;)
    {
        if (!fSecMsgenabled)
           break;

        memcpy(&psmsg->nonse[0], &nonse, 4);

        for (int i = 0; i < 32; i+=4)
            memcpy(civ+i, &nonse, 4);

        size_t outlen = 0;
        if (!ctx
            || !EVP_MAC_init(ctx.get(), &civ[0], 32, params)
            || !EVP_MAC_update(ctx.get(), reinterpret_cast<const unsigned char*>(pHeader)+4, SMSG_HDR_LEN-4)
            || !EVP_MAC_update(ctx.get(), reinterpret_cast<const unsigned char*>(pPayload), nPayload)
            || !EVP_MAC_update(ctx.get(), pPayload, nPayload)
            || !EVP_MAC_final(ctx.get(), sha256Hash, &outlen, sizeof(sha256Hash))
            || outlen != 32)
            break;

        if (sha256Hash[31] == 0
            && sha256Hash[30] == 0
            && (~(sha256Hash[29]) & ((1<<0) | (1<<1) | (1<<2)) ))
        {
            found = true;
            break;
        }

        if (nonse >= 4294967295U)
        {
            if (fDebugSmsg)
                printf("No match %u\n", nonse);
            break;
        }
        nonse++;
    };

    if (!fSecMsgenabled)
    {
        if (fDebugSmsg)
            printf("SecureMsgSetHash() stopped, shutdown detected.\n");
        return 2;
    };

    if (!found)
    {
        if (fDebugSmsg)
            printf("SecureMsgSetHash() failed, took %" PRId64 " ms, nonse %u\n", GetTimeMillis() - nStart, nonse);
        return 1;
    };

    memcpy(psmsg->hash, sha256Hash, 4);
    //memcpy(psmsg->hash, &vchHash[0], 4);

    if (fDebugSmsg)
        printf("SecureMsgSetHash() took %" PRId64 " ms, nonse %u\n", GetTimeMillis() - nStart, nonse);

    return 0;
};

int SecureMspinkcrypt(SecureMessage& smsg, std::string& addressFrom, std::string& addressTo, std::string& message)
{
    /* Create a secure message

        Using similar method to bitmessage.
        If bitmessage is secure this should be too.
        https://bitmessage.org/wiki/Encryption

        Some differences:
        bitmessage seems to use curve sect283r1
        *coin addresses use secp256k1

        returns
            2       message is too long.
            3       addressFrom is invalid.
            4       addressTo is invalid.
            5       Could not get public key for addressTo.
            6       ECDH_compute_key failed
            7       Could not get private key for addressFrom.
            8       Could not allocate memory.
            9       Could not compress message data.
            10      Could not generate MAC.
            11      Encrypt failed.
    */

    if (fDebugSmsg)
        printf("SecureMspinkcrypt(%s, %s, ...)\n", addressFrom.c_str(), addressTo.c_str());


    if (message.size() > SMSG_MAX_MSG_BYTES)
    {
        printf("Message is too long, %" PRIszu ".\n", message.size());
        return 2;
    };

    smsg.version[0] = 1;
    smsg.version[1] = 1;
    smsg.timestamp = GetAdjustedTime();


    bool fSendAnonymous;
    CBitcoinAddress coinAddrFrom;
    CKeyID ckidFrom;
    CKey keyFrom;

    if (addressFrom.compare("anon") == 0)
    {
        fSendAnonymous = true;

    } else
    {
        fSendAnonymous = false;

        if (!coinAddrFrom.SetString(addressFrom))
        {
            printf("addressFrom is not valid.\n");
            return 3;
        };

        if (!coinAddrFrom.GetKeyID(ckidFrom))
        {
            printf("coinAddrFrom.GetKeyID failed: %s.\n", coinAddrFrom.ToString().c_str());
            return 3;
        };
    };


    CBitcoinAddress coinAddrDest;
    CKeyID ckidDest;

    if (!coinAddrDest.SetString(addressTo))
    {
        printf("addressTo is not valid.\n");
        return 4;
    };

    if (!coinAddrDest.GetKeyID(ckidDest))
    {
        printf("coinAddrDest.GetKeyID failed: %s.\n", coinAddrDest.ToString().c_str());
        return 4;
    };

    // -- public key K is the destination address
    CPubKey cpkDestK;
    if (SecureMsgGetStoredKey(ckidDest, cpkDestK) != 0
        && SecureMsgGetLocalKey(ckidDest, cpkDestK) != 0) // maybe it's a local key (outbox?)
    {
        printf("Could not get public key for destination address.\n");
        return 5;
    };


    // -- Generate 16 random bytes as IV.
    RandAddSeedPerfmon();
    RAND_bytes(&smsg.iv[0], 16);


    // -- Generate a new random EC key pair with private key called r and public key called R.
    CKey keyR;
    keyR.MakeNewKey(true); // make compressed key


    // -- Do an EC point multiply with public key K and private key r. This gives you public key P.
    CKey keyK;
    if (!keyK.SetPubKey(cpkDestK))
    {
        printf("Could not set pubkey for K: %s.\n", ValueString(cpkDestK.Raw()).c_str());
        return 4; // address to is invalid
    };

    // -- ECDH: compute shared secret P = r * K (x-coordinate only)
    std::vector<unsigned char> vchP;
    vchP.resize(32);
    {
        extern secp256k1_context* GetContext();
        secp256k1_context* ctx = GetContext();

        // Custom hash function: return raw x-coordinate (matches ECDH_compute_key with NULL KDF)
        auto ecdh_copy_x = [](unsigned char *output, const unsigned char *x32,
                              const unsigned char * /*y32*/, void * /*data*/) -> int {
            memcpy(output, x32, 32);
            return 1;
        };

        secp256k1_pubkey pubkey;
        std::vector<unsigned char> rawK = cpkDestK.Raw();
        if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, &rawK[0], rawK.size()))
        {
            printf("secp256k1_ec_pubkey_parse failed for K.\n");
            return 6;
        }

        if (!secp256k1_ecdh(ctx, &vchP[0], &pubkey, keyR.begin(), ecdh_copy_x, nullptr))
        {
            printf("secp256k1_ecdh failed.\n");
            return 6;
        }
    }

    CPubKey cpkR = keyR.GetPubKey();
    if (!cpkR.IsValid()
        || !cpkR.IsCompressed())
    {
        printf("Could not get public key for key R.\n");
        return 1;
    };

    memcpy(smsg.cpkR, &cpkR.Raw()[0], 33);


    // -- Use public key P and calculate the SHA512 hash H.
    //    The first 32 bytes of H are called key_e and the last 32 bytes are called key_m.
    std::vector<unsigned char> vchHashed;
    vchHashed.resize(64); // 512
    EVP_Digest(&vchP[0], vchP.size(), reinterpret_cast<unsigned char*>(&vchHashed[0]), nullptr, EVP_sha512(), nullptr);
    std::vector<unsigned char> key_e(&vchHashed[0], &vchHashed[0]+32);
    std::vector<unsigned char> key_m(&vchHashed[32], &vchHashed[32]+32);


    std::vector<unsigned char> vchPayload;
    std::vector<unsigned char> vchCompressed;
    unsigned char* pMsgData;
    uint32_t lenMsgData;

    uint32_t lenMsg = message.size();
    if (lenMsg > 128)
    {
        // -- only compress if over 128 bytes
        int worstCase = LZ4_compressBound(message.size());
        try {
            vchCompressed.resize(worstCase);
        } catch (std::exception& e) {
            printf("vchCompressed.resize %u threw: %s.\n", worstCase, e.what());
            return 8;
        };

        int lenComp = LZ4_compress(reinterpret_cast<const char*>(message.c_str()), reinterpret_cast<char*>(&vchCompressed[0]), lenMsg);
        if (lenComp < 1)
        {
            printf("Could not compress message data.\n");
            return 9;
        };

        pMsgData = &vchCompressed[0];
        lenMsgData = lenComp;

    } else
    {
        // -- no compression
        pMsgData = reinterpret_cast<unsigned char*>(const_cast<char*>(message.c_str()));
        lenMsgData = lenMsg;
    };

    if (fSendAnonymous)
    {
        try {
            vchPayload.resize(9 + lenMsgData);
        } catch (std::exception& e) {
            printf("vchPayload.resize %u threw: %s.\n", 9 + lenMsgData, e.what());
            return 8;
        };

        memcpy(&vchPayload[9], pMsgData, lenMsgData);

        vchPayload[0] = 250; // id as anonymous message
        // -- next 4 bytes are unused - there to ensure encrypted payload always > 8 bytes
        memcpy(&vchPayload[5], &lenMsg, 4); // length of uncompressed plain text
    } else
    {
        try {
            vchPayload.resize(SMSG_PL_HDR_LEN + lenMsgData);
        } catch (std::exception& e) {
            printf("vchPayload.resize %u threw: %s.\n", SMSG_PL_HDR_LEN + lenMsgData, e.what());
            return 8;
        };
        memcpy(&vchPayload[SMSG_PL_HDR_LEN], pMsgData, lenMsgData);
        // -- compact signature proves ownership of from address and allows the public key to be recovered, recipient can always reply.
        if (!pwalletMain->GetKey(ckidFrom, keyFrom))
        {
            printf("Could not get private key for addressFrom.\n");
            return 7;
        };

        // -- sign the plaintext
        std::vector<unsigned char> vchSignature;
        vchSignature.resize(65);
        keyFrom.SignCompact(Hash(message.begin(), message.end()), vchSignature);

        // -- Save some bytes by sending address raw
        vchPayload[0] = (static_cast<CBitcoinAddress_B*>(&coinAddrFrom))->getVersion(); // vchPayload[0] = coinAddrDest.nVersion;
        memcpy(&vchPayload[1], (static_cast<CKeyID_B*>(&ckidFrom))->GetPPN(), 20); // memcpy(&vchPayload[1], ckidDest.pn, 20);

        memcpy(&vchPayload[1+20], &vchSignature[0], vchSignature.size());
        memcpy(&vchPayload[1+20+65], &lenMsg, 4); // length of uncompressed plain text
    };


    SecMsgCrypter crypter;
    crypter.SetKey(key_e, smsg.iv);
    std::vector<unsigned char> vchCiphertext;

    if (!crypter.Encrypt(&vchPayload[0], vchPayload.size(), vchCiphertext))
    {
        printf("crypter.Encrypt failed.\n");
        return 11;
    };

    try {
        smsg.pPayload = new unsigned char[vchCiphertext.size()];
    } catch (std::exception& e)
    {
        printf("Could not allocate pPayload, exception: %s.\n", e.what());
        return 8;
    };

    memcpy(smsg.pPayload, &vchCiphertext[0], vchCiphertext.size());
    smsg.nPayload = vchCiphertext.size();


    // -- Calculate a 32 byte MAC with HMACSHA256, using key_m as salt
    //    Message authentication code, (hash of timestamp + destination + payload)
    bool fHmacOk = true;

    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };
    size_t outlen = 0;
    if (!ctx
        || !EVP_MAC_init(ctx, &key_m[0], 32, params)
        || !EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(&smsg.timestamp), sizeof(smsg.timestamp))
        || !EVP_MAC_update(ctx, &vchCiphertext[0], vchCiphertext.size())
        || !EVP_MAC_final(ctx, smsg.mac, &outlen, sizeof(smsg.mac))
        || outlen != 32)
        fHmacOk = false;

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);

    if (!fHmacOk)
    {
        printf("Could not generate MAC.\n");
        return 10;
    };


    return 0;
};

int SecureMsgDecrypt(bool fTestOnly, std::string& address, unsigned char *pHeader, unsigned char *pPayload, uint32_t nPayload, MessageData& msg)
{
    /* Decrypt secure message

        address is the owned address to decrypt with.

        validate first in SecureMsgValidate

        returns
            1       Error
            2       Unknown version number
            3       Decrypt address is not valid.
            8       Could not allocate memory
    */

    if (fDebugSmsg)
        printf("SecureMsgDecrypt(), using %s, testonly %d.\n", address.c_str(), fTestOnly);

    if (!pHeader
        || !pPayload)
    {
        printf("Error: null pointer to header or payload.\n");
        return 1;
    };

    SecureMessage* psmsg = (SecureMessage*) pHeader;


    if (psmsg->version[0] != 1)
    {
        printf("Unknown version number.\n");
        return 2;
    };



    // -- Fetch private key k, used to decrypt
    CBitcoinAddress coinAddrDest;
    CKeyID ckidDest;
    CKey keyDest;
    if (!coinAddrDest.SetString(address))
    {
        printf("Address is not valid.\n");
        return 3;
    };
    if (!coinAddrDest.GetKeyID(ckidDest))
    {
        printf("coinAddrDest.GetKeyID failed: %s.\n", coinAddrDest.ToString().c_str());
        return 3;
    };
    if (!pwalletMain->GetKey(ckidDest, keyDest))
    {
        printf("Could not get private key for addressDest.\n");
        return 3;
    };



    CKey keyR;
    std::vector<unsigned char> vchR(psmsg->cpkR, psmsg->cpkR+33); // would be neater to override CPubKey() instead
    CPubKey cpkR(vchR);
    if (!cpkR.IsValid())
    {
        printf("Could not get public key for key R.\n");
        return 1;
    };
    if (!keyR.SetPubKey(cpkR))
    {
        printf("Could not set pubkey for R: %s.\n", ValueString(cpkR.Raw()).c_str());
        return 1;
    };

    cpkR = keyR.GetPubKey();
    if (!cpkR.IsValid()
        || !cpkR.IsCompressed())
    {
        printf("Could not get compressed public key for key R.\n");
        return 1;
    };


    // -- ECDH: compute shared secret P = k * R (x-coordinate only)
    std::vector<unsigned char> vchP;
    vchP.resize(32);
    {
        extern secp256k1_context* GetContext();
        secp256k1_context* ctx = GetContext();

        auto ecdh_copy_x = [](unsigned char *output, const unsigned char *x32,
                              const unsigned char * /*y32*/, void * /*data*/) -> int {
            memcpy(output, x32, 32);
            return 1;
        };

        secp256k1_pubkey pubkey;
        std::vector<unsigned char> rawR = cpkR.Raw();
        if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, &rawR[0], rawR.size()))
        {
            printf("secp256k1_ec_pubkey_parse failed for R.\n");
            return 1;
        }

        if (!secp256k1_ecdh(ctx, &vchP[0], &pubkey, keyDest.begin(), ecdh_copy_x, nullptr))
        {
            printf("secp256k1_ecdh failed.\n");
            return 1;
        }
    }


    // -- Use public key P to calculate the SHA512 hash H.
    //    The first 32 bytes of H are called key_e and the last 32 bytes are called key_m.
    std::vector<unsigned char> vchHashedDec;
    vchHashedDec.resize(64);    // 512 bits
    EVP_Digest(&vchP[0], vchP.size(), reinterpret_cast<unsigned char*>(&vchHashedDec[0]), nullptr, EVP_sha512(), nullptr);
    std::vector<unsigned char> key_e(&vchHashedDec[0], &vchHashedDec[0]+32);
    std::vector<unsigned char> key_m(&vchHashedDec[32], &vchHashedDec[32]+32);


    // -- Message authentication code, (hash of timestamp + destination + payload)
    unsigned char MAC[32];
    bool fHmacOk = true;

    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };
    size_t outlen = 0;
    if (!ctx
        || !EVP_MAC_init(ctx, &key_m[0], 32, params)
        || !EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(&psmsg->timestamp), sizeof(psmsg->timestamp))
        || !EVP_MAC_update(ctx, pPayload, nPayload)
        || !EVP_MAC_final(ctx, MAC, &outlen, sizeof(MAC))
        || outlen != 32)
        fHmacOk = false;

    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);

    if (!fHmacOk)
    {
        printf("Could not generate MAC.\n");
        return 1;
    };

    if (memcmp(MAC, psmsg->mac, 32) != 0)
    {
        if (fDebugSmsg)
            printf("MAC does not match.\n"); // expected if message is not to address on node

        return 1;
    };

    if (fTestOnly)
        return 0;

    SecMsgCrypter crypter;
    crypter.SetKey(key_e, psmsg->iv);
    std::vector<unsigned char> vchPayload;
    if (!crypter.Decrypt(pPayload, nPayload, vchPayload))
    {
        printf("Decrypt failed.\n");
        return 1;
    };

    msg.timestamp = psmsg->timestamp;
    uint32_t lenData;
    uint32_t lenPlain;

    unsigned char* pMsgData;
    bool fFromAnonymous;
    if (static_cast<uint32_t>(vchPayload[0]) == 250)
    {
        fFromAnonymous = true;
        lenData = vchPayload.size() - (9);
        memcpy(&lenPlain, &vchPayload[5], 4);
        pMsgData = &vchPayload[9];
    } else
    {
        fFromAnonymous = false;
        lenData = vchPayload.size() - (SMSG_PL_HDR_LEN);
        memcpy(&lenPlain, &vchPayload[1+20+65], 4);
        pMsgData = &vchPayload[SMSG_PL_HDR_LEN];
    };

    try {
        msg.vchMessage.resize(lenPlain + 1);
    } catch (std::exception& e) {
        printf("msg.vchMessage.resize %u threw: %s.\n", lenPlain + 1, e.what());
        return 8;
    };


    if (lenPlain > 128)
    {
        // -- decompress
        if (LZ4_decompress_safe(reinterpret_cast<char*>(pMsgData), reinterpret_cast<char*>(&msg.vchMessage[0]), lenData, lenPlain) != static_cast<int>(lenPlain))
        {
            printf("Could not decompress message data.\n");
            return 1;
        };
    } else
    {
        // -- plaintext
        memcpy(&msg.vchMessage[0], pMsgData, lenPlain);
    };

    msg.vchMessage[lenPlain] = '\0';

    if (fFromAnonymous)
    {
        // -- Anonymous sender
        msg.sFromAddress = "anon";
    } else
    {
        std::vector<unsigned char> vchUint160;
        vchUint160.resize(20);

        memcpy(&vchUint160[0], &vchPayload[1], 20);

        uint160 ui160(vchUint160);
        CKeyID ckidFrom(ui160);

        CBitcoinAddress coinAddrFrom;
        coinAddrFrom.Set(ckidFrom);
        if (!coinAddrFrom.IsValid())
        {
            printf("From Addess is invalid.\n");
            return 1;
        };

        std::vector<unsigned char> vchSig;
        vchSig.resize(65);

        memcpy(&vchSig[0], &vchPayload[1+20], 65);

        CKey keyFrom;
        keyFrom.SetCompactSignature(Hash(msg.vchMessage.begin(), msg.vchMessage.end()-1), vchSig);
        CPubKey cpkFromSig = keyFrom.GetPubKey();
        if (!cpkFromSig.IsValid())
        {
            printf("Signature validation failed.\n");
            return 1;
        };

        // -- get address for the compressed public key
        CBitcoinAddress coinAddrFromSig;
        coinAddrFromSig.Set(cpkFromSig.GetID());

        if (!(coinAddrFrom == coinAddrFromSig))
        {
            printf("Signature validation failed.\n");
            return 1;
        };

        cpkFromSig = keyFrom.GetPubKey();

        int rv = 5;
        try {
            rv = SecureMsgInsertAddress(ckidFrom, cpkFromSig);
        } catch (std::exception& e) {
            printf("SecureMsgInsertAddress(), exception: %s.\n", e.what());
            //return 1;
        };

        switch(rv)
        {
            case 0:
                printf("Sender public key added to db.\n");
                break;
            case 4:
                printf("Sender public key already in db.\n");
                break;
            default:
                printf("Error adding sender public key to db.\n");
                break;
        };

        msg.sFromAddress = coinAddrFrom.ToString();
    };

    if (fDebugSmsg)
        printf("Decrypted message for %s.\n", address.c_str());

    return 0;
};

int SecureMsgDecrypt(bool fTestOnly, std::string& address, SecureMessage& smsg, MessageData& msg)
{
    return SecureMsgDecrypt(fTestOnly, address, &smsg.hash[0], smsg.pPayload, smsg.nPayload, msg);
}
