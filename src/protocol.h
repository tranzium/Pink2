// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef __cplusplus
# error This header can only be compiled as C++.
#endif

#ifndef __INCLUDED_PROTOCOL_H__
#define __INCLUDED_PROTOCOL_H__

#include "serialize.h"
#include "netbase.h"
#include <string>
#include "uint256.h"

extern bool fTestNet;
static inline unsigned short GetDefaultPort(const bool testnet = fTestNet)
{
    return testnet ? 19134 : 9134;
}


extern unsigned char pchMessageStart[4];

/** Message header.
 * (4) message start.
 * (12) command.
 * (4) size.
 * (4) checksum.
 */
class CMessageHeader
{
    public:
        CMessageHeader();
        CMessageHeader(const char* pszCommand, unsigned int nMessageSizeIn);

        std::string GetCommand() const;
        bool IsValid() const;

        unsigned int GetSerializeSize(int nType, int nVersion) const
        {
            unsigned int nSerSize = 0;
            nSerSize += sizeof(pchMessageStart);
            nSerSize += sizeof(pchCommand);
            nSerSize += ::GetSerializeSize(nMessageSize, nType, nVersion);
            nSerSize += ::GetSerializeSize(nChecksum, nType, nVersion);
            return nSerSize;
        }
        template<typename Stream>
        void Serialize(Stream& s, int nType, int nVersion) const
        {
            s.write(pchMessageStart, sizeof(pchMessageStart));
            s.write(pchCommand, sizeof(pchCommand));
            ::Serialize(s, nMessageSize, nType, nVersion);
            ::Serialize(s, nChecksum, nType, nVersion);
        }
        template<typename Stream>
        void Unserialize(Stream& s, int nType, int nVersion)
        {
            s.read(pchMessageStart, sizeof(pchMessageStart));
            s.read(pchCommand, sizeof(pchCommand));
            ::Unserialize(s, nMessageSize, nType, nVersion);
            ::Unserialize(s, nChecksum, nType, nVersion);
        }

    // TODO: make private (improves encapsulation)
    public:
        enum {
            MESSAGE_START_SIZE=sizeof(::pchMessageStart),
            COMMAND_SIZE=12,
            MESSAGE_SIZE_SIZE=sizeof(int),
            CHECKSUM_SIZE=sizeof(int),

            MESSAGE_SIZE_OFFSET=MESSAGE_START_SIZE+COMMAND_SIZE,
            CHECKSUM_OFFSET=MESSAGE_SIZE_OFFSET+MESSAGE_SIZE_SIZE,
            HEADER_SIZE=MESSAGE_START_SIZE+COMMAND_SIZE+MESSAGE_SIZE_SIZE+CHECKSUM_SIZE
        };
        char pchMessageStart[MESSAGE_START_SIZE];
        char pchCommand[COMMAND_SIZE];
        unsigned int nMessageSize;
        unsigned int nChecksum;
};

/** nServices flags */
enum
{
    NODE_NETWORK = (1 << 0),
};

/** A CService with information about it as peer */
class CAddress : public CService
{
    public:
        CAddress();
        explicit CAddress(CService ipIn, uint64_t nServicesIn=NODE_NETWORK);

        void Init();

        unsigned int GetSerializeSize(int nType, int nVersion) const
        {
            unsigned int nSerSize = 0;
            if (nType & SER_DISK)
                nSerSize += ::GetSerializeSize(nVersion, nType, nVersion);
            if ((nType & SER_DISK) ||
                (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
                nSerSize += ::GetSerializeSize(nTime, nType, nVersion);
            nSerSize += ::GetSerializeSize(nServices, nType, nVersion);
            nSerSize += CService::GetSerializeSize(nType, nVersion);
            return nSerSize;
        }
        template<typename Stream>
        void Serialize(Stream& s, int nType, int nVersion) const
        {
            if (nType & SER_DISK)
                ::Serialize(s, nVersion, nType, nVersion);
            if ((nType & SER_DISK) ||
                (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
                ::Serialize(s, nTime, nType, nVersion);
            ::Serialize(s, nServices, nType, nVersion);
            CService::Serialize(s, nType, nVersion);
        }
        template<typename Stream>
        void Unserialize(Stream& s, int nType, int nVersion)
        {
            Init();
            if (nType & SER_DISK) {
                ::Unserialize(s, nVersion, nType, nVersion);
            }
            if ((nType & SER_DISK) ||
                (nVersion >= CADDR_TIME_VERSION && !(nType & SER_GETHASH)))
                ::Unserialize(s, nTime, nType, nVersion);
            ::Unserialize(s, nServices, nType, nVersion);
            CService::Unserialize(s, nType, nVersion);
        }

        void print() const;

    // TODO: make private (improves encapsulation)
    public:
        uint64_t nServices;

        // disk and network only
        unsigned int nTime;

        // memory only
        int64_t nLastTry;
};

/** inv message data */
class CInv
{
    public:
        CInv();
        CInv(int typeIn, const uint256& hashIn);
        CInv(const std::string& strType, const uint256& hashIn);

        unsigned int GetSerializeSize(int nType, int nVersion) const
        {
            return ::GetSerializeSize(type, nType, nVersion) +
                   ::GetSerializeSize(hash, nType, nVersion);
        }
        template<typename Stream>
        void Serialize(Stream& s, int nType, int nVersion) const
        {
            ::Serialize(s, type, nType, nVersion);
            ::Serialize(s, hash, nType, nVersion);
        }
        template<typename Stream>
        void Unserialize(Stream& s, int nType, int nVersion)
        {
            ::Unserialize(s, type, nType, nVersion);
            ::Unserialize(s, hash, nType, nVersion);
        }

        friend bool operator<(const CInv& a, const CInv& b);

        bool IsKnownType() const;
        const char* GetCommand() const;
        std::string ToString() const;
        void print() const;

    // TODO: make private (improves encapsulation)
    public:
        int type;
        uint256 hash;
};

#endif // __INCLUDED_PROTOCOL_H__
