// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ARITH_UINT256_H
#define BITCOIN_ARITH_UINT256_H

#include "uint256.h"

#include <cstdint>
#include <cstring>
#include <string>

/// 256-bit unsigned integer for arithmetic operations (difficulty targets).
/// Stores value as 8 little-endian 32-bit limbs.
class arith_uint256
{
    static constexpr int WIDTH = 8;
    uint32_t pn[WIDTH];

public:
    arith_uint256()
    {
        for (int i = 0; i < WIDTH; i++)
            pn[i] = 0;
    }

    arith_uint256(uint64_t b)
    {
        pn[0] = static_cast<uint32_t>(b);
        pn[1] = static_cast<uint32_t>(b >> 32);
        for (int i = 2; i < WIDTH; i++)
            pn[i] = 0;
    }

    arith_uint256(const arith_uint256&) = default;
    arith_uint256& operator=(const arith_uint256&) = default;

    // --- Comparisons ---

    int CompareTo(const arith_uint256& b) const
    {
        for (int i = WIDTH - 1; i >= 0; i--) {
            if (pn[i] < b.pn[i])
                return -1;
            if (pn[i] > b.pn[i])
                return 1;
        }
        return 0;
    }

    friend bool operator<(const arith_uint256& a, const arith_uint256& b) { return a.CompareTo(b) < 0; }
    friend bool operator<=(const arith_uint256& a, const arith_uint256& b) { return a.CompareTo(b) <= 0; }
    friend bool operator>(const arith_uint256& a, const arith_uint256& b) { return a.CompareTo(b) > 0; }
    friend bool operator>=(const arith_uint256& a, const arith_uint256& b) { return a.CompareTo(b) >= 0; }
    friend bool operator==(const arith_uint256& a, const arith_uint256& b) { return a.CompareTo(b) == 0; }
    friend bool operator!=(const arith_uint256& a, const arith_uint256& b) { return a.CompareTo(b) != 0; }

    bool IsZero() const
    {
        for (int i = 0; i < WIDTH; i++)
            if (pn[i] != 0)
                return false;
        return true;
    }

    // --- Arithmetic ---

    arith_uint256& operator+=(const arith_uint256& b)
    {
        uint64_t carry = 0;
        for (int i = 0; i < WIDTH; i++) {
            uint64_t n = carry + pn[i] + b.pn[i];
            pn[i] = static_cast<uint32_t>(n & 0xffffffffu);
            carry = n >> 32;
        }
        return *this;
    }

    arith_uint256& operator-=(const arith_uint256& b)
    {
        *this += (-b);
        return *this;
    }

    arith_uint256& operator*=(uint32_t b32);
    arith_uint256& operator*=(const arith_uint256& b);
    arith_uint256& operator/=(const arith_uint256& b);

    arith_uint256& operator%=(const arith_uint256& b)
    {
        *this -= (*this / b) * b;
        return *this;
    }

    friend arith_uint256 operator+(const arith_uint256& a, const arith_uint256& b) { arith_uint256 r(a); r += b; return r; }
    friend arith_uint256 operator-(const arith_uint256& a, const arith_uint256& b) { arith_uint256 r(a); r -= b; return r; }
    friend arith_uint256 operator*(const arith_uint256& a, const arith_uint256& b) { arith_uint256 r(a); r *= b; return r; }
    friend arith_uint256 operator/(const arith_uint256& a, const arith_uint256& b) { arith_uint256 r(a); r /= b; return r; }
    friend arith_uint256 operator%(const arith_uint256& a, const arith_uint256& b) { arith_uint256 r(a); r %= b; return r; }

    const arith_uint256 operator~() const
    {
        arith_uint256 ret;
        for (int i = 0; i < WIDTH; i++)
            ret.pn[i] = ~pn[i];
        return ret;
    }

    const arith_uint256 operator-() const
    {
        arith_uint256 ret = ~(*this);
        ++ret;
        return ret;
    }

    arith_uint256& operator++()
    {
        int i = 0;
        while (i < WIDTH && ++pn[i] == 0)
            i++;
        return *this;
    }

    // --- Shifts ---

    arith_uint256& operator<<=(unsigned int shift)
    {
        arith_uint256 a(*this);
        for (int i = 0; i < WIDTH; i++)
            pn[i] = 0;
        int k = shift / 32;
        shift = shift % 32;
        for (int i = 0; i < WIDTH; i++) {
            if (i + k + 1 < WIDTH && shift != 0)
                pn[i + k + 1] |= (a.pn[i] >> (32 - shift));
            if (i + k < WIDTH)
                pn[i + k] |= (a.pn[i] << shift);
        }
        return *this;
    }

    arith_uint256& operator>>=(unsigned int shift)
    {
        arith_uint256 a(*this);
        for (int i = 0; i < WIDTH; i++)
            pn[i] = 0;
        int k = shift / 32;
        shift = shift % 32;
        for (int i = 0; i < WIDTH; i++) {
            if (i - k - 1 >= 0 && shift != 0)
                pn[i - k - 1] |= (a.pn[i] << (32 - shift));
            if (i - k >= 0)
                pn[i - k] |= (a.pn[i] >> shift);
        }
        return *this;
    }

    friend arith_uint256 operator<<(const arith_uint256& a, unsigned int shift) { arith_uint256 r(a); r <<= shift; return r; }
    friend arith_uint256 operator>>(const arith_uint256& a, unsigned int shift) { arith_uint256 r(a); r >>= shift; return r; }

    // --- Compact encoding (nBits difficulty target format) ---

    /// Decode compact target encoding. Returns *this for chaining.
    arith_uint256& SetCompact(uint32_t nCompact, bool* pfNegative = nullptr, bool* pfOverflow = nullptr);

    /// Encode to compact target encoding.
    uint32_t GetCompact(bool fNegative = false) const;

    // --- Low-level access ---

    uint64_t GetLow64() const
    {
        return pn[0] | static_cast<uint64_t>(pn[1]) << 32;
    }

    unsigned int bits() const;

    std::string GetHex() const;
    std::string ToString() const;

    // Bridge functions: convert between uint256 (raw hash type) and arith_uint256 (arithmetic type).
    // Both store 256 bits as uint32_t[8] little-endian limbs with the same in-memory layout.
    friend arith_uint256 UintToArith256(const uint256& a);
    friend uint256 ArithToUint256(const arith_uint256& a);
};

arith_uint256 UintToArith256(const uint256& a);
uint256 ArithToUint256(const arith_uint256& a);

#endif // BITCOIN_ARITH_UINT256_H
