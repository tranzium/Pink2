// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string.h>
#include <cassert>
#include <algorithm>

#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "key.h"

// ---------------------------------------------------------------------------
// Global secp256k1 context (initialized by ECC_Start, destroyed by ECC_Stop)
// ---------------------------------------------------------------------------
static secp256k1_context* secp256k1_ctx = nullptr;

void ECC_Start()
{
    assert(secp256k1_ctx == nullptr);
    secp256k1_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    assert(secp256k1_ctx != nullptr);

    // Randomize for side-channel protection
    unsigned char seed[32];
    RAND_bytes(seed, sizeof(seed));
    int ret = secp256k1_context_randomize(secp256k1_ctx, seed);
    assert(ret);
    OPENSSL_cleanse(seed, sizeof(seed));
}

void ECC_Stop()
{
    if (secp256k1_ctx) {
        secp256k1_context_destroy(secp256k1_ctx);
        secp256k1_ctx = nullptr;
    }
}

// Lazily ensures the context exists (for backward compatibility with code
// that creates CKey objects before calling ECC_Start explicitly).
// Non-static: also used by stealth.cpp and smessage.cpp for EC operations.
secp256k1_context* GetContext()
{
    if (!secp256k1_ctx)
        ECC_Start();
    return secp256k1_ctx;
}

// ---------------------------------------------------------------------------
// SEC 1 DER encoder/decoder for wallet.dat compatibility
// ---------------------------------------------------------------------------

// OID for secp256k1: 1.3.132.0.10
static const unsigned char secp256k1_oid[] = { 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x0A };

// Encode private key in SEC 1 ECPrivateKey DER format (compatible with i2d_ECPrivateKey)
static CPrivKey EncodePrivKey(const unsigned char secret[32], const std::vector<unsigned char>& pubkey)
{
    size_t publen = pubkey.size(); // 33 or 65
    size_t bitstrlen = 1 + publen; // 00 (no unused bits) + pubkey
    size_t ctx1len = 2 + bitstrlen; // BIT STRING TLV: tag(1) + len(1) + contents
    size_t a1len = 2 + ctx1len; // [1] EXPLICIT wrapper

    size_t innerlen = 3                   // INTEGER 1: 02 01 01
                    + 2 + 32              // OCTET STRING: 04 20 <32>
                    + 2 + sizeof(secp256k1_oid)  // [0]: A0 07 <oid>
                    + a1len;              // [1]: A1 <len> ...

    CPrivKey result;
    result.reserve(2 + innerlen);

    // SEQUENCE header
    result.push_back(0x30);
    result.push_back(static_cast<unsigned char>(innerlen));

    // version INTEGER 1
    result.push_back(0x02);
    result.push_back(0x01);
    result.push_back(0x01);

    // privateKey OCTET STRING (32 bytes)
    result.push_back(0x04);
    result.push_back(0x20);
    result.insert(result.end(), secret, secret + 32);

    // parameters [0] EXPLICIT { OID secp256k1 }
    result.push_back(0xA0);
    result.push_back(static_cast<unsigned char>(sizeof(secp256k1_oid)));
    result.insert(result.end(), secp256k1_oid, secp256k1_oid + sizeof(secp256k1_oid));

    // publicKey [1] EXPLICIT { BIT STRING }
    result.push_back(0xA1);
    result.push_back(static_cast<unsigned char>(ctx1len));
    result.push_back(0x03); // BIT STRING tag
    result.push_back(static_cast<unsigned char>(bitstrlen));
    result.push_back(0x00); // no unused bits
    result.insert(result.end(), pubkey.begin(), pubkey.end());

    return result;
}

// Parse a DER length field (handles both short-form and long-form encoding).
// Advances p past the length bytes. Returns false if malformed.
static bool ParseDERLength(const unsigned char*& p, const unsigned char* pend, size_t& len)
{
    if (p >= pend) return false;
    unsigned char first = *p++;
    if (!(first & 0x80)) {
        len = first;  // Short form: length in low 7 bits
        return true;
    }
    unsigned char nbytes = first & 0x7F;
    if (nbytes == 0 || nbytes > 2 || p + nbytes > pend)
        return false;  // Indefinite form or too many length bytes
    len = 0;
    for (unsigned char i = 0; i < nbytes; i++)
        len = (len << 8) | *p++;
    return true;
}

// Decode SEC 1 DER private key, returns true on success.
// Handles all DER variations produced by any version of OpenSSL's i2d_ECPrivateKey:
// - Short and long-form DER length encoding
// - Variable-length private key OCTET STRING (1-32 bytes, zero-padded to 32)
// - Optional/absent parameters [0] and publicKey [1] tags
static bool DecodePrivKey(const CPrivKey& vchPrivKey, unsigned char secret[32], bool& fCompressed)
{
    if (vchPrivKey.size() < 2)
        return false;

    const unsigned char* p = &vchPrivKey[0];
    const unsigned char* pend = p + vchPrivKey.size();

    // SEQUENCE
    if (*p++ != 0x30)
        return false;
    size_t seqlen;
    if (!ParseDERLength(p, pend, seqlen))
        return false;
    if (p + seqlen > pend)
        return false;
    pend = p + seqlen;

    // version INTEGER (must be 1)
    if (p + 3 > pend || p[0] != 0x02 || p[1] != 0x01 || p[2] != 0x01)
        return false;
    p += 3;

    // privateKey OCTET STRING (1-32 bytes; zero-pad to 32)
    if (p + 2 > pend || p[0] != 0x04)
        return false;
    p++;
    size_t keylen;
    if (!ParseDERLength(p, pend, keylen))
        return false;
    if (keylen == 0 || keylen > 32 || p + keylen > pend)
        return false;
    memset(secret, 0, 32);
    memcpy(secret + 32 - keylen, p, keylen);
    p += keylen;

    // Default to compressed; detect from publicKey [1] if present
    fCompressed = true;
    while (p < pend) {
        if (p + 1 > pend) break;
        unsigned char tag = *p++;
        size_t len;
        if (!ParseDERLength(p, pend, len)) break;
        if (p + len > pend) break;

        // [1] EXPLICIT publicKey — detect compressed vs uncompressed
        if (tag == 0xA1 && len >= 3 && p[0] == 0x03) {
            // BIT STRING: tag(03) + length + unused-bits(00) + pubkey
            size_t bslen;
            const unsigned char* bp = p + 1;
            if (!ParseDERLength(bp, p + len, bslen)) { p += len; continue; }
            if (bslen >= 2 && bp < p + len && *bp == 0x00) {
                size_t pubkeylen = bslen - 1;  // subtract unused-bits byte
                fCompressed = (pubkeylen == 33);
            }
        }
        p += len;
    }

    return true;
}

// Fallback decoder using OpenSSL's d2i_ECPrivateKey for maximum compatibility
// with any DER format variant ever produced by any OpenSSL version.
static bool DecodePrivKeyOpenSSL(const CPrivKey& vchPrivKey, unsigned char secret[32], bool& fCompressed)
{
    EC_KEY_ptr eckey(EC_KEY_new_by_curve_name(NID_secp256k1));
    if (!eckey)
        return false;

    const unsigned char* pbegin = &vchPrivKey[0];
    EC_KEY* eckey_raw = eckey.get();
    if (!d2i_ECPrivateKey(&eckey_raw, &pbegin, vchPrivKey.size()))
        return false;

    const BIGNUM* bn = EC_KEY_get0_private_key(eckey.get());
    if (!bn)
        return false;

    // Extract 32-byte secret with zero-padding
    memset(secret, 0, 32);
    int nBytes = BN_num_bytes(bn);
    if (nBytes <= 0 || nBytes > 32)
        return false;
    BN_bn2bin(bn, secret + 32 - nBytes);

    // Detect compression from the EC_KEY's point conversion form
    point_conversion_form_t form = EC_KEY_get_conv_form(eckey.get());
    fCompressed = (form == POINT_CONVERSION_COMPRESSED);

    return true;
}

// ---------------------------------------------------------------------------
// CheckSignatureElement (used by script.cpp for DER signature validation)
// ---------------------------------------------------------------------------

static int CompareBigEndian(const unsigned char *c1, size_t c1len, const unsigned char *c2, size_t c2len) {
    while (c1len > c2len) {
        if (*c1)
            return 1;
        c1++;
        c1len--;
    }
    while (c2len > c1len) {
        if (*c2)
            return -1;
        c2++;
        c2len--;
    }
    while (c1len > 0) {
        if (*c1 > *c2)
            return 1;
        if (*c2 > *c1)
            return -1;
        c1++;
        c2++;
        c1len--;
    }
    return 0;
}

// Order of secp256k1's generator minus 1.
const unsigned char vchMaxModOrder[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
    0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x40
};

// Half of the order of secp256k1's generator minus 1.
const unsigned char vchMaxModHalfOrder[32] = {
    0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x5D,0x57,0x6E,0x73,0x57,0xA4,0x50,0x1D,
    0xDF,0xE9,0x2F,0x46,0x68,0x1B,0x20,0xA0
};

const unsigned char vchZero[0] = {};

bool CKey::CheckSignatureElement(const unsigned char *vch, int len, bool half) {
    return CompareBigEndian(vch, len, vchZero, 0) > 0 &&
           CompareBigEndian(vch, len, half ? vchMaxModHalfOrder : vchMaxModOrder, 32) <= 0;
}

// ---------------------------------------------------------------------------
// CKey implementation
// ---------------------------------------------------------------------------

void CKey::SetCompressedPubKey()
{
    fCompressedPubKey = true;
}

void CKey::SetUnCompressedPubKey()
{
    fCompressedPubKey = false;
}

void CKey::Reset()
{
    OPENSSL_cleanse(vch, 32);
    fCompressedPubKey = false;
    fSet = false;
    fPubKeyOnly = false;
    pubKeyCache = CPubKey();
}

CKey::CKey()
{
    memset(vch, 0, 32);
    fSet = false;
    fCompressedPubKey = false;
    fPubKeyOnly = false;
}

CKey::CKey(const CKey& b)
{
    memcpy(vch, b.vch, 32);
    fSet = b.fSet;
    fCompressedPubKey = b.fCompressedPubKey;
    fPubKeyOnly = b.fPubKeyOnly;
    pubKeyCache = b.pubKeyCache;
}

CKey& CKey::operator=(const CKey& b)
{
    memcpy(vch, b.vch, 32);
    fSet = b.fSet;
    fCompressedPubKey = b.fCompressedPubKey;
    fPubKeyOnly = b.fPubKeyOnly;
    pubKeyCache = b.pubKeyCache;
    return (*this);
}

CKey::~CKey()
{
    OPENSSL_cleanse(vch, 32);
}

bool CKey::IsNull() const
{
    return !fSet;
}

bool CKey::IsCompressed() const
{
    return fCompressedPubKey;
}

void CKey::MakeNewKey(bool fCompressed)
{
    secp256k1_context* ctx = GetContext();
    do {
        RAND_bytes(vch, 32);
    } while (!secp256k1_ec_seckey_verify(ctx, vch));

    fSet = true;
    fPubKeyOnly = false;
    pubKeyCache = CPubKey();
    if (fCompressed)
        SetCompressedPubKey();
}

bool CKey::SetPrivKey(const CPrivKey& vchPrivKey)
{
    unsigned char secret[32];
    bool fCompressed;

    // Try custom DER decoder first; fall back to OpenSSL for any format
    // variations produced by older OpenSSL versions (1.0.x, 1.1.x, etc.)
    if (!DecodePrivKey(vchPrivKey, secret, fCompressed))
    {
        if (!DecodePrivKeyOpenSSL(vchPrivKey, secret, fCompressed))
        {
            printf("SetPrivKey: DER decode failed (size=%u, first bytes:", (unsigned)vchPrivKey.size());
            for (size_t i = 0; i < std::min(vchPrivKey.size(), (size_t)16); i++)
                printf(" %02x", vchPrivKey[i]);
            printf(")\n");
            Reset();
            return false;
        }
    }

    secp256k1_context* ctx = GetContext();
    if (!secp256k1_ec_seckey_verify(ctx, secret))
    {
        printf("SetPrivKey: secp256k1_ec_seckey_verify failed\n");
        OPENSSL_cleanse(secret, 32);
        Reset();
        return false;
    }

    memcpy(vch, secret, 32);
    OPENSSL_cleanse(secret, 32);
    fSet = true;
    fPubKeyOnly = false;
    pubKeyCache = CPubKey();
    fCompressedPubKey = fCompressed;
    return true;
}

bool CKey::SetSecret(const CSecret& vchSecret, bool fCompressed)
{
    if (vchSecret.size() != 32)
        throw key_error("CKey::SetSecret() : secret must be 32 bytes");

    secp256k1_context* ctx = GetContext();
    if (!secp256k1_ec_seckey_verify(ctx, &vchSecret[0]))
        throw key_error("CKey::SetSecret() : invalid secret key");

    memcpy(vch, &vchSecret[0], 32);
    fSet = true;
    fPubKeyOnly = false;
    pubKeyCache = CPubKey();
    if (fCompressed || fCompressedPubKey)
        SetCompressedPubKey();
    return true;
}

CSecret CKey::GetSecret(bool &fCompressed) const
{
    CSecret vchRet;
    vchRet.resize(32);
    memcpy(&vchRet[0], vch, 32);
    fCompressed = fCompressedPubKey;
    return vchRet;
}

CPrivKey CKey::GetPrivKey() const
{
    std::vector<unsigned char> pubkey = GetPubKey().Raw();
    return EncodePrivKey(vch, pubkey);
}

bool CKey::SetPubKey(const CPubKey& vchPubKey)
{
    secp256k1_context* ctx = GetContext();
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, &vchPubKey.vchPubKey[0], vchPubKey.vchPubKey.size()))
    {
        Reset();
        return false;
    }

    memset(vch, 0, 32);
    fSet = true;
    fPubKeyOnly = true;
    pubKeyCache = vchPubKey;
    if (vchPubKey.vchPubKey.size() == 33)
        SetCompressedPubKey();
    else
        SetUnCompressedPubKey();
    return true;
}

CPubKey CKey::GetPubKey() const
{
    if (fPubKeyOnly)
        return pubKeyCache;

    secp256k1_context* ctx = GetContext();
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, vch))
        throw key_error("CKey::GetPubKey() : secp256k1_ec_pubkey_create failed");

    size_t outlen = fCompressedPubKey ? 33 : 65;
    std::vector<unsigned char> vchPubKey(outlen);
    secp256k1_ec_pubkey_serialize(ctx, &vchPubKey[0], &outlen, &pubkey,
        fCompressedPubKey ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED);

    return CPubKey(vchPubKey);
}

bool CKey::Sign(uint256 hash, std::vector<unsigned char>& vchSig)
{
    vchSig.clear();
    secp256k1_context* ctx = GetContext();

    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_sign(ctx, &sig, (unsigned char*)&hash, vch, nullptr, nullptr))
        return false;

    // libsecp256k1 produces low-S signatures by default
    unsigned char der[72];
    size_t derlen = sizeof(der);
    secp256k1_ecdsa_signature_serialize_der(ctx, der, &derlen, &sig);

    vchSig.assign(der, der + derlen);
    return true;
}

bool CKey::SignCompact(uint256 hash, std::vector<unsigned char>& vchSig)
{
    secp256k1_context* ctx = GetContext();

    secp256k1_ecdsa_recoverable_signature rsig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &rsig, (unsigned char*)&hash, vch, nullptr, nullptr))
        return false;

    unsigned char compact[64];
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact, &recid, &rsig);

    vchSig.resize(65);
    vchSig[0] = 27 + recid + (fCompressedPubKey ? 4 : 0);
    memcpy(&vchSig[1], compact, 64);
    return true;
}

bool CKey::SetCompactSignature(uint256 hash, const std::vector<unsigned char>& vchSig)
{
    if (vchSig.size() != 65)
        return false;
    int nV = vchSig[0];
    if (nV < 27 || nV >= 35)
        return false;

    bool fComp = (nV >= 31);
    int recid = (nV - 27) & 3;

    secp256k1_context* ctx = GetContext();

    secp256k1_ecdsa_recoverable_signature rsig;
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &rsig, &vchSig[1], recid))
        return false;

    secp256k1_pubkey pubkey;
    if (!secp256k1_ecdsa_recover(ctx, &pubkey, &rsig, (unsigned char*)&hash))
        return false;

    // Serialize recovered public key
    size_t outlen = fComp ? 33 : 65;
    std::vector<unsigned char> vchPubKey(outlen);
    secp256k1_ec_pubkey_serialize(ctx, &vchPubKey[0], &outlen, &pubkey,
        fComp ? SECP256K1_EC_COMPRESSED : SECP256K1_EC_UNCOMPRESSED);

    // Store as pub-key-only (no private key available)
    memset(vch, 0, 32);
    fSet = true;
    fPubKeyOnly = true;
    fCompressedPubKey = fComp;
    pubKeyCache = CPubKey(vchPubKey);
    return true;
}

bool CKey::Verify(uint256 hash, const std::vector<unsigned char>& vchSig)
{
    if (vchSig.empty())
        return false;

    secp256k1_context* ctx = GetContext();

    // Parse the public key
    CPubKey pub = GetPubKey();
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, &pub.vchPubKey[0], pub.vchPubKey.size()))
        return false;

    // Parse the DER signature (normalizing to low-S)
    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_signature_parse_der(ctx, &sig, &vchSig[0], vchSig.size()))
        return false;

    // Normalize to low-S for verification
    secp256k1_ecdsa_signature_normalize(ctx, &sig, &sig);

    return secp256k1_ecdsa_verify(ctx, &sig, (unsigned char*)&hash, &pubkey) == 1;
}

bool CKey::VerifyCompact(uint256 hash, const std::vector<unsigned char>& vchSig)
{
    CKey key;
    if (!key.SetCompactSignature(hash, vchSig))
        return false;
    if (GetPubKey() != key.GetPubKey())
        return false;

    return true;
}

bool CKey::IsValid()
{
    if (!fSet)
        return false;

    bool fCompr;
    CSecret secret = GetSecret(fCompr);
    CKey key2;
    key2.SetSecret(secret, fCompr);
    return GetPubKey() == key2.GetPubKey();
}

bool ECC_InitSanityCheck() {
    secp256k1_context* ctx = GetContext();
    return ctx != nullptr;
}
