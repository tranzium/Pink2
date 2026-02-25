// Copyright (c) 2014 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Message storage, retrieval, reception, and sending functions
// extracted from smessage.cpp.

#include "smessage/smessage.h"

#include <stdint.h>
#include <errno.h>
#include <sstream>
#include <filesystem>

#include "base58.h"
#include "init.h" // pwalletMain
#include "util.h"


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


int SecureMsgScanMessage(unsigned char *pHeader, unsigned char *pPayload, uint32_t nPayload, bool reportToGui)
{
    /*
    Check if message belongs to this node.
    If so add to inbox db.

    if !reportToGui don't fire NotifySecMsgInboxChanged
     - loads messages received when wallet locked in bulk.

    returns
        0 success,
        1 error
        2 no match
        3 wallet is locked - message stored for scanning later.
    */

    if (fDebugSmsg)
        printf("SecureMsgScanMessage()\n");

    if (pwalletMain->IsLocked())
    {
        if (fDebugSmsg)
            printf("ScanMessage: Wallet is locked, storing message to scan later.\n");

        int rv;
        if ((rv = SecureMsgStoreUnscanned(pHeader, pPayload, nPayload)) != 0)
            return 1;

        return 3;
    };

    std::string addressTo;
    MessageData msg; // placeholder
    bool fOwnMessage = false;

    for (const auto& smsgAddr : smsgAddresses)
    {
        if (!smsgAddr.fReceiveEnabled)
            continue;

        CBitcoinAddress coinAddress(smsgAddr.sAddress);
        addressTo = coinAddress.ToString();

        if (!smsgAddr.fReceiveAnon)
        {
            // -- have to do full decrypt to see address from
            if (SecureMsgDecrypt(false, addressTo, pHeader, pPayload, nPayload, msg) == 0)
            {
                if (fDebugSmsg)
                    printf("Decrypted message with %s.\n", addressTo.c_str());

                if (msg.sFromAddress.compare("anon") != 0)
                    fOwnMessage = true;
                break;
            };
        } else
        {

            if (SecureMsgDecrypt(true, addressTo, pHeader, pPayload, nPayload, msg) == 0)
            {
                if (fDebugSmsg)
                    printf("Decrypted message with %s.\n", addressTo.c_str());

                fOwnMessage = true;
                break;
            };
        }
    };

    if (fOwnMessage)
    {
        // -- save to inbox
        SecureMessage* psmsg = (SecureMessage*) pHeader;
        std::string sPrefix("im");
        unsigned char chKey[18];
        memcpy(&chKey[0],  sPrefix.data(),    2);
        memcpy(&chKey[2],  &psmsg->timestamp, 8);
        memcpy(&chKey[10], pPayload,          8);

        SecMsgStored smsgInbox;
        smsgInbox.timeReceived  = GetAdjustedTime();
        smsgInbox.status        = (SMSG_MASK_UNREAD) & 0xFF;
        smsgInbox.sAddrTo       = addressTo;

        // -- data may not be contiguous
        try {
            smsgInbox.vchMessage.resize(SMSG_HDR_LEN + nPayload);
        } catch (std::exception& e) {
            printf("SecureMsgScanMessage(): Could not resize vchData, %u, %s\n", SMSG_HDR_LEN + nPayload, e.what());
            return 1;
        };
        memcpy(&smsgInbox.vchMessage[0], pHeader, SMSG_HDR_LEN);
        memcpy(&smsgInbox.vchMessage[SMSG_HDR_LEN], pPayload, nPayload);

        {
            LOCK(cs_smsgDB);
            SecMsgDB dbInbox;

            if (dbInbox.Open("cw"))
            {
                if (dbInbox.ExistsSmesg(chKey))
                {
                    if (fDebugSmsg)
                        printf("Message already exists in inbox db.\n");
                } else
                {
                    dbInbox.WriteSmesg(chKey, smsgInbox);

                    if (reportToGui)
                        NotifySecMsgInboxChanged(smsgInbox);
                    printf("SecureMsg saved to inbox, received with %s.\n", addressTo.c_str());
                };
            };
        }
    };

    return 0;
};

int SecureMsgRetrieve(SecMsgToken &token, std::vector<unsigned char>& vchData)
{
    if (fDebugSmsg)
        printf("SecureMsgRetrieve() %" PRId64 ".\n", token.timestamp);

    // -- has cs_smsg lock from SecureMsgReceiveData

    fs::path pathSmsgDir = GetDataDir() / "smsgStore";

    //printf("token.offset %d.\n", token.offset); // DEBUG
    int64_t bucket = token.timestamp - (token.timestamp % SMSG_BUCKET_LEN);
    std::string fileName = std::to_string(bucket) + "_01.dat";
    fs::path fullpath = pathSmsgDir / fileName;

    //printf("bucket %d.\n", bucket);
    //printf("bucket lld %lld.\n", bucket);
    //printf("fileName %s.\n", fileName.c_str());

    FILE *fp;
    errno = 0;
    if (!(fp = fopen(fullpath.string().c_str(), "rb")))
    {
        printf("Error opening file: %s\nPath %s\n", strerror(errno), fullpath.string().c_str());
        return 1;
    };

    errno = 0;
    if (fseek(fp, token.offset, SEEK_SET) != 0)
    {
        printf("fseek, strerror: %s.\n", strerror(errno));
        fclose(fp);
        return 1;
    };

    SecureMessage smsg;
    errno = 0;
    if (fread(&smsg.hash[0], sizeof(unsigned char), SMSG_HDR_LEN, fp) != static_cast<size_t>(SMSG_HDR_LEN))
    {
        printf("fread header failed: %s\n", strerror(errno));
        fclose(fp);
        return 1;
    };

    try {
        vchData.resize(SMSG_HDR_LEN + smsg.nPayload);
    } catch (std::exception& e) {
        printf("SecureMsgRetrieve(): Could not resize vchData, %u, %s\n", SMSG_HDR_LEN + smsg.nPayload, e.what());
        return 1;
    };

    memcpy(&vchData[0], &smsg.hash[0], SMSG_HDR_LEN);
    errno = 0;
    if (fread(&vchData[SMSG_HDR_LEN], sizeof(unsigned char), smsg.nPayload, fp) != smsg.nPayload)
    {
        printf("fread data failed: %s. Wanted %u bytes.\n", strerror(errno), smsg.nPayload);
        fclose(fp);
        return 1;
    };


    fclose(fp);

    return 0;
};

int SecureMsgReceive(CNode* pfrom, std::vector<unsigned char>& vchData)
{
    if (fDebugSmsg)
        printf("SecureMsgReceive().\n");

    if (vchData.size() < 12) // nBunch4 + timestamp8
    {
        printf("Error: not enough data.\n");
        return 1;
    };

    uint32_t nBunch;
    int64_t bktTime;

    memcpy(&nBunch, &vchData[0], 4);
    memcpy(&bktTime, &vchData[4], 8);


    // -- check bktTime ()
    //    bucket may not exist yet - will be created when messages are added
    int64_t now = GetAdjustedTime();
    if (bktTime > now + SMSG_TIME_LEEWAY)
    {
        if (fDebugSmsg)
            printf("bktTime > now.\n");
        // misbehave?
        return 1;
    } else
    if (bktTime < now - SMSG_RETENTION)
    {
        if (fDebugSmsg)
            printf("bktTime < now - SMSG_RETENTION.\n");
        // misbehave?
        return 1;
    };

    std::map<int64_t, SecMsgBucket>::iterator itb;

    if (nBunch == 0 || nBunch > 500)
    {
        printf("Error: Invalid no. messages received in bunch %u, for bucket %" PRId64 ".\n", nBunch, bktTime);
        pfrom->Misbehaving(1);

        // -- release lock on bucket if it exists
        itb = smsgBuckets.find(bktTime);
        if (itb != smsgBuckets.end())
            itb->second.nLockCount = 0;
        return 1;
    };

    uint32_t n = 12;

    for (uint32_t i = 0; i < nBunch; ++i)
    {
        if (vchData.size() - n < SMSG_HDR_LEN)
        {
            printf("Error: not enough data sent, n = %u.\n", n);
            break;
        };

        SecureMessage* psmsg = (SecureMessage*) &vchData[n];

        int rv;
        if ((rv = SecureMsgValidate(&vchData[n], &vchData[n + SMSG_HDR_LEN], psmsg->nPayload)) != 0)
        {
            // message dropped
            if (rv == 2) // invalid proof of work
            {
                pfrom->Misbehaving(10);
            } else
            {
                pfrom->Misbehaving(1);
            };
            continue;
        };

        // -- store message, but don't hash bucket
        if (SecureMsgStore(&vchData[n], &vchData[n + SMSG_HDR_LEN], psmsg->nPayload, false) != 0)
        {
            // message dropped
            break; // continue?
        };

        if (SecureMsgScanMessage(&vchData[n], &vchData[n + SMSG_HDR_LEN], psmsg->nPayload, true) != 0)
        {
            // message recipient is not this node (or failed)
        };

        n += SMSG_HDR_LEN + psmsg->nPayload;
    };

    // -- if messages have been added, bucket must exist now
    itb = smsgBuckets.find(bktTime);
    if (itb == smsgBuckets.end())
    {
        if (fDebugSmsg)
            printf("Don't have bucket %" PRId64 ".\n", bktTime);
        return 1;
    };

    itb->second.nLockCount  = 0; // this node has received data from peer, release lock
    itb->second.nLockPeerId = 0;
    itb->second.hashBucket();

    return 0;
};

int SecureMsgStoreUnscanned(unsigned char *pHeader, unsigned char *pPayload, uint32_t nPayload)
{
    /*
    When the wallet is locked a copy of each received message is stored to be scanned later if wallet is unlocked
    */

    if (fDebugSmsg)
        printf("SecureMsgStoreUnscanned()\n");

    if (!pHeader
        || !pPayload)
    {
        printf("Error: null pointer to header or payload.\n");
        return 1;
    };

    SecureMessage* psmsg = (SecureMessage*) pHeader;

    fs::path pathSmsgDir;
    try {
        pathSmsgDir = GetDataDir() / "smsgStore";
        fs::create_directory(pathSmsgDir);
    } catch (const std::filesystem::filesystem_error& ex)
    {
        printf("Error: Failed to create directory %s - %s\n", pathSmsgDir.string().c_str(), ex.what());
        return 1;
    };

    int64_t now = GetAdjustedTime();
    if (psmsg->timestamp > now + SMSG_TIME_LEEWAY)
    {
        printf("Message > now.\n");
        return 1;
    } else
    if (psmsg->timestamp < now - SMSG_RETENTION)
    {
        printf("Message < SMSG_RETENTION.\n");
        return 1;
    };

    int64_t bucket = psmsg->timestamp - (psmsg->timestamp % SMSG_BUCKET_LEN);

    std::string fileName = std::to_string(bucket) + "_01_wl.dat";
    fs::path fullpath = pathSmsgDir / fileName;

    FILE *fp;
    errno = 0;
    if (!(fp = fopen(fullpath.string().c_str(), "ab")))
    {
        printf("Error opening file: %s\n", strerror(errno));
        return 1;
    };

    if (fwrite(pHeader, sizeof(unsigned char), SMSG_HDR_LEN, fp) != static_cast<size_t>(SMSG_HDR_LEN)
        || fwrite(pPayload, sizeof(unsigned char), nPayload, fp) != nPayload)
    {
        printf("fwrite failed: %s\n", strerror(errno));
        fclose(fp);
        return 1;
    };

    fclose(fp);

    return 0;
};


int SecureMsgStore(unsigned char *pHeader, unsigned char *pPayload, uint32_t nPayload, bool fUpdateBucket)
{
    if (fDebugSmsg)
        printf("SecureMsgStore()\n");

    if (!pHeader
        || !pPayload)
    {
        printf("Error: null pointer to header or payload.\n");
        return 1;
    };

    SecureMessage* psmsg = (SecureMessage*) pHeader;


    long int ofs;
    fs::path pathSmsgDir;
    try {
        pathSmsgDir = GetDataDir() / "smsgStore";
        fs::create_directory(pathSmsgDir);
    } catch (const std::filesystem::filesystem_error& ex)
    {
        printf("Error: Failed to create directory %s - %s\n", pathSmsgDir.string().c_str(), ex.what());
        return 1;
    };

    int64_t now = GetAdjustedTime();
    if (psmsg->timestamp > now + SMSG_TIME_LEEWAY)
    {
        printf("Message > now.\n");
        return 1;
    } else
    if (psmsg->timestamp < now - SMSG_RETENTION)
    {
        printf("Message < SMSG_RETENTION.\n");
        return 1;
    };

    int64_t bucket = psmsg->timestamp - (psmsg->timestamp % SMSG_BUCKET_LEN);

    {
        // -- must lock cs_smsg before calling
        //LOCK(cs_smsg);

        SecMsgToken token(psmsg->timestamp, pPayload, nPayload, 0);

        std::set<SecMsgToken>& tokenSet = smsgBuckets[bucket].setTokens;
        auto it = tokenSet.find(token);
        if (it != tokenSet.end())
        {
            printf("Already have message.\n");
            if (fDebugSmsg)
            {
                printf("nPayload: %u\n", nPayload);
                printf("bucket: %" PRId64 "\n", bucket);

                printf("message ts: %" PRId64 "", token.timestamp);
                std::vector<unsigned char> vchShow;
                vchShow.resize(8);
                memcpy(&vchShow[0], token.sample, 8);
                printf(" sample %s\n", ValueString(vchShow).c_str());
                /*
                printf("\nmessages in bucket:\n");
                for (it = tokenSet.begin(); it != tokenSet.end(); ++it)
                {
                    printf("message ts: %d", (*it).timestamp);
                    vchShow.resize(8);
                    memcpy(&vchShow[0], (*it).sample, 8);
                    printf(" sample %s\n", ValueString(vchShow).c_str());
                };
                */
            };
            return 1;
        };

        std::string fileName = std::to_string(bucket) + "_01.dat";
        fs::path fullpath = pathSmsgDir / fileName;

        FILE *fp;
        errno = 0;
        if (!(fp = fopen(fullpath.string().c_str(), "ab")))
        {
            printf("Error opening file: %s\n", strerror(errno));
            return 1;
        };

        // -- on windows ftell will always return 0 after fopen(ab), call fseek to set.
        errno = 0;
        if (fseek(fp, 0, SEEK_END) != 0)
        {
            printf("Error fseek failed: %s\n", strerror(errno));
            return 1;
        };


        ofs = ftell(fp);

        if (fwrite(pHeader, sizeof(unsigned char), SMSG_HDR_LEN, fp) != static_cast<size_t>(SMSG_HDR_LEN)
            || fwrite(pPayload, sizeof(unsigned char), nPayload, fp) != nPayload)
        {
            printf("fwrite failed: %s\n", strerror(errno));
            fclose(fp);
            return 1;
        };

        fclose(fp);

        token.offset = ofs;

        //printf("token.offset: %d\n", token.offset); // DEBUG
        tokenSet.insert(token);

        if (fUpdateBucket)
            smsgBuckets[bucket].hashBucket();
    };

    //if (fDebugSmsg)
    printf("SecureMsg added to bucket %" PRId64 ".\n", bucket);
    return 0;
};

int SecureMsgStore(SecureMessage& smsg, bool fUpdateBucket)
{
    return SecureMsgStore(&smsg.hash[0], smsg.pPayload, smsg.nPayload, fUpdateBucket);
};

int SecureMsgSend(std::string& addressFrom, std::string& addressTo, std::string& message, std::string& sError)
{
    /* Encrypt secure message, and place it on the network
        Make a copy of the message to sender's first address and place in send queue db
        proof of work thread will pick up messages from  send queue db

    */

    if (fDebugSmsg)
        printf("SecureMsgSend(%s, %s, ...)\n", addressFrom.c_str(), addressTo.c_str());

    if (pwalletMain->IsLocked())
    {
        sError = "Wallet is locked, wallet must be unlocked to send and recieve messages.";
        printf("Wallet is locked, wallet must be unlocked to send and recieve messages.\n");
        return 1;
    };

    if (message.size() > SMSG_MAX_MSG_BYTES)
    {
        std::ostringstream oss;
        oss << message.size() << " > " << SMSG_MAX_MSG_BYTES;
        sError = "Message is too long, " + oss.str();
        printf("Message is too long, %" PRIszu ".\n", message.size());
        return 1;
    };


    int rv;
    SecureMessage smsg;

    if ((rv = SecureMspinkcrypt(smsg, addressFrom, addressTo, message)) != 0)
    {
        printf("SecureMsgSend(), encrypt for recipient failed.\n");

        switch(rv)
        {
            case 2:  sError = "Message is too long.";                       break;
            case 3:  sError = "Invalid addressFrom.";                       break;
            case 4:  sError = "Invalid addressTo.";                         break;
            case 5:  sError = "Could not get public key for addressTo.";    break;
            case 6:  sError = "ECDH_compute_key failed.";                   break;
            case 7:  sError = "Could not get private key for addressFrom."; break;
            case 8:  sError = "Could not allocate memory.";                 break;
            case 9:  sError = "Could not compress message data.";           break;
            case 10: sError = "Could not generate MAC.";                    break;
            case 11: sError = "Encrypt failed.";                            break;
            default: sError = "Unspecified Error.";                         break;
        };

        return rv;
    };


    // -- Place message in send queue, proof of work will happen in a thread.
    std::string sPrefix("qm");
    unsigned char chKey[18];
    memcpy(&chKey[0],  sPrefix.data(),  2);
    memcpy(&chKey[2],  &smsg.timestamp, 8);
    memcpy(&chKey[10], &smsg.pPayload,  8);

    SecMsgStored smsgSQ;

    smsgSQ.timeReceived  = GetAdjustedTime();
    smsgSQ.sAddrTo       = addressTo;

    try {
        smsgSQ.vchMessage.resize(SMSG_HDR_LEN + smsg.nPayload);
    } catch (std::exception& e) {
        printf("smsgSQ.vchMessage.resize %u threw: %s.\n", SMSG_HDR_LEN + smsg.nPayload, e.what());
        sError = "Could not allocate memory.";
        return 8;
    };

    memcpy(&smsgSQ.vchMessage[0], &smsg.hash[0], SMSG_HDR_LEN);
    memcpy(&smsgSQ.vchMessage[SMSG_HDR_LEN], smsg.pPayload, smsg.nPayload);

    {
        LOCK(cs_smsgDB);
        SecMsgDB dbSendQueue;
        if (dbSendQueue.Open("cw"))
        {
            dbSendQueue.WriteSmesg(chKey, smsgSQ);
            //NotifySecMsgSendQueueChanged(smsgOutbox);
        };
    }

    // TODO: only update outbox when proof of work thread is done.

    //  -- for outbox create a copy encrypted for owned address
    //     if the wallet is encrypted private key needed to decrypt will be unavailable

    if (fDebugSmsg)
        printf("Encrypting message for outbox.\n");

    std::string addressOutbox = "None";
    CBitcoinAddress coinAddrOutbox;

    for (const auto& entry : pwalletMain->mapAddressBook)
    {
        // -- get first owned address
        if (!IsMine(*pwalletMain, entry.first))
            continue;

        const CBitcoinAddress& address = entry.first;

        addressOutbox = address.ToString();
        if (!coinAddrOutbox.SetString(addressOutbox)) // test valid
            continue;
        break;
    };

    if (addressOutbox == "None")
    {
        printf("Warning: SecureMsgSend() could not find an address to encrypt outbox message with.\n");
    } else
    {
        if (fDebugSmsg)
            printf("Encrypting a copy for outbox, using address %s\n", addressOutbox.c_str());

        SecureMessage smsgForOutbox;
        if ((rv = SecureMspinkcrypt(smsgForOutbox, addressFrom, addressOutbox, message)) != 0)
        {
            printf("SecureMsgSend(), encrypt for outbox failed, %d.\n", rv);
        } else
        {
            // -- save sent message to db
            std::string sPrefix("sm");
            unsigned char chKey[18];
            memcpy(&chKey[0],  sPrefix.data(),           2);
            memcpy(&chKey[2],  &smsgForOutbox.timestamp, 8);
            memcpy(&chKey[10], &smsgForOutbox.pPayload,  8);   // sample

            SecMsgStored smsgOutbox;

            smsgOutbox.timeReceived  = GetAdjustedTime();
            smsgOutbox.sAddrTo       = addressTo;
            smsgOutbox.sAddrOutbox   = addressOutbox;

            try {
                smsgOutbox.vchMessage.resize(SMSG_HDR_LEN + smsgForOutbox.nPayload);
            } catch (std::exception& e) {
                printf("smsgOutbox.vchMessage.resize %u threw: %s.\n", SMSG_HDR_LEN + smsgForOutbox.nPayload, e.what());
                sError = "Could not allocate memory.";
                return 8;
            };
            memcpy(&smsgOutbox.vchMessage[0], &smsgForOutbox.hash[0], SMSG_HDR_LEN);
            memcpy(&smsgOutbox.vchMessage[SMSG_HDR_LEN], smsgForOutbox.pPayload, smsgForOutbox.nPayload);


            {
                LOCK(cs_smsgDB);
                SecMsgDB dbSent;

                if (dbSent.Open("cw"))
                {
                    dbSent.WriteSmesg(chKey, smsgOutbox);
                    NotifySecMsgOutboxChanged(smsgOutbox);
                };
            }
        };
    };

    if (fDebugSmsg)
        printf("Secure message queued for sending to %s.\n", addressTo.c_str());

    return 0;
};
