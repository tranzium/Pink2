// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KEY_H
#define BITCOIN_KEY_H

#include <stdexcept>
#include <vector>

#include "allocators.h"
#include "serialize.h"
#include "uint256.h"
#include "util.h"

class key_error : public std::runtime_error
{
public:
    explicit key_error(const std::string& str) : std::runtime_error(str) {}
};

/** A reference to a CKey: the Hash160 of its serialized public key */
class CKeyID : public uint160
{
public:
    CKeyID() : uint160(0) { }
    CKeyID(const uint160 &in) : uint160(in) { }
};

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160(0) { }
    CScriptID(const uint160 &in) : uint160(in) { }
};

/** An encapsulated public key. */
class CPubKey {
private:
    std::vector<unsigned char> vchPubKey;
    friend class CKey;

public:
    CPubKey() { }
    CPubKey(const std::vector<unsigned char> &vchPubKeyIn) : vchPubKey(vchPubKeyIn) { }
    friend bool operator==(const CPubKey &a, const CPubKey &b) { return a.vchPubKey == b.vchPubKey; }
    friend bool operator!=(const CPubKey &a, const CPubKey &b) { return a.vchPubKey != b.vchPubKey; }
    friend bool operator<(const CPubKey &a, const CPubKey &b) { return a.vchPubKey < b.vchPubKey; }

    IMPLEMENT_SERIALIZE(
        READWRITE(vchPubKey);
    )

    CKeyID GetID() const {
        return CKeyID(Hash160(vchPubKey));
    }

    uint256 GetHash() const {
        return Hash(vchPubKey.begin(), vchPubKey.end());
    }

    bool IsValid() const {
        return vchPubKey.size() == 33 || vchPubKey.size() == 65;
    }

    bool IsCompressed() const {
        return vchPubKey.size() == 33;
    }

    std::vector<unsigned char> Raw() const {
        return vchPubKey;
    }
};


// secure_allocator is defined in allocators.h
// CPrivKey is a serialized private key, with all parameters included
// (SEC 1 DER format: 86 bytes compressed, 118 bytes uncompressed)
using CPrivKey = std::vector<unsigned char, secure_allocator<unsigned char> >;
// CSecret is a serialization of just the secret parameter (32 bytes)
using CSecret = std::vector<unsigned char, secure_allocator<unsigned char> >;

/** An encapsulated secp256k1 elliptic curve key (public and/or private) */
class CKey
{
protected:
    unsigned char vch[32]; // raw 32-byte private key
    bool fSet;
    bool fCompressedPubKey;
    bool fPubKeyOnly;      // true when only public key is known (no private key)
    CPubKey pubKeyCache;   // cached public key for pub-key-only mode

public:
    // Raw private key access for ECDH operations (stealth, smessage)
    const unsigned char* begin() const { return vch; }
    const unsigned char* end() const { return vch + 32; }

    void SetCompressedPubKey();
    void SetUnCompressedPubKey();

    void Reset();

    CKey();
    CKey(const CKey& b);

    CKey& operator=(const CKey& b);

    ~CKey();

    bool IsNull() const;
    bool IsCompressed() const;

    void MakeNewKey(bool fCompressed);
    bool SetPrivKey(const CPrivKey& vchPrivKey);
    bool SetSecret(const CSecret& vchSecret, bool fCompressed = false);
    CSecret GetSecret(bool &fCompressed) const;
    CPrivKey GetPrivKey() const;
    bool SetPubKey(const CPubKey& vchPubKey);
    CPubKey GetPubKey() const;

    bool Sign(uint256 hash, std::vector<unsigned char>& vchSig);

    // create a compact signature (65 bytes), which allows reconstructing the used public key
    // The format is one header byte, followed by two times 32 bytes for the serialized r and s values.
    // The header byte: 0x1B = first key with even y, 0x1C = first key with odd y,
    //                  0x1D = second key with even y, 0x1E = second key with odd y
    bool SignCompact(uint256 hash, std::vector<unsigned char>& vchSig);

    // reconstruct public key from a compact signature
    // This is only slightly more CPU intensive than just verifying it.
    // If this function succeeds, the recovered public key is guaranteed to be valid
    // (the signature is a valid signature of the given data for that key)
    bool SetCompactSignature(uint256 hash, const std::vector<unsigned char>& vchSig);

    bool Verify(uint256 hash, const std::vector<unsigned char>& vchSig);

    // Verify a compact signature
    bool VerifyCompact(uint256 hash, const std::vector<unsigned char>& vchSig);

    bool IsValid();

    // Check whether an element of a signature (r or s) is valid.
    static bool CheckSignatureElement(const unsigned char *vch, int len, bool half);
};

/** Initialize/shutdown libsecp256k1 context */
void ECC_Start();
void ECC_Stop();

/** Check that required EC support is available at runtime */
bool ECC_InitSanityCheck(void);

#endif
