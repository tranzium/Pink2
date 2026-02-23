// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOINALERT_H_
#define _BITCOINALERT_H_ 1

#include <set>
#include <string>

#include "uint256.h"
#include "util.h"

class CNode;

/** Alerts are for notifying old versions if they become too obsolete and
 * need to upgrade.  The message is displayed in the status bar.
 * Alert messages are broadcast as a vector of signed data.  Unserializing may
 * not read the entire buffer if the alert is for a newer version, but older
 * versions can still relay the original data.
 */
class CUnsignedAlert
{
public:
    int nVersion;
    int64_t nRelayUntil;      // when newer nodes stop relaying to newer nodes
    int64_t nExpiration;
    int nID;
    int nCancel;
    std::set<int> setCancel;
    int nMinVer;            // lowest version inclusive
    int nMaxVer;            // highest version inclusive
    std::set<std::string> setSubVer;  // empty matches all
    int nPriority;

    // Actions
    std::string strComment;
    std::string strStatusBar;
    std::string strReserved;

    unsigned int GetSerializeSize(int nType, int nVersion) const
    {
        unsigned int nSerSize = 0;
        nSerSize += ::GetSerializeSize(this->nVersion, nType, nVersion);
        nSerSize += ::GetSerializeSize(nRelayUntil, nType, nVersion);
        nSerSize += ::GetSerializeSize(nExpiration, nType, nVersion);
        nSerSize += ::GetSerializeSize(nID, nType, nVersion);
        nSerSize += ::GetSerializeSize(nCancel, nType, nVersion);
        nSerSize += ::GetSerializeSize(setCancel, nType, nVersion);
        nSerSize += ::GetSerializeSize(nMinVer, nType, nVersion);
        nSerSize += ::GetSerializeSize(nMaxVer, nType, nVersion);
        nSerSize += ::GetSerializeSize(setSubVer, nType, nVersion);
        nSerSize += ::GetSerializeSize(nPriority, nType, nVersion);
        nSerSize += ::GetSerializeSize(strComment, nType, nVersion);
        nSerSize += ::GetSerializeSize(strStatusBar, nType, nVersion);
        nSerSize += ::GetSerializeSize(strReserved, nType, nVersion);
        return nSerSize;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const
    {
        ::Serialize(s, this->nVersion, nType, nVersion);
        ::Serialize(s, nRelayUntil, nType, nVersion);
        ::Serialize(s, nExpiration, nType, nVersion);
        ::Serialize(s, nID, nType, nVersion);
        ::Serialize(s, nCancel, nType, nVersion);
        ::Serialize(s, setCancel, nType, nVersion);
        ::Serialize(s, nMinVer, nType, nVersion);
        ::Serialize(s, nMaxVer, nType, nVersion);
        ::Serialize(s, setSubVer, nType, nVersion);
        ::Serialize(s, nPriority, nType, nVersion);
        ::Serialize(s, strComment, nType, nVersion);
        ::Serialize(s, strStatusBar, nType, nVersion);
        ::Serialize(s, strReserved, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion)
    {
        ::Unserialize(s, this->nVersion, nType, nVersion);
        nVersion = this->nVersion;
        ::Unserialize(s, nRelayUntil, nType, nVersion);
        ::Unserialize(s, nExpiration, nType, nVersion);
        ::Unserialize(s, nID, nType, nVersion);
        ::Unserialize(s, nCancel, nType, nVersion);
        ::Unserialize(s, setCancel, nType, nVersion);
        ::Unserialize(s, nMinVer, nType, nVersion);
        ::Unserialize(s, nMaxVer, nType, nVersion);
        ::Unserialize(s, setSubVer, nType, nVersion);
        ::Unserialize(s, nPriority, nType, nVersion);
        ::Unserialize(s, strComment, nType, nVersion);
        ::Unserialize(s, strStatusBar, nType, nVersion);
        ::Unserialize(s, strReserved, nType, nVersion);
    }

    void SetNull();

    std::string ToString() const;
    void print() const;
};

/** An alert is a combination of a serialized CUnsignedAlert and a signature. */
class CAlert : public CUnsignedAlert
{
public:
    std::vector<unsigned char> vchMsg;
    std::vector<unsigned char> vchSig;

    CAlert()
    {
        SetNull();
    }

    unsigned int GetSerializeSize(int nType, int nVersion) const
    {
        unsigned int nSerSize = 0;
        nSerSize += ::GetSerializeSize(vchMsg, nType, nVersion);
        nSerSize += ::GetSerializeSize(vchSig, nType, nVersion);
        return nSerSize;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const
    {
        ::Serialize(s, vchMsg, nType, nVersion);
        ::Serialize(s, vchSig, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion)
    {
        ::Unserialize(s, vchMsg, nType, nVersion);
        ::Unserialize(s, vchSig, nType, nVersion);
    }

    void SetNull();
    bool IsNull() const;
    uint256 GetHash() const;
    bool IsInEffect() const;
    bool Cancels(const CAlert& alert) const;
    bool AppliesTo(int nVersion, std::string strSubVerIn) const;
    bool AppliesToMe() const;
    bool RelayTo(CNode* pnode) const;
    bool CheckSignature() const;
    bool ProcessAlert(bool fThread = true);

    /*
     * Get copy of (active) alert object by hash. Returns a null alert if it is not found.
     */
    static CAlert getAlertByHash(const uint256 &hash);
};

#endif
