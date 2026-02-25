// Copyright (c) 2014 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Network protocol handlers, thread functions, and bucket hash computation
// for the secure messaging system.

#include "smessage/smessage.h"

#include <stdint.h>
#include <filesystem>

#include "net.h"
#include "util.h"

#include "xxhash/xxhash.h"


// On 64 bit system ld is 64bits
#ifdef IS_ARCH_64
#undef  PRId64
#undef  PRIu64
#undef  PRIx64
#define  PRId64  "ld"
#define  PRIu64  "lu"
#define  PRIx64  "lx"
#endif // IS_ARCH_64


namespace fs = std::filesystem;

uint32_t nPeerIdCounter = 1;


void SecMsgBucket::hashBucket()
{
    if (fDebugSmsg)
        printf("SecMsgBucket::hashBucket()\n");

    timeChanged = GetAdjustedTime();

    void* state = XXH32_init(1);

    for (const auto& token : setTokens)
    {
        XXH32_update(state, token.sample, 8);
    };

    hash = XXH32_digest(state);

    if (fDebugSmsg)
        printf("Hashed %" PRIszu " messages, hash %u\n", setTokens.size(), hash);
};

void ThreadSecureMsg()
{
    // -- bucket management thread
    RenameThread("pinkcoin-smsg"); // Make this thread recognisable

    uint32_t delay = 0;

    while (fSecMsgenabled)
    {
        // shutdown thread waits 5 seconds, this should be less
        MilliSleep(1000); // milliseconds

        if (!fSecMsgenabled) // check again after sleep
            break;

        delay++;
        if (delay < SMSG_THREAD_DELAY) // check every SMSG_THREAD_DELAY seconds
            continue;
        delay = 0;

        int64_t now = GetAdjustedTime();

        if (fDebugSmsg)
            printf("SecureMsgThread %" PRId64 " \n", now);

        int64_t cutoffTime = now - SMSG_RETENTION;

        {
            LOCK(cs_smsg);
            auto it = smsgBuckets.begin();

            while (it != smsgBuckets.end())
            {
                //if (fDebugSmsg)
                //    printf("Checking bucket %d, size %u \n", it->first, it->second.setTokens.size());
                if (it->first < cutoffTime)
                {
                    if (fDebugSmsg)
                        printf("Removing bucket %" PRId64 " \n", it->first);
                    std::string fileName = std::to_string(it->first) + "_01.dat";
                    fs::path fullPath = GetDataDir() / "smsgStore" / fileName;
                    if (fs::exists(fullPath))
                    {
                        try {
                            fs::remove(fullPath);
                        } catch (const fs::filesystem_error& ex)
                        {
                            printf("Error removing bucket file %s.\n", ex.what());
                        };
                    } else
                        printf("Path %s does not exist \n", fullPath.string().c_str());

                    // -- look for a wl file, it stores incoming messages when wallet is locked
                    fileName = std::to_string(it->first) + "_01_wl.dat";
                    fullPath = GetDataDir() / "smsgStore" / fileName;
                    if (fs::exists(fullPath))
                    {
                        try {
                            fs::remove(fullPath);
                        } catch (const fs::filesystem_error& ex)
                        {
                            printf("Error removing wallet locked file %s.\n", ex.what());
                        };
                    };

                    smsgBuckets.erase(it++);
                } else
                {
                    // -- tick down nLockCount, so will eventually expire if peer never sends data
                    if (it->second.nLockCount > 0)
                    {
                        it->second.nLockCount--;

                        if (it->second.nLockCount == 0)     // lock timed out
                        {
                            uint32_t    nPeerId     = it->second.nLockPeerId;
                            int64_t     ignoreUntil = GetAdjustedTime() + SMSG_TIME_IGNORE;

                            if (fDebugSmsg)
                                printf("Lock on bucket %" PRId64 " for peer %u timed out.\n", it->first, nPeerId);
                            // -- look through the nodes for the peer that locked this bucket
                            LOCK(cs_vNodes);
                            for (CNode* pnode : vNodes)
                            {
                                if (pnode->smsgData.nPeerId != nPeerId)
                                    continue;
                                pnode->smsgData.ignoreUntil = ignoreUntil;

                                // -- alert peer that they are being ignored
                                std::vector<unsigned char> vchData;
                                vchData.resize(8);
                                memcpy(&vchData[0], &ignoreUntil, 8);
                                pnode->PushMessage("smsgIgnore", vchData);

                                if (fDebugSmsg)
                                    printf("This node will ignore peer %u until %" PRId64 ".\n", nPeerId, ignoreUntil);
                                break;
                            };
                            it->second.nLockPeerId = 0;
                        }; // if (it->second.nLockCount == 0)
                    };
                    ++it;
                }; // ! if (it->first < cutoffTime)
            };
        }; // LOCK(cs_smsg);
    };

    printf("ThreadSecureMsg exited.\n");
};

void ThreadSecureMsgPow()
{
    // -- proof of work thread
    RenameThread("pinkcoin-smsg-pow"); // Make this thread recognisable

    int rv;
    std::vector<unsigned char> vchKey;
    SecMsgStored smsgStored;

    std::string sPrefix("qm");
    unsigned char chKey[18];


    while (fSecMsgenabled)
    {
        // -- sleep at end, then fSecMsgenabled is tested on wake

        SecMsgDB dbOutbox;
        std::unique_ptr<leveldb::Iterator> it;
        {
            LOCK(cs_smsgDB);

            if (!dbOutbox.Open("cr+"))
                continue;

            // -- fifo (smallest key first)
            it.reset(dbOutbox.pdb->NewIterator(leveldb::ReadOptions()));
        }
        // -- break up lock, SecureMsgSetHash will take long

        for (;;)
        {
            {
                LOCK(cs_smsgDB);
                if (!dbOutbox.NextSmesg(it.get(), sPrefix, chKey, smsgStored))
                    break;
            }

            unsigned char* pHeader = &smsgStored.vchMessage[0];
            unsigned char* pPayload = &smsgStored.vchMessage[SMSG_HDR_LEN];
            SecureMessage* psmsg = (SecureMessage*) pHeader;

            // -- do proof of work
            rv = SecureMsgSetHash(pHeader, pPayload, psmsg->nPayload);
            if (rv == 2)
                break; // /eave message in db, if terminated due to shutdown

            // -- message is removed here, no matter what
            {
                LOCK(cs_smsgDB);
                dbOutbox.EraseSmesg(chKey);
            }
            if (rv != 0)
            {
                printf("SecMsgPow: Could not get proof of work hash, message removed.\n");
                continue;
            };

            // -- add to message store
            {
                LOCK(cs_smsg);
                if (SecureMsgStore(pHeader, pPayload, psmsg->nPayload, true) != 0)
                {
                    printf("SecMsgPow: Could not place message in buckets, message removed.\n");
                    continue;
                };
            }

            // -- test if message was sent to self
            if (SecureMsgScanMessage(pHeader, pPayload, psmsg->nPayload, true) != 0)
            {
                // message recipient is not this node (or failed)
            };
        };

        {
            LOCK(cs_smsg);
            it.reset();
        }

        // -- shutdown thread waits 5 seconds, this should be less
        MilliSleep(1000); // milliseconds
    };

    printf("ThreadSecureMsgPow exited.\n");
};


bool SecureMsgReceiveData(CNode* pfrom, std::string strCommand, CDataStream& vRecv)
{
    /*
        Called from ProcessMessage
        Runs in ThreadMessageHandler2
    */

    if (fDebugSmsg)
        printf("SecureMsgReceiveData() %s %s.\n", pfrom->addrName.c_str(), strCommand.c_str());

    {
    // break up?
    LOCK(cs_smsg);

    if (strCommand == "smsgInv")
    {
        std::vector<unsigned char> vchData;
        vRecv >> vchData;

        if (vchData.size() < 4)
        {
            pfrom->Misbehaving(1);
            return false; // not enough data received to be a valid smsgInv
        };

        int64_t now = GetAdjustedTime();

        if (now < pfrom->smsgData.ignoreUntil)
        {
            if (fDebugSmsg)
                printf("Node is ignoring peer %u until %" PRId64 ".\n", pfrom->smsgData.nPeerId, pfrom->smsgData.ignoreUntil);
            return false;
        };

        uint32_t nBuckets       = smsgBuckets.size();
        uint32_t nLocked        = 0;    // no. of locked buckets on this node
        uint32_t nInvBuckets;           // no. of bucket headers sent by peer in smsgInv
        memcpy(&nInvBuckets, &vchData[0], 4);
        if (fDebugSmsg)
            printf("Remote node sent %d bucket headers, this has %d.\n", nInvBuckets, nBuckets);


        // -- Check no of buckets:
        if (nInvBuckets > (SMSG_RETENTION / SMSG_BUCKET_LEN) + 1) // +1 for some leeway
        {
            printf("Peer sent more bucket headers than possible %u, %u.\n", nInvBuckets, (SMSG_RETENTION / SMSG_BUCKET_LEN));
            pfrom->Misbehaving(1);
            return false;
        };

        if (vchData.size() < 4 + nInvBuckets*16)
        {
            printf("Remote node did not send enough data.\n");
            pfrom->Misbehaving(1);
            return false;
        };

        std::vector<unsigned char> vchDataOut;
        vchDataOut.reserve(4 + 8 * nInvBuckets); // reserve max possible size
        vchDataOut.resize(4);
        uint32_t nShowBuckets = 0;


        unsigned char *p = &vchData[4];
        for (uint32_t i = 0; i < nInvBuckets; ++i)
        {
            int64_t time;
            uint32_t ncontent, hash;
            memcpy(&time, p, 8);
            memcpy(&ncontent, p+8, 4);
            memcpy(&hash, p+12, 4);

            p += 16;

            // Check time valid:
            if (time < now - SMSG_RETENTION)
            {
                if (fDebugSmsg)
                    printf("Not interested in peer bucket %" PRId64 ", has expired.\n", time);

                if (time < now - SMSG_RETENTION - SMSG_TIME_LEEWAY)
                    pfrom->Misbehaving(1);
                continue;
            };
            if (time > now + SMSG_TIME_LEEWAY)
            {
                if (fDebugSmsg)
                    printf("Not interested in peer bucket %" PRId64 ", in the future.\n", time);
                pfrom->Misbehaving(1);
                continue;
            };

            if (ncontent < 1)
            {
                if (fDebugSmsg)
                    printf("Peer sent empty bucket, ignore %" PRId64 " %u %u.\n", time, ncontent, hash);
                continue;
            };

            if (fDebugSmsg)
            {
                printf("peer bucket %" PRId64 " %u %u.\n", time, ncontent, hash);
                printf("this bucket %" PRId64 " %" PRIszu " %u.\n", time, smsgBuckets[time].setTokens.size(), smsgBuckets[time].hash);
            };

            if (smsgBuckets[time].nLockCount > 0)
            {
                if (fDebugSmsg)
                    printf("Bucket is locked %u, waiting for peer %u to send data.\n", smsgBuckets[time].nLockCount, smsgBuckets[time].nLockPeerId);
                nLocked++;
                continue;
            };

            // -- if this node has more than the peer node, peer node will pull from this
            //    if then peer node has more this node will pull fom peer
            if (smsgBuckets[time].setTokens.size() < ncontent
                || (smsgBuckets[time].setTokens.size() == ncontent
                    && smsgBuckets[time].hash != hash)) // if same amount in buckets check hash
            {
                if (fDebugSmsg)
                    printf("Requesting contents of bucket %" PRId64 ".\n", time);

                uint32_t sz = vchDataOut.size();
                vchDataOut.resize(sz + 8);
                memcpy(&vchDataOut[sz], &time, 8);

                nShowBuckets++;
            };
        };

        // TODO: should include hash?
        memcpy(&vchDataOut[0], &nShowBuckets, 4);
        if (vchDataOut.size() > 4)
        {
            pfrom->PushMessage("smsgShow", vchDataOut);
        } else
        if (nLocked < 1) // Don't report buckets as matched if any are locked
        {
            // -- peer has no buckets we want, don't send them again until something changes
            //    peer will still request buckets from this node if needed (< ncontent)
            vchDataOut.resize(8);
            memcpy(&vchDataOut[0], &now, 8);
            pfrom->PushMessage("smsgMatch", vchDataOut);
            if (fDebugSmsg)
                printf("Sending smsgMatch, %" PRId64 ".\n", now);
        };

    } else
    if (strCommand == "smsgShow")
    {
        std::vector<unsigned char> vchData;
        vRecv >> vchData;

        if (vchData.size() < 4)
            return false;

        uint32_t nBuckets;
        memcpy(&nBuckets, &vchData[0], 4);

        if (vchData.size() < 4 + nBuckets * 8)
            return false;

        if (fDebugSmsg)
            printf("smsgShow: peer wants to see content of %u buckets.\n", nBuckets);

        std::map<int64_t, SecMsgBucket>::iterator itb;
        std::set<SecMsgToken>::iterator it;

        std::vector<unsigned char> vchDataOut;
        int64_t time;
        unsigned char* pIn = &vchData[4];
        for (uint32_t i = 0; i < nBuckets; ++i, pIn += 8)
        {
            memcpy(&time, pIn, 8);

            itb = smsgBuckets.find(time);
            if (itb == smsgBuckets.end())
            {
                if (fDebugSmsg)
                    printf("Don't have bucket %" PRId64 ".\n", time);
                continue;
            };

            std::set<SecMsgToken>& tokenSet = (*itb).second.setTokens;

            try {
                vchDataOut.resize(8 + 16 * tokenSet.size());
            } catch (std::exception& e) {
                printf("vchDataOut.resize %" PRIszu " threw: %s.\n", 8 + 16 * tokenSet.size(), e.what());
                continue;
            };
            memcpy(&vchDataOut[0], &time, 8);

            unsigned char* p = &vchDataOut[8];
            for (it = tokenSet.begin(); it != tokenSet.end(); ++it)
            {
                memcpy(p, &it->timestamp, 8);
                memcpy(p+8, &it->sample, 8);

                p += 16;
            };
            pfrom->PushMessage("smsgHave", vchDataOut);
        };


    } else
    if (strCommand == "smsgHave")
    {
        // -- peer has these messages in bucket
        std::vector<unsigned char> vchData;
        vRecv >> vchData;

        if (vchData.size() < 8)
            return false;

        int n = (vchData.size() - 8) / 16;

        int64_t time;
        memcpy(&time, &vchData[0], 8);

        // -- Check time valid:
        int64_t now = GetAdjustedTime();
        if (time < now - SMSG_RETENTION)
        {
            if (fDebugSmsg)
                printf("Not interested in peer bucket %" PRId64 ", has expired.\n", time);
            return false;
        };
        if (time > now + SMSG_TIME_LEEWAY)
        {
            if (fDebugSmsg)
                printf("Not interested in peer bucket %" PRId64 ", in the future.\n", time);
            pfrom->Misbehaving(1);
            return false;
        };

        if (smsgBuckets[time].nLockCount > 0)
        {
            if (fDebugSmsg)
                printf("Bucket %" PRId64 " lock count %u, waiting for message data from peer %u.\n", time, smsgBuckets[time].nLockCount, smsgBuckets[time].nLockPeerId);
            return false;
        };

        if (fDebugSmsg)
            printf("Sifting through bucket %" PRId64 ".\n", time);

        std::vector<unsigned char> vchDataOut;
        vchDataOut.resize(8);
        memcpy(&vchDataOut[0], &vchData[0], 8);

        std::set<SecMsgToken>& tokenSet = smsgBuckets[time].setTokens;
        std::set<SecMsgToken>::iterator it;
        SecMsgToken token;
        unsigned char* p = &vchData[8];

        for (int i = 0; i < n; ++i)
        {
            memcpy(&token.timestamp, p, 8);
            memcpy(&token.sample, p+8, 8);

            it = tokenSet.find(token);
            if (it == tokenSet.end())
            {
                int nd = vchDataOut.size();
                try {
                    vchDataOut.resize(nd + 16);
                } catch (std::exception& e) {
                    printf("vchDataOut.resize %d threw: %s.\n", nd + 16, e.what());
                    continue;
                };

                memcpy(&vchDataOut[nd], p, 16);
            };

            p += 16;
        };

        if (vchDataOut.size() > 8)
        {
            if (fDebugSmsg)
            {
                printf("Asking peer for  %" PRIszu " messages.\n", (vchDataOut.size() - 8) / 16);
                printf("Locking bucket %" PRIu64 " for peer %u.\n", time, pfrom->smsgData.nPeerId);
            };
            smsgBuckets[time].nLockCount   = 3; // lock this bucket for at most 3 * SMSG_THREAD_DELAY seconds, unset when peer sends smsgMsg
            smsgBuckets[time].nLockPeerId  = pfrom->smsgData.nPeerId;
            pfrom->PushMessage("smsgWant", vchDataOut);
        };
    } else
    if (strCommand == "smsgWant")
    {
        std::vector<unsigned char> vchData;
        vRecv >> vchData;

        if (vchData.size() < 8)
            return false;

        std::vector<unsigned char> vchOne;
        std::vector<unsigned char> vchBunch;

        vchBunch.resize(4+8); // nmessages + bucketTime

        int n = (vchData.size() - 8) / 16;

        int64_t time;
        uint32_t nBunch = 0;
        memcpy(&time, &vchData[0], 8);

        auto itb = smsgBuckets.find(time);
        if (itb == smsgBuckets.end())
        {
            if (fDebugSmsg)
                printf("Don't have bucket %" PRId64 ".\n", time);
            return false;
        };

        std::set<SecMsgToken>& tokenSet = itb->second.setTokens;
        std::set<SecMsgToken>::iterator it;
        SecMsgToken token;
        unsigned char* p = &vchData[8];
        for (int i = 0; i < n; ++i)
        {
            memcpy(&token.timestamp, p, 8);
            memcpy(&token.sample, p+8, 8);

            it = tokenSet.find(token);
            if (it == tokenSet.end())
            {
                if (fDebugSmsg)
                    printf("Don't have wanted message %" PRId64 ".\n", token.timestamp);
            } else
            {
                //printf("Have message at %d.\n", it->offset); // DEBUG
                token.offset = it->offset;
                //printf("winb before SecureMsgRetrieve %d.\n", token.timestamp);

                // -- place in vchOne so if SecureMsgRetrieve fails it won't corrupt vchBunch
                if (SecureMsgRetrieve(token, vchOne) == 0)
                {
                    nBunch++;
                    vchBunch.insert(vchBunch.end(), vchOne.begin(), vchOne.end()); // append
                } else
                {
                    printf("SecureMsgRetrieve failed %" PRId64 ".\n", token.timestamp);
                };

                if (nBunch >= 500
                    || vchBunch.size() >= 96000)
                {
                    if (fDebugSmsg)
                        printf("Break bunch %u, %" PRIszu ".\n", nBunch, vchBunch.size());
                    break; // end here, peer will send more want messages if needed.
                };
            };
            p += 16;
        };

        if (nBunch > 0)
        {
            if (fDebugSmsg)
                printf("Sending block of %u messages for bucket %" PRId64 ".\n", nBunch, time);

            memcpy(&vchBunch[0], &nBunch, 4);
            memcpy(&vchBunch[4], &time, 8);
            pfrom->PushMessage("smsgMsg", vchBunch);
        };
    } else
    if (strCommand == "smsgMsg")
    {
        std::vector<unsigned char> vchData;
        vRecv >> vchData;

        if (fDebugSmsg)
            printf("smsgMsg vchData.size() %" PRIszu ".\n", vchData.size());

        SecureMsgReceive(pfrom, vchData);
    } else
    if (strCommand == "smsgMatch")
    {
        std::vector<unsigned char> vchData;
        vRecv >> vchData;


        if (vchData.size() < 8)
        {
            printf("smsgMatch, not enough data %" PRIszu ".\n", vchData.size());
            pfrom->Misbehaving(1);
            return false;
        };

        int64_t time;
        memcpy(&time, &vchData[0], 8);

        int64_t now = GetAdjustedTime();
        if (time > now + SMSG_TIME_LEEWAY)
        {
            printf("Warning: Peer buckets matched in the future: %" PRId64 ".\nEither this node or the peer node has the incorrect time set.\n", time);
            if (fDebugSmsg)
                printf("Peer match time set to now.\n");
            time = now;
        };

        pfrom->smsgData.lastMatched = time;

        if (fDebugSmsg)
            printf("Peer buckets matched at %" PRId64 ".\n", time);

    } else
    if (strCommand == "smsgPing")
    {
        // -- smsgPing is the initial message, send reply
        pfrom->PushMessage("smsgPong");
    } else
    if (strCommand == "smsgPong")
    {
        if (fDebugSmsg)
             printf("Peer replied, secure messaging enabled.\n");

        pfrom->smsgData.fEnabled = true;
    } else
    if (strCommand == "smsgDisabled")
    {
        // -- peer has disabled secure messaging.

        pfrom->smsgData.fEnabled = false;

        if (fDebugSmsg)
            printf("Peer %u has disabled secure messaging.\n", pfrom->smsgData.nPeerId);

    } else
    if (strCommand == "smsgIgnore")
    {
        // -- peer is reporting that it will ignore this node until time.
        //    Ignore peer too
        std::vector<unsigned char> vchData;
        vRecv >> vchData;

        if (vchData.size() < 8)
        {
            printf("smsgIgnore, not enough data %" PRIszu ".\n", vchData.size());
            pfrom->Misbehaving(1);
            return false;
        };

        int64_t time;
        memcpy(&time, &vchData[0], 8);

        pfrom->smsgData.ignoreUntil = time;

        if (fDebugSmsg)
            printf("Peer %u is ignoring this node until %" PRId64 ", ignore peer too.\n", pfrom->smsgData.nPeerId, time);
    } else
    {
        // Unknown message
    };

    }; //  LOCK(cs_smsg);

    return true;
};

bool SecureMsgSendData(CNode* pto, bool fSendTrickle)
{
    /*
        Called from ProcessMessage
        Runs in ThreadMessageHandler2
    */

    //printf("SecureMsgSendData() %s.\n", pto->addrName.c_str());


    int64_t now = GetAdjustedTime();

    if (pto->smsgData.lastSeen == 0)
    {
        // -- first contact
        pto->smsgData.nPeerId = nPeerIdCounter++;
        if (fDebugSmsg)
            printf("SecureMsgSendData() new node %s, peer id %u.\n", pto->addrName.c_str(), pto->smsgData.nPeerId);
        // -- Send smsgPing once, do nothing until receive 1st smsgPong (then set fEnabled)
        pto->PushMessage("smsgPing");
        pto->smsgData.lastSeen = GetAdjustedTime();
        return true;
    } else
    if (!pto->smsgData.fEnabled
        || now - pto->smsgData.lastSeen < SMSG_SEND_DELAY
        || now < pto->smsgData.ignoreUntil)
    {
        return true;
    };

    // -- When nWakeCounter == 0, resend bucket inventory.
    if (pto->smsgData.nWakeCounter < 1)
    {
        pto->smsgData.lastMatched = 0;
        pto->smsgData.nWakeCounter = 10 + GetRandInt(300);  // set to a random time between [10, 300] * SMSG_SEND_DELAY seconds

        if (fDebugSmsg)
            printf("SecureMsgSendData(): nWakeCounter expired, sending bucket inventory to %s.\n"
            "Now %" PRId64 " next wake counter %u\n", pto->addrName.c_str(), now, pto->smsgData.nWakeCounter);
    };
    pto->smsgData.nWakeCounter--;

    {
        LOCK(cs_smsg);
        std::map<int64_t, SecMsgBucket>::iterator it;

        uint32_t nBuckets = smsgBuckets.size();
        if (nBuckets > 0) // no need to send keep alive pkts, coin messages already do that
        {
            std::vector<unsigned char> vchData;
            // should reserve?
            vchData.reserve(4 + nBuckets*16); // timestamp + size + hash

            uint32_t nBucketsShown = 0;
            vchData.resize(4);

            unsigned char* p = &vchData[4];
            for (it = smsgBuckets.begin(); it != smsgBuckets.end(); ++it)
            {
                SecMsgBucket &bkt = it->second;

                uint32_t nMessages = bkt.setTokens.size();

                if (bkt.timeChanged < pto->smsgData.lastMatched     // peer has this bucket
                    || nMessages < 1)                               // this bucket is empty
                    continue;


                uint32_t hash = bkt.hash;

                try {
                    vchData.resize(vchData.size() + 16);
                } catch (std::exception& e) {
                    printf("vchData.resize %" PRIszu " threw: %s.\n", vchData.size() + 16, e.what());
                    continue;
                };
                memcpy(p, &it->first, 8);
                memcpy(p+8, &nMessages, 4);
                memcpy(p+12, &hash, 4);

                p += 16;
                nBucketsShown++;
                //if (fDebug)
                //    printf("Sending bucket %d, size %d \n", it->first, it->second.size());
            };

            if (vchData.size() > 4)
            {
                memcpy(&vchData[0], &nBucketsShown, 4);
                if (fDebugSmsg)
                    printf("Sending %d bucket headers.\n", nBucketsShown);

                pto->PushMessage("smsgInv", vchData);
            };
        };
    }

    pto->smsgData.lastSeen = GetAdjustedTime();

    return true;
};
