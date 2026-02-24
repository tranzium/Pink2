// Copyright (c) 2014 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"

#include "smessage.h"
#include "init.h" // pwalletMain


extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json& entry);



json smsgenable(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "smsgenable \n"
            "Enable secure messaging.");

    if (fSecMsgenabled)
        throw std::runtime_error("Secure messaging is already enabled.");

    json result;
    if (!SecureMsgEnable())
    {
        result["result"] = "Failed to enable secure messaging.";
    } else
    {
        result["result"] = "Enabled secure messaging.";
    }
    return result;
}

json smsgdisable(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "smsgdisable \n"
            "Disable secure messaging.");
    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is already disabled.");

    json result;
    if (!SecureMsgDisable())
    {
        result["result"] = "Failed to disable secure messaging.";
    } else
    {
        result["result"] = "Disabled secure messaging.";
    }
    return result;
}

json smsgoptions(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw std::runtime_error(
            "smsgoptions [list|set <optname> <value>]\n"
            "List and manage options.");

    std::string mode = "list";
    if (!params.empty())
    {
        mode = params[0].get<std::string>();
    };

    json result;

    if (mode == "list")
    {
        json options = json::array();
        options.push_back(std::string("newAddressRecv = ") + (smsgOptions.fNewAddressRecv ? "true" : "false"));
        options.push_back(std::string("newAddressAnon = ") + (smsgOptions.fNewAddressAnon ? "true" : "false"));
        result["options"] = options;

        result["result"] = "Success.";
    } else
    if (mode == "set")
    {
        if (params.size() < 3)
        {
            result["result"] = "Too few parameters.";
            result["expected"] = "set <optname> <value>";
            return result;
        };

        std::string optname = params[1].get<std::string>();
        std::string value   = params[2].get<std::string>();

        if (optname == "newAddressRecv")
        {
            if (value == "+" || value == "on"  || value == "true"  || value == "1")
            {
                smsgOptions.fNewAddressRecv = true;
            } else
            if (value == "-" || value == "off" || value == "false" || value == "0")
            {
                smsgOptions.fNewAddressRecv = false;
            } else
            {
                result["result"] = "Unknown value.";
                return result;
            };
            result["set option"] = std::string("newAddressRecv = ") + (smsgOptions.fNewAddressRecv ? "true" : "false");
        } else
        if (optname == "newAddressAnon")
        {
            if (value == "+" || value == "on"  || value == "true"  || value == "1")
            {
                smsgOptions.fNewAddressAnon = true;
            } else
            if (value == "-" || value == "off" || value == "false" || value == "0")
            {
                smsgOptions.fNewAddressAnon = false;
            } else
            {
                result["result"] = "Unknown value.";
                return result;
            };
            result["set option"] = std::string("newAddressAnon = ") + (smsgOptions.fNewAddressAnon ? "true" : "false");
        } else
        {
            result["result"] = "Option not found.";
            return result;
        };
    } else
    {
        result["result"] = "Unknown Mode.";
        result["expected"] = "smsgoption [list|set <optname> <value>]";
    };
    return result;
}

json smsglocalkeys(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw std::runtime_error(
            "smsglocalkeys [whitelist|all|wallet|recv <+/-> <address>|anon <+/-> <address>]\n"
            "List and manage keys.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    json result;

    std::string mode = "whitelist";
    if (!params.empty())
    {
        mode = params[0].get<std::string>();
    };

    char cbuf[256];

    if (mode == "whitelist"
        || mode == "all")
    {
        uint32_t nKeys = 0;
        int all = mode == "all" ? 1 : 0;
        json keyList = json::array();
        for (auto& smsgAddr : smsgAddresses)
        {
            if (!all
                && !smsgAddr.fReceiveEnabled)
                continue;

            CBitcoinAddress coinAddress(smsgAddr.sAddress);
            if (!coinAddress.IsValid())
                continue;

            std::string sPublicKey;

            CKeyID keyID;
            if (!coinAddress.GetKeyID(keyID))
                continue;

            CPubKey pubKey;
            if (!pwalletMain->GetPubKey(keyID, pubKey))
                continue;
            if (!pubKey.IsValid()
                || !pubKey.IsCompressed())
            {
                continue;
            };


            sPublicKey = EncodeBase58(pubKey.Raw());

            std::string sLabel = pwalletMain->mapAddressBook[keyID];
            std::string sInfo;
            if (all)
                sInfo = std::string("Receive ") + (smsgAddr.fReceiveEnabled ? "on,  " : "off, ");
            sInfo += std::string("Anon ") + (smsgAddr.fReceiveAnon ? "on" : "off");
            keyList.push_back(smsgAddr.sAddress + " - " + sPublicKey + " " + sInfo + " - " + sLabel);

            nKeys++;
        };

        result["keys"] = keyList;

        snprintf(cbuf, sizeof(cbuf), "%u keys listed.", nKeys);
        result["result"] = std::string(cbuf);

    } else
    if (mode == "recv")
    {
        if (params.size() < 3)
        {
            result["result"] = "Too few parameters.";
            result["expected"] = "recv <+/-> <address>";
            return result;
        };

        std::string op      = params[1].get<std::string>();
        std::string addr    = params[2].get<std::string>();

        std::vector<SecMsgAddress>::iterator it;
        for (it = smsgAddresses.begin(); it != smsgAddresses.end(); ++it)
        {
            if (addr != it->sAddress)
                continue;
            break;
        };

        if (it == smsgAddresses.end())
        {
            result["result"] = "Address not found.";
            return result;
        };

        if (op == "+" || op == "on"  || op == "add" || op == "a")
        {
            it->fReceiveEnabled = true;
        } else
        if (op == "-" || op == "off" || op == "rem" || op == "r")
        {
            it->fReceiveEnabled = false;
        } else
        {
            result["result"] = "Unknown operation.";
            return result;
        };

        std::string sInfo;
        sInfo = std::string("Receive ") + (it->fReceiveEnabled ? "on, " : "off,");
        sInfo += std::string("Anon ") + (it->fReceiveAnon ? "on" : "off");
        result["result"] = "Success.";
        result["key"] = it->sAddress + " " + sInfo;
        return result;

    } else
    if (mode == "anon")
    {
        if (params.size() < 3)
        {
            result["result"] = "Too few parameters.";
            result["expected"] = "anon <+/-> <address>";
            return result;
        };

        std::string op      = params[1].get<std::string>();
        std::string addr    = params[2].get<std::string>();

        std::vector<SecMsgAddress>::iterator it;
        for (it = smsgAddresses.begin(); it != smsgAddresses.end(); ++it)
        {
            if (addr != it->sAddress)
                continue;
            break;
        };

        if (it == smsgAddresses.end())
        {
            result["result"] = "Address not found.";
            return result;
        };

        if (op == "+" || op == "on"  || op == "add" || op == "a")
        {
            it->fReceiveAnon = true;
        } else
        if (op == "-" || op == "off" || op == "rem" || op == "r")
        {
            it->fReceiveAnon = false;
        } else
        {
            result["result"] = "Unknown operation.";
            return result;
        };

        std::string sInfo;
        sInfo = std::string("Receive ") + (it->fReceiveEnabled ? "on, " : "off,");
        sInfo += std::string("Anon ") + (it->fReceiveAnon ? "on" : "off");
        result["result"] = "Success.";
        result["key"] = it->sAddress + " " + sInfo;
        return result;

    } else
    if (mode == "wallet")
    {
        uint32_t nKeys = 0;
        json keyList = json::array();
        for (const auto& entry : pwalletMain->mapAddressBook)
        {
            if (!IsMine(*pwalletMain, entry.first))
                continue;

            CBitcoinAddress coinAddress(entry.first);
            if (!coinAddress.IsValid())
                continue;

            std::string address;
            std::string sPublicKey;
            address = coinAddress.ToString();

            CKeyID keyID;
            if (!coinAddress.GetKeyID(keyID))
                continue;

            CPubKey pubKey;
            if (!pwalletMain->GetPubKey(keyID, pubKey))
                continue;
            if (!pubKey.IsValid()
                || !pubKey.IsCompressed())
            {
                continue;
            };

            sPublicKey = EncodeBase58(pubKey.Raw());

            keyList.push_back(address + " - " + sPublicKey + " - " + entry.second);
            nKeys++;
        };

        result["keys"] = keyList;

        snprintf(cbuf, sizeof(cbuf), "%u keys listed from wallet.", nKeys);
        result["result"] = std::string(cbuf);
    } else
    {
        result["result"] = "Unknown Mode.";
        result["expected"] = "smsglocalkeys [whitelist|all|wallet|recv <+/-> <address>|anon <+/-> <address>]";
    };

    return result;
};

json smsgscanchain(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "smsgscanchain \n"
            "Look for public keys in the block chain.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    json result;
    if (!SecureMsgScanBlockChain())
    {
        result["result"] = "Scan Chain Failed.";
    } else
    {
        result["result"] = "Scan Chain Completed.";
    }
    return result;
}

json smsgscanbuckets(const json& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw std::runtime_error(
            "smsgscanbuckets \n"
            "Force rescan of all messages in the bucket store.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    if (pwalletMain->IsLocked())
        throw std::runtime_error("Wallet is locked.");

    json result;
    if (!SecureMsgScanBuckets())
    {
        result["result"] = "Scan Buckets Failed.";
    } else
    {
        result["result"] = "Scan Buckets Completed.";
    }
    return result;
}

json smsgaddkey(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
            "smsgaddkey <address> <pubkey>\n"
            "Add address, pubkey pair to database.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    std::string addr = params[0].get<std::string>();
    std::string pubk = params[1].get<std::string>();

    json result;
    int rv = SecureMsgAddAddress(addr, pubk);
    if (rv != 0)
    {
        result["result"] = "Public key not added to db.";
        switch (rv)
        {
            case 2:     result["reason"] = "publicKey is invalid.";                  break;
            case 3:     result["reason"] = "publicKey does not match address.";      break;
            case 4:     result["reason"] = "address is already in db.";              break;
            case 5:     result["reason"] = "address is invalid.";                    break;
            default:    result["reason"] = "error.";                                 break;
        };
    } else
    {
        result["result"] = "Added public key to db.";
    };

    return result;
}

json smsggetpubkey(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
            "smsggetpubkey <address>\n"
            "Return the base58 encoded compressed public key for an address.\n"
            "Tests localkeys first, then looks in public key db.\n");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");


    std::string address   = params[0].get<std::string>();
    std::string publicKey;

    json result;
    int rv = SecureMsgGetLocalPublicKey(address, publicKey);
    switch (rv)
    {
        case 0:
            result["result"] = "Success.";
            result["address in wallet"] = address;
            result["compressed public key"] = publicKey;
            return result; // success, don't check db
        case 2:
        case 3:
            result["result"] = "Failed.";
            result["message"] = "Invalid address.";
            return result;
        case 4:
            break; // check db
        //case 1:
        default:
            result["result"] = "Failed.";
            result["message"] = "Error.";
            return result;
    };

    CBitcoinAddress coinAddress(address);


    CKeyID keyID;
    if (!coinAddress.GetKeyID(keyID))
    {
        result["result"] = "Failed.";
        result["message"] = "Invalid address.";
        return result;
    };

    CPubKey cpkFromDB;
    rv = SecureMsgGetStoredKey(keyID, cpkFromDB);

    switch (rv)
    {
        case 0:
            if (!cpkFromDB.IsValid()
                || !cpkFromDB.IsCompressed())
            {
                result["result"] = "Failed.";
                result["message"] = "Invalid address.";
            } else
            {
                //cpkFromDB.SetCompressedPubKey(); // make sure key is compressed
                publicKey = EncodeBase58(cpkFromDB.Raw());

                result["result"] = "Success.";
                result["peer address in DB"] = address;
                result["compressed public key"] = publicKey;
            };
            break;
        case 2:
            result["result"] = "Failed.";
            result["message"] = "Address not found in wallet or db.";
            return result;
        //case 1:
        default:
            result["result"] = "Failed.";
            result["message"] = "Error, GetStoredKey().";
            return result;
    };

    return result;
}

json smsgsend(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw std::runtime_error(
            "smsgsend <addrFrom> <addrTo> <message>\n"
            "Send an encrypted message from addrFrom to addrTo.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    std::string addrFrom  = params[0].get<std::string>();
    std::string addrTo    = params[1].get<std::string>();
    std::string msg       = params[2].get<std::string>();


    json result;

    std::string sError;
    if (SecureMsgSend(addrFrom, addrTo, msg, sError) != 0)
    {
        result["result"] = "Send failed.";
        result["error"] = sError;
    } else
        result["result"] = "Sent.";

    return result;
}

json smsgsendanon(const json& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
            "smsgsendanon <addrTo> <message>\n"
            "Send an anonymous encrypted message to addrTo.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    std::string addrFrom  = "anon";
    std::string addrTo    = params[0].get<std::string>();
    std::string msg       = params[1].get<std::string>();


    json result;
    std::string sError;
    if (SecureMsgSend(addrFrom, addrTo, msg, sError) != 0)
    {
        result["result"] = "Send failed.";
        result["error"] = sError;
    } else
        result["result"] = "Sent.";

    return result;
}

json smsginbox(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1) // defaults to read
        throw std::runtime_error(
            "smsginbox [all|unread|clear]\n"
            "Decrypt and display all received messages.\n"
            "Warning: clear will delete all messages.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    if (pwalletMain->IsLocked())
        throw std::runtime_error("Wallet is locked.");

    std::string mode = "unread";
    if (!params.empty())
    {
        mode = params[0].get<std::string>();
    }


    json result;

    std::vector<unsigned char> vchKey;
    vchKey.resize(16);
    memset(&vchKey[0], 0, 16);

    {
        LOCK(cs_smsgDB);

        SecMsgDB dbInbox;

        if (!dbInbox.Open("cr+"))
            throw std::runtime_error("Could not open DB.");

        uint32_t nMessages = 0;
        char cbuf[256];

        std::string sPrefix("im");
        unsigned char chKey[18];

        if (mode == "clear")
        {
            dbInbox.TxnBegin();

            std::unique_ptr<leveldb::Iterator> it(dbInbox.pdb->NewIterator(leveldb::ReadOptions()));
            while (dbInbox.NextSmesgKey(it.get(), sPrefix, chKey))
            {
                dbInbox.EraseSmesg(chKey);
                nMessages++;
            };
            it.reset();
            dbInbox.TxnCommit();

            snprintf(cbuf, sizeof(cbuf), "Deleted %u messages.", nMessages);
            result["result"] = std::string(cbuf);
        } else
        if (mode == "all"
            || mode == "unread")
        {
            int fCheckReadStatus = mode == "unread" ? 1 : 0;

            SecMsgStored smsgStored;
            MessageData msg;

            dbInbox.TxnBegin();

            json messages = json::array();
            std::unique_ptr<leveldb::Iterator> it(dbInbox.pdb->NewIterator(leveldb::ReadOptions()));
            while (dbInbox.NextSmesg(it.get(), sPrefix, chKey, smsgStored))
            {
                if (fCheckReadStatus
                    && !(smsgStored.status & SMSG_MASK_UNREAD))
                    continue;

                uint32_t nPayload = smsgStored.vchMessage.size() - SMSG_HDR_LEN;
                if (SecureMsgDecrypt(false, smsgStored.sAddrTo, &smsgStored.vchMessage[0], &smsgStored.vchMessage[SMSG_HDR_LEN], nPayload, msg) == 0)
                {
                    json objM;
                    objM["received"] = getTimeString(smsgStored.timeReceived, cbuf, sizeof(cbuf));
                    objM["sent"] = getTimeString(msg.timestamp, cbuf, sizeof(cbuf));
                    objM["from"] = msg.sFromAddress;
                    objM["to"] = smsgStored.sAddrTo;
                    objM["text"] = std::string(reinterpret_cast<char*>(&msg.vchMessage[0])); // ugh

                    messages.push_back(objM);
                } else
                {
                    messages.push_back("Could not decrypt.");
                };

                if (fCheckReadStatus)
                {
                    smsgStored.status &= ~SMSG_MASK_UNREAD;
                    dbInbox.WriteSmesg(chKey, smsgStored);
                };
                nMessages++;
            };
            it.reset();
            dbInbox.TxnCommit();

            result["messages"] = messages;

            snprintf(cbuf, sizeof(cbuf), "%u messages shown.", nMessages);
            result["result"] = std::string(cbuf);

        } else
        {
            result["result"] = "Unknown Mode.";
            result["expected"] = "[all|unread|clear].";
        };
    }

    return result;
};

json smsgoutbox(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1) // defaults to read
        throw std::runtime_error(
            "smsgoutbox [all|clear]\n"
            "Decrypt and display all sent messages.\n"
            "Warning: clear will delete all sent messages.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    if (pwalletMain->IsLocked())
        throw std::runtime_error("Wallet is locked.");

    std::string mode = "all";
    if (!params.empty())
    {
        mode = params[0].get<std::string>();
    }


    json result;

    std::string sPrefix("sm");
    unsigned char chKey[18];
    memset(&chKey[0], 0, 18);

    {
        LOCK(cs_smsgDB);

        SecMsgDB dbOutbox;

        if (!dbOutbox.Open("cr+"))
            throw std::runtime_error("Could not open DB.");

        uint32_t nMessages = 0;
        char cbuf[256];

        if (mode == "clear")
        {
            dbOutbox.TxnBegin();

            std::unique_ptr<leveldb::Iterator> it(dbOutbox.pdb->NewIterator(leveldb::ReadOptions()));
            while (dbOutbox.NextSmesgKey(it.get(), sPrefix, chKey))
            {
                dbOutbox.EraseSmesg(chKey);
                nMessages++;
            };
            it.reset();
            dbOutbox.TxnCommit();


            snprintf(cbuf, sizeof(cbuf), "Deleted %u messages.", nMessages);
            result["result"] = std::string(cbuf);
        } else
        if (mode == "all")
        {
            SecMsgStored smsgStored;
            MessageData msg;
            json messages = json::array();
            std::unique_ptr<leveldb::Iterator> it(dbOutbox.pdb->NewIterator(leveldb::ReadOptions()));
            while (dbOutbox.NextSmesg(it.get(), sPrefix, chKey, smsgStored))
            {
                uint32_t nPayload = smsgStored.vchMessage.size() - SMSG_HDR_LEN;

                if (SecureMsgDecrypt(false, smsgStored.sAddrOutbox, &smsgStored.vchMessage[0], &smsgStored.vchMessage[SMSG_HDR_LEN], nPayload, msg) == 0)
                {
                    json objM;
                    objM["sent"] = getTimeString(msg.timestamp, cbuf, sizeof(cbuf));
                    objM["from"] = msg.sFromAddress;
                    objM["to"] = smsgStored.sAddrTo;
                    objM["text"] = std::string(reinterpret_cast<char*>(&msg.vchMessage[0])); // ugh

                    messages.push_back(objM);
                } else
                {
                    messages.push_back("Could not decrypt.");
                };
                nMessages++;
            };

            result["messages"] = messages;

            snprintf(cbuf, sizeof(cbuf), "%u sent messages shown.", nMessages);
            result["result"] = std::string(cbuf);
        } else
        {
            result["result"] = "Unknown Mode.";
            result["expected"] = "[all|clear].";
        };
    }

    return result;
};


json smsgbuckets(const json& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "smsgbuckets [stats|dump]\n"
            "Display some statistics.");

    if (!fSecMsgenabled)
        throw std::runtime_error("Secure messaging is disabled.");

    std::string mode = "stats";
    if (!params.empty())
    {
        mode = params[0].get<std::string>();
    };

    json result;

    char cbuf[256];
    if (mode == "stats")
    {
        uint32_t nBuckets = 0;
        uint32_t nMessages = 0;
        uint64_t nBytes = 0;
        {
            LOCK(cs_smsg);
            std::map<int64_t, SecMsgBucket>::iterator it;
            it = smsgBuckets.begin();

            json bucketList = json::array();
            for (it = smsgBuckets.begin(); it != smsgBuckets.end(); ++it)
            {
                std::set<SecMsgToken>& tokenSet = it->second.setTokens;

                std::string sBucket = std::to_string(it->first);
                std::string sFile = sBucket + "_01.dat";

                snprintf(cbuf, sizeof(cbuf), "%" PRIszu, tokenSet.size());
                std::string snContents(cbuf);

                std::string sHash = std::to_string(it->second.hash);

                nBuckets++;
                nMessages += tokenSet.size();

                json objM;
                objM["bucket"] = sBucket;
                objM["time"] = getTimeString(it->first, cbuf, sizeof(cbuf));
                objM["no. messages"] = snContents;
                objM["hash"] = sHash;
                objM["last changed"] = getTimeString(it->second.timeChanged, cbuf, sizeof(cbuf));

                std::filesystem::path fullPath = GetDataDir() / "smsgStore" / sFile;


                if (!std::filesystem::exists(fullPath))
                {
                    // -- If there is a file for an empty bucket something is wrong.
                    if (tokenSet.empty())
                        objM["file size"] = "Empty bucket.";
                    else
                        objM["file size, error"] = "File not found.";
                } else
                {
                    try {

                        uint64_t nFBytes = 0;
                        nFBytes = std::filesystem::file_size(fullPath);
                        nBytes += nFBytes;
                        objM["file size"] = fsReadable(nFBytes);
                    } catch (const std::filesystem::filesystem_error& ex)
                    {
                        objM["file size, error"] = ex.what();
                    };
                };

                bucketList.push_back(objM);
            };

            result["buckets"] = bucketList;
        }; // LOCK(cs_smsg);


        std::string snBuckets = std::to_string(nBuckets);
        std::string snMessages = std::to_string(nMessages);

        json objM;
        objM["buckets"] = snBuckets;
        objM["messages"] = snMessages;
        objM["size"] = fsReadable(nBytes);
        result["total"] = objM;

    } else
    if (mode == "dump")
    {
        {
            LOCK(cs_smsg);
            std::map<int64_t, SecMsgBucket>::iterator it;
            it = smsgBuckets.begin();

            for (it = smsgBuckets.begin(); it != smsgBuckets.end(); ++it)
            {
                std::string sFile = std::to_string(it->first) + "_01.dat";

                try {
                    std::filesystem::path fullPath = GetDataDir() / "smsgStore" / sFile;
                    std::filesystem::remove(fullPath);
                } catch (const std::filesystem::filesystem_error& ex)
                {
                    //objM["file size, error"] = ex.what();
                    printf("Error removing bucket file %s.\n", ex.what());
                };
            };
            smsgBuckets.clear();
        }; // LOCK(cs_smsg);

        result["result"] = "Removed all buckets.";

    } else
    {
        result["result"] = "Unknown Mode.";
        result["expected"] = "[stats|dump].";
    };


    return result;
};
