// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPTNUM_H
#define BITCOIN_SCRIPTNUM_H

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

class scriptnum_error : public std::runtime_error
{
public:
    explicit scriptnum_error(const std::string& str) : std::runtime_error(str) {}
};

/// Numeric opcodes (OP_1ADD, etc) are restricted to operating on 4-byte integers.
/// The semantics are subtle, though: operands must be in the range [-2^31+1...2^31-1],
/// but results may overflow (and are valid as long as they are not used in a subsequent
/// numeric operation). CScriptNum enforces those semantics by storing results as
/// an int64, and IsOversizedNumericValue() checks for oversize serialization.
class CScriptNum
{
public:
    explicit CScriptNum(int64_t n) : m_value(n) {}

    explicit CScriptNum(const std::vector<unsigned char>& vch, size_t nMaxNumSize)
    {
        if (vch.size() > nMaxNumSize)
            throw scriptnum_error("script number overflow");
        m_value = set_vch(vch);
    }

    // --- Comparison operators ---

    bool operator==(int64_t rhs) const { return m_value == rhs; }
    bool operator!=(int64_t rhs) const { return m_value != rhs; }
    bool operator<=(int64_t rhs) const { return m_value <= rhs; }
    bool operator< (int64_t rhs) const { return m_value <  rhs; }
    bool operator>=(int64_t rhs) const { return m_value >= rhs; }
    bool operator> (int64_t rhs) const { return m_value >  rhs; }

    bool operator==(const CScriptNum& rhs) const { return m_value == rhs.m_value; }
    bool operator!=(const CScriptNum& rhs) const { return m_value != rhs.m_value; }
    bool operator<=(const CScriptNum& rhs) const { return m_value <= rhs.m_value; }
    bool operator< (const CScriptNum& rhs) const { return m_value <  rhs.m_value; }
    bool operator>=(const CScriptNum& rhs) const { return m_value >= rhs.m_value; }
    bool operator> (const CScriptNum& rhs) const { return m_value >  rhs.m_value; }

    // --- Arithmetic operators ---

    CScriptNum operator+(int64_t rhs) const { return CScriptNum(m_value + rhs); }
    CScriptNum operator-(int64_t rhs) const { return CScriptNum(m_value - rhs); }

    CScriptNum operator+(const CScriptNum& rhs) const { return CScriptNum(m_value + rhs.m_value); }
    CScriptNum operator-(const CScriptNum& rhs) const { return CScriptNum(m_value - rhs.m_value); }

    CScriptNum operator-() const { return CScriptNum(-m_value); }

    CScriptNum& operator+=(const CScriptNum& rhs) { m_value += rhs.m_value; return *this; }
    CScriptNum& operator-=(const CScriptNum& rhs) { m_value -= rhs.m_value; return *this; }

    CScriptNum& operator&=(int64_t rhs) { m_value &= rhs; return *this; }

    // --- Conversion ---

    int getint() const
    {
        if (m_value > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        else if (m_value < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return static_cast<int>(m_value);
    }

    int64_t getint64() const { return m_value; }

    std::vector<unsigned char> getvch() const
    {
        return serialize(m_value);
    }

    // --- Serialization (little-endian sign-magnitude) ---

    static std::vector<unsigned char> serialize(int64_t value)
    {
        if (value == 0)
            return {};

        std::vector<unsigned char> result;
        const bool neg = value < 0;
        uint64_t absvalue = neg ? -static_cast<uint64_t>(value) : static_cast<uint64_t>(value);

        while (absvalue) {
            result.push_back(static_cast<unsigned char>(absvalue & 0xff));
            absvalue >>= 8;
        }

        // If the most significant byte has its high bit set,
        // push an extra byte to carry the sign.
        if (result.back() & 0x80)
            result.push_back(neg ? 0x80 : 0x00);
        else if (neg)
            result.back() |= 0x80;

        return result;
    }

private:
    static int64_t set_vch(const std::vector<unsigned char>& vch)
    {
        if (vch.empty())
            return 0;

        int64_t result = 0;
        for (size_t i = 0; i != vch.size(); ++i)
            result |= static_cast<int64_t>(vch[i]) << (8 * i);

        // If the input vector's most significant byte is 0x80, remove it
        // from the result's computation and set the sign.
        if (vch.back() & 0x80)
            return -static_cast<int64_t>(static_cast<uint64_t>(result) & ~(static_cast<uint64_t>(0x80) << (8 * (vch.size() - 1))));

        return result;
    }

    int64_t m_value;
};

#endif // BITCOIN_SCRIPTNUM_H
