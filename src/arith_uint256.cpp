// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "arith_uint256.h"

#include <cstring>

// --- Multiplication ---

arith_uint256& arith_uint256::operator*=(uint32_t b32)
{
    uint64_t carry = 0;
    for (int i = 0; i < WIDTH; i++) {
        uint64_t n = carry + static_cast<uint64_t>(pn[i]) * b32;
        pn[i] = static_cast<uint32_t>(n & 0xffffffffu);
        carry = n >> 32;
    }
    return *this;
}

arith_uint256& arith_uint256::operator*=(const arith_uint256& b)
{
    arith_uint256 a;
    for (int j = 0; j < WIDTH; j++) {
        uint64_t carry = 0;
        for (int i = 0; i + j < WIDTH; i++) {
            uint64_t n = carry + a.pn[i + j] + static_cast<uint64_t>(pn[j]) * b.pn[i];
            a.pn[i + j] = static_cast<uint32_t>(n & 0xffffffffu);
            carry = n >> 32;
        }
    }
    *this = a;
    return *this;
}

// --- Division ---

arith_uint256& arith_uint256::operator/=(const arith_uint256& b)
{
    arith_uint256 div = b;     // make a copy, so we can shift.
    arith_uint256 num = *this; // make a copy, so we can subtract.
    *this = arith_uint256(0);  // the quotient.
    int num_bits = num.bits();
    int div_bits = div.bits();
    if (div_bits == 0)
        return *this; // division by zero — return 0
    if (div_bits > num_bits)
        return *this; // the result is certainly 0.
    int shift = num_bits - div_bits;
    div <<= shift; // shift so that div and num align.
    while (shift >= 0) {
        if (num >= div) {
            num -= div;
            pn[shift / 32] |= (1u << (shift & 31)); // set a bit of the result.
        }
        div >>= 1; // shift back.
        shift--;
    }
    // num now contains the remainder of the division.
    return *this;
}

// --- Compact encoding ---

arith_uint256& arith_uint256::SetCompact(uint32_t nCompact, bool* pfNegative, bool* pfOverflow)
{
    int nSize = nCompact >> 24;
    uint32_t nWord = nCompact & 0x007fffff;
    if (nSize <= 3) {
        nWord >>= 8 * (3 - nSize);
        *this = arith_uint256(nWord);
    } else {
        *this = arith_uint256(nWord);
        *this <<= 8 * (nSize - 3);
    }
    if (pfNegative)
        *pfNegative = (nWord != 0 && (nCompact & 0x00800000) != 0);
    if (pfOverflow)
        *pfOverflow = (nWord != 0 && ((nSize > 34) ||
                       (nWord > 0xff && nSize > 33) ||
                       (nWord > 0xffff && nSize > 32)));
    return *this;
}

uint32_t arith_uint256::GetCompact(bool fNegative) const
{
    int nSize = (bits() + 7) / 8;
    uint32_t nCompact = 0;
    if (nSize <= 3) {
        nCompact = GetLow64() << 8 * (3 - nSize);
    } else {
        arith_uint256 bn = *this >> 8 * (nSize - 3);
        nCompact = static_cast<uint32_t>(bn.GetLow64());
    }
    // The 0x00800000 bit denotes the sign.
    // Thus, if it is already set, divide the mantissa by 256 and increase the exponent.
    if (nCompact & 0x00800000) {
        nCompact >>= 8;
        nSize++;
    }
    nCompact |= static_cast<uint32_t>(nSize) << 24;
    nCompact |= (fNegative && (nCompact & 0x007fffff) ? 0x00800000 : 0);
    return nCompact;
}

// --- Utility ---

unsigned int arith_uint256::bits() const
{
    for (int pos = WIDTH - 1; pos >= 0; pos--) {
        if (pn[pos]) {
            for (int nbits = 31; nbits > 0; nbits--) {
                if (pn[pos] & (1u << nbits))
                    return 32 * pos + nbits + 1;
            }
            return 32 * pos + 1;
        }
    }
    return 0;
}

std::string arith_uint256::GetHex() const
{
    return ArithToUint256(*this).GetHex();
}

std::string arith_uint256::ToString() const
{
    return GetHex();
}

// --- Bridge functions ---

arith_uint256 UintToArith256(const uint256& a)
{
    arith_uint256 b;
    static_assert(sizeof(b.pn) == 32, "arith_uint256 must be 32 bytes");
    std::memcpy(b.pn, &a, 32);
    return b;
}

uint256 ArithToUint256(const arith_uint256& a)
{
    uint256 b;
    static_assert(sizeof(a.pn) == 32, "arith_uint256 must be 32 bytes");
    std::memcpy(&b, a.pn, 32);
    return b;
}
