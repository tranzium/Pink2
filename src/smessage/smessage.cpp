// Copyright (c) 2014 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/*
Notes:
    Running with -debug could leave to and from address hashes and public keys in the log.
    
    
    parameters:
        -debugsmsg          Show extra debug messages (fDebugSmsg)
        -smsgscanchain      Scan the block chain for public key addresses on startup
    
    
    Wallet Locked
        A copy of each incoming message is stored in bucket files ending in _wl.dat
        wl (wallet locked) bucket files are deleted if they expire, like normal buckets
        When the wallet is unlocked all the messages in wl files are scanned.
    
    
    Address Whitelist
        Owned Addresses are stored in smsgAddresses vector
        Saved to smsg.ini
        Modify options using the smsglocalkeys rpc command or edit the smsg.ini file (with client closed)
        
    
*/

#include "smessage/smessage.h"

#include <stdint.h>
#include <time.h>
#include <map>
#include <stdexcept>
#include <sstream>
#include <errno.h>

#include <openssl/crypto.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <secp256k1.h>
#include <secp256k1_ecdh.h>

#include "string_utils.h"


#include "base58.h"
#include "db.h"
#include "init.h" // pwalletMain
#include "txdb.h"


#include "lz4/lz4.c"

#include "xxhash/xxhash.h"
#include "xxhash/xxhash.c"


// On 64 bit system ld is 64bits
#ifdef IS_ARCH_64
#undef  PRId64
#undef  PRIu64
#undef  PRIx64
#define  PRId64  "ld"
#define  PRIu64  "lu"
#define  PRIx64  "lx"
#endif // IS_ARCH_64


// TODO: For buckets older than current, only need to store no. messages and hash in memory

Signal<SecMsgStored&>  NotifySecMsgInboxChanged;
Signal<SecMsgStored&> NotifySecMsgOutboxChanged;
Signal<> NotifySecMsgWalletUnlocked;

bool fSecMsgenabled = false;

std::map<int64_t, SecMsgBucket> smsgBuckets;
std::vector<SecMsgAddress>      smsgAddresses;
SecMsgOptions                   smsgOptions;



CCriticalSection cs_smsg;
CCriticalSection cs_smsgDB;

leveldb::DB *smsgDB = nullptr;


namespace fs = std::filesystem;


std::string getTimeString(int64_t timestamp, char *buffer, size_t nBuffer)
{
    struct tm* dt;
    time_t t = timestamp;
    dt = localtime(&t);
    
    strftime(buffer, nBuffer, "%Y-%m-%d %H:%M:%S %z", dt); // %Z shows long strings on windows
    return std::string(buffer); // copies the null-terminated character sequence
};

std::string fsReadable(uint64_t nBytes)
{
    char buffer[128];
    if (nBytes >= 1024ll*1024ll*1024ll*1024ll)
        snprintf(buffer, sizeof(buffer), "%.2f TB", nBytes/1024.0/1024.0/1024.0/1024.0);
    else
    if (nBytes >= 1024*1024*1024)
        snprintf(buffer, sizeof(buffer), "%.2f GB", nBytes/1024.0/1024.0/1024.0);
    else
    if (nBytes >= 1024*1024)
        snprintf(buffer, sizeof(buffer), "%.2f MB", nBytes/1024.0/1024.0);
    else
    if (nBytes >= 1024)
        snprintf(buffer, sizeof(buffer), "%.2f KB", nBytes/1024.0);
    else
        snprintf(buffer, sizeof(buffer), "%" PRIu64" bytes", nBytes);
    return std::string(buffer);
};

int SecureMsgBuildBucketSet()
{
    /*
        Build the bucket set by scanning the files in the smsgStore dir.
        
        smsgBuckets should be empty
    */
    
    if (fDebugSmsg)
        printf("SecureMsgBuildBucketSet()\n");
        
    int64_t  now            = GetAdjustedTime();
    uint32_t nFiles         = 0;
    uint32_t nMessages      = 0;
    
    fs::path pathSmsgDir = GetDataDir() / "smsgStore";
    fs::directory_iterator itend;
    
    
    if (!fs::exists(pathSmsgDir)
        || !fs::is_directory(pathSmsgDir))
    {
        printf("Message store directory does not exist.\n");
        return 0; // not an error
    }
    
    
    for (fs::directory_iterator itd(pathSmsgDir) ; itd != itend ; ++itd)
    {
        if (!fs::is_regular_file(itd->status()))
            continue;
        
        std::string fileType = (*itd).path().extension().string();
        
        if (fileType.compare(".dat") != 0)
            continue;
            
        std::string fileName = (*itd).path().filename().string();
        
        
        if (fDebugSmsg)
            printf("Processing file: %s.\n", fileName.c_str());
        
        nFiles++;
        
        // TODO files must be split if > 2GB
        // time_noFile.dat
        size_t sep = fileName.find_first_of("_");
        if (sep == std::string::npos)
            continue;
        
        std::string stime = fileName.substr(0, sep);
        
        int64_t fileTime = std::stoll(stime);
        
        if (fileTime < now - SMSG_RETENTION)
        {
            printf("Dropping file %s, expired.\n", fileName.c_str());
            try {
                fs::remove((*itd).path());
            } catch (const fs::filesystem_error& ex)
            {
                printf("Error removing bucket file %s, %s.\n", fileName.c_str(), ex.what());
            };
            continue;
        };
        
        if (strutil::ends_with(fileName, "_wl.dat"))
        {
            if (fDebugSmsg)
                printf("Skipping wallet locked file: %s.\n", fileName.c_str());
            continue;
        };
        
        
        SecureMessage smsg;
        std::set<SecMsgToken>& tokenSet = smsgBuckets[fileTime].setTokens;
        
        {
            LOCK(cs_smsg);
            FILE *fp;
            
            if (!(fp = fopen((*itd).path().string().c_str(), "rb")))
            {
                printf("Error opening file: %s\n", strerror(errno));
                continue;
            };
            
            for (;;)
            {
                long int ofs = ftell(fp);
                SecMsgToken token;
                token.offset = ofs;
                errno = 0;
                if (fread(&smsg.hash[0], sizeof(unsigned char), SMSG_HDR_LEN, fp) != static_cast<size_t>(SMSG_HDR_LEN))
                {
                    if (errno != 0)
                    {
                        printf("fread header failed: %s\n", strerror(errno));
                    } else
                    {
                        //printf("End of file.\n");
                    };
                    break;
                };
                token.timestamp = smsg.timestamp;
                
                if (smsg.nPayload < 8)
                    continue;
                
                if (fread(token.sample, sizeof(unsigned char), 8, fp) != 8)
                {
                    printf("fread data failed: %s\n", strerror(errno));
                    break;
                };
                
                if (fseek(fp, smsg.nPayload-8, SEEK_CUR) != 0)
                {
                    printf("fseek, strerror: %s.\n", strerror(errno));
                    break;
                };
                
                tokenSet.insert(token);
            };
            
            fclose(fp);
        };
        smsgBuckets[fileTime].hashBucket();
        
        nMessages += tokenSet.size();
        
        if (fDebugSmsg)
            printf("Bucket %" PRId64 " contains %" PRIszu " messages.\n", fileTime, tokenSet.size());
    };
    
    printf("Processed %u files, loaded %" PRIszu " buckets containing %u messages.\n", nFiles, smsgBuckets.size(), nMessages);
    
    return 0;
};

int SecureMsgAddWalletAddresses()
{
    if (fDebugSmsg)
        printf("SecureMsgAddWalletAddresses()\n");
    
    uint32_t nAdded = 0;
    for (const auto& entry : pwalletMain->mapAddressBook)
    {
        if (!IsMine(*pwalletMain, entry.first))
            continue;
        
        CBitcoinAddress coinAddress(entry.first);
        if (!coinAddress.IsValid())
            continue;
        
        std::string address;
        std::string strPublicKey;
        address = coinAddress.ToString();
        
        
        bool fExists        = 0;
        for (const auto& smsgAddr : smsgAddresses)
        {
            if (address != smsgAddr.sAddress)
                continue;
            fExists = 1;
            break;
        };
        
        if (fExists)
            continue;
        
        bool recvEnabled    = 1;
        bool recvAnon       = 1;
        
        smsgAddresses.push_back(SecMsgAddress(address, recvEnabled, recvAnon));
        nAdded++;
    };
    
    if (fDebugSmsg)
        printf("Added %u addresses to whitelist.\n", nAdded);
    
    return 0;
};


int SecureMsgReadIni()
{
    if (!fSecMsgenabled)
        return false;
    
    if (fDebugSmsg)
        printf("SecureMsgReadIni()\n");
    
    fs::path fullpath = GetDataDir() / "smsg.ini";
    
    
    FILE *fp;
    errno = 0;
    if (!(fp = fopen(fullpath.string().c_str(), "r")))
    {
        printf("Error opening file: %s\n", strerror(errno));
        return 1;
    };
    
    char cLine[512];
    char *pName, *pValue;
    
    char cAddress[64];
    int addrRecv, addrRecvAnon;
    
    while (fgets(cLine, 512, fp))
    {
        cLine[strcspn(cLine, "\n")] = '\0';
        cLine[strcspn(cLine, "\r")] = '\0';
        cLine[511] = '\0'; // for safety
        
        // -- check that line contains a name value pair and is not a comment, or section header
        if (cLine[0] == '#' || cLine[0] == '[' || strcspn(cLine, "=") < 1)
            continue;
        
        if (!(pName = strtok(cLine, "="))
            || !(pValue = strtok(nullptr, "=")))
            continue;
        
        if (strcmp(pName, "newAddressRecv") == 0)
        {
            smsgOptions.fNewAddressRecv = (strcmp(pValue, "true") == 0) ? true : false;
        } else
        if (strcmp(pName, "newAddressAnon") == 0)
        {
            smsgOptions.fNewAddressAnon = (strcmp(pValue, "true") == 0) ? true : false;
        } else
        if (strcmp(pName, "key") == 0)
        {
            int rv = sscanf(pValue, "%64[^|]|%d|%d", cAddress, &addrRecv, &addrRecvAnon);
            if (rv == 3)
            {
                smsgAddresses.push_back(SecMsgAddress(std::string(cAddress), addrRecv, addrRecvAnon));
            } else
            {
                printf("Could not parse key line %s, rv %d.\n", pValue, rv);
            }
        } else
        {
            printf("Unknown setting name: '%s'.", pName);
        };
    };
    
    printf("Loaded %" PRIszu " addresses.\n", smsgAddresses.size());
    
    fclose(fp);
    
    return 0;
};

int SecureMsgWriteIni()
{
    if (!fSecMsgenabled)
        return false;
    
    if (fDebugSmsg)
        printf("SecureMsgWriteIni()\n");
    
    fs::path fullpath = GetDataDir() / "smsg.ini~";
    
    FILE *fp;
    errno = 0;
    if (!(fp = fopen(fullpath.string().c_str(), "w")))
    {
        printf("Error opening file: %s\n", strerror(errno));
        return 1;
    };
    
    if (fwrite("[Options]\n", sizeof(char), 10, fp) != 10)
    {
        printf("fwrite error: %s\n", strerror(errno));
        fclose(fp);
        return false;
    };
    
    if (fprintf(fp, "newAddressRecv=%s\n", smsgOptions.fNewAddressRecv ? "true" : "false") < 0
        || fprintf(fp, "newAddressAnon=%s\n", smsgOptions.fNewAddressAnon ? "true" : "false") < 0)
    {
        printf("fprintf error: %s\n", strerror(errno));
        fclose(fp);
        return false;
    }
    
    if (fwrite("\n[Keys]\n", sizeof(char), 8, fp) != 8)
    {
        printf("fwrite error: %s\n", strerror(errno));
        fclose(fp);
        return false;
    };
    for (const auto& smsgAddr : smsgAddresses)
    {
        errno = 0;
        if (fprintf(fp, "key=%s|%d|%d\n", smsgAddr.sAddress.c_str(), smsgAddr.fReceiveEnabled, smsgAddr.fReceiveAnon) < 0)
        {
            printf("fprintf error: %s\n", strerror(errno));
            continue;
        };
    };
    
    
    fclose(fp);
    
    
    try {
        fs::path finalpath = GetDataDir() / "smsg.ini";
        fs::rename(fullpath, finalpath);
    } catch (const fs::filesystem_error& ex)
    {
        printf("Error renaming file %s, %s.\n", fullpath.string().c_str(), ex.what());
    };
    return 0;
};


/** called from AppInit2() in init.cpp */
bool SecureMsgStart(bool fScanChain)
{
    fSecMsgenabled = true;
    
    if (SecureMsgReadIni() != 0)
        printf("Failed to read smsg.ini\n");
    
    if (smsgAddresses.size() < 1)
    {
        printf("No address keys loaded.\n");
        if (SecureMsgAddWalletAddresses() != 0)
            printf("Failed to load addresses from wallet.\n");
    };
    
    if (fScanChain)
    {
        SecureMsgScanBlockChain();
    };
    
    if (SecureMsgBuildBucketSet() != 0)
    {
        printf("SecureMsg could not load bucket sets, secure messaging disabled.\n");
        fSecMsgenabled = false;
        return false;
    };
    
    // -- start threads
    if (!NewThread(ThreadSecureMsg)
        || !NewThread(ThreadSecureMsgPow))
    {
        printf("SecureMsg could not start threads, secure messaging disabled.\n");
        fSecMsgenabled = false;
        return false;
    };
    
    return true;
};

/** called from Shutdown() in init.cpp */
bool SecureMsgShutdown()
{
    if (!fSecMsgenabled)
        return false;
    
    printf("Stopping secure messaging.\n");
    
    
    if (SecureMsgWriteIni() != 0)
        printf("Failed to save smsg.ini\n");
    
    fSecMsgenabled = false;
    
    if (smsgDB)
    {
        LOCK(cs_smsgDB);
        delete smsgDB;
        smsgDB = nullptr;
    };
    
    // -- main program will wait 5 seconds for threads to terminate.
    
    return true;
};

bool SecureMsgEnable()
{
    // -- start secure messaging at runtime
    if (fSecMsgenabled)
    {
        printf("SecureMsgenable: secure messaging is already enabled.\n");
        return false;
    };
    
    {
        LOCK(cs_smsg);
        fSecMsgenabled = true;
        
        smsgAddresses.clear(); // should be empty already
        if (SecureMsgReadIni() != 0)
            printf("Failed to read smsg.ini\n");
        
        if (smsgAddresses.size() < 1)
        {
            printf("No address keys loaded.\n");
            if (SecureMsgAddWalletAddresses() != 0)
                printf("Failed to load addresses from wallet.\n");
        };
        
        smsgBuckets.clear(); // should be empty already
        
        if (SecureMsgBuildBucketSet() != 0)
        {
            printf("SecureMsgenable: could not load bucket sets, secure messaging disabled.\n");
            fSecMsgenabled = false;
            return false;
        };
        
    }; // LOCK(cs_smsg);
    
    // -- start threads
    if (!NewThread(ThreadSecureMsg)
        || !NewThread(ThreadSecureMsgPow))
    {
        printf("SecureMsgenable could not start threads, secure messaging disabled.\n");
        fSecMsgenabled = false;
        return false;
    };
    
    // -- ping each peer, don't know which have messaging enabled
    {
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
        {
            pnode->PushMessage("smsgPing");
            pnode->PushMessage("smsgPong"); // Send pong as have missed initial ping sent by peer when it connected
        };
    }
    
    printf("Secure messaging enabled.\n");
    return true;
};

bool SecureMsgDisable()
{
    // -- stop secure messaging at runtime
    if (!fSecMsgenabled)
    {
        printf("SecureMsgDisable: secure messaging is already disabled.\n");
        return false;
    };
    
    {
        LOCK(cs_smsg);
        fSecMsgenabled = false;
        
        // -- clear smsgBuckets
        for (auto& entry : smsgBuckets)
        {
            entry.second.setTokens.clear();
        };
        smsgBuckets.clear();
        
        // -- tell each smsg enabled peer that this node is disabling
        {
            LOCK(cs_vNodes);
            for (CNode* pnode : vNodes)
            {
                if (!pnode->smsgData.fEnabled)
                    continue;
                
                pnode->PushMessage("smsgDisabled");
                pnode->smsgData.fEnabled = false;
            };
        }
    
        if (SecureMsgWriteIni() != 0)
            printf("Failed to save smsg.ini\n");
        
        smsgAddresses.clear();
        
    }; // LOCK(cs_smsg);
    
    // -- allow time for threads to stop
    MilliSleep(3000); // milliseconds
    // TODO be certain that threads have stopped
    
    if (smsgDB)
    {
        LOCK(cs_smsgDB);
        delete smsgDB;
        smsgDB = nullptr;
    };
    
    
    printf("Secure messaging disabled.\n");
    return true;
};



static int SecureMsgInsertAddress(CKeyID& hashKey, CPubKey& pubKey, SecMsgDB& addrpkdb)
{
    /* insert key hash and public key to addressdb
        
        should have LOCK(cs_smsg) where db is opened
        
        returns
            0 success
            1 error
            4 address is already in db
    */
    
    
    if (addrpkdb.ExistsPK(hashKey))
    {
        //printf("DB already contains public key for address.\n");
        CPubKey cpkCheck;
        if (!addrpkdb.ReadPK(hashKey, cpkCheck))
        {
            printf("addrpkdb.Read failed.\n");
        } else
        {
            if (cpkCheck != pubKey)
                printf("DB already contains existing public key that does not match .\n");
        };
        return 4;
    };
    
    if (!addrpkdb.WritePK(hashKey, pubKey))
    {
        printf("Write pair failed.\n");
        return 1;
    };
    
    return 0;
};

int SecureMsgInsertAddress(CKeyID& hashKey, CPubKey& pubKey)
{
    int rv;
    {
        LOCK(cs_smsgDB);
        SecMsgDB addrpkdb;
        
        if (!addrpkdb.Open("cr+"))
            return 1;
        
        rv = SecureMsgInsertAddress(hashKey, pubKey, addrpkdb);
    }
    return rv;
};


static bool ScanBlock(CBlock& block, CTxDB& txdb, SecMsgDB& addrpkdb,
    uint32_t& nTransactions, uint32_t& nInputs, uint32_t& nPubkeys, uint32_t& nDuplicates)
{
    // -- should have LOCK(cs_smsg) where db is opened
    for (CTransaction& tx : block.vtx)
    {
        if (!tx.IsStandard())
            continue; // leave out coinbase and others
        
        /*
        Look at the inputs of every tx.
        If the inputs are standard, get the pubkey from scriptsig and
        look for the corresponding output (the input(output of other tx) to the input of this tx)
        get the address from scriptPubKey
        add to db if address is unique.
        
        Would make more sense to do this the other way around, get address first for early out.
        
        */
        
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            CScript *script = &tx.vin[i].scriptSig;
            
            opcodetype opcode;
            valtype vch;
            auto pc = script->begin();
            auto pend = script->end();
            
            uint256 prevoutHash;
            CKey key;
            
            // -- matching address is in scriptPubKey of previous tx output
            while (pc < pend)
            {
                if (!script->GetOp(pc, opcode, vch))
                    break;
                // -- opcode is the length of the following data, compressed public key is always 33
                if (opcode == 33)
                {
                    if (!key.SetPubKey(vch))
                    {
                        printf("Bad Public Key.\n");
                        continue;
                    }
                    
                    key.SetCompressedPubKey(); // ensure key is compressed
                    CPubKey pubKey = key.GetPubKey();
                    
                    if (!pubKey.IsValid()
                        || !pubKey.IsCompressed())
                    {
                        printf("Public key is invalid %s.\n", ValueString(pubKey.Raw()).c_str());
                        continue;
                    };
                    
                    prevoutHash = tx.vin[i].prevout.hash;
                    CTransaction txOfPrevOutput;
                    if (!txdb.ReadDiskTx(prevoutHash, txOfPrevOutput))
                    {
                        printf("Could not get transaction for hash: %s.\n", prevoutHash.ToString().c_str());
                        continue;
                    };
                    
                    unsigned int nOut = tx.vin[i].prevout.n;
                    if (nOut >= txOfPrevOutput.vout.size())
                    {
                        printf("Output %u, not in transaction: %s.\n", nOut, prevoutHash.ToString().c_str());
                        continue;
                    };
                    
                    CTxOut *txOut = &txOfPrevOutput.vout[nOut];
                    
                    CTxDestination addressRet;
                    if (!ExtractDestination(txOut->scriptPubKey, addressRet))
                    {
                        printf("ExtractDestination failed: %s.\n", prevoutHash.ToString().c_str());
                        break;
                    };
                    
                    
                    CBitcoinAddress coinAddress(addressRet);
                    CKeyID hashKey;
                    if (!coinAddress.GetKeyID(hashKey))
                    {
                        printf("coinAddress.GetKeyID failed: %s.\n", coinAddress.ToString().c_str());
                        break;
                    };
                    
                    int rv = SecureMsgInsertAddress(hashKey, pubKey, addrpkdb);
                    if (rv != 0)
                    {
                        if (rv == 4)
                            nDuplicates++;
                        break;
                    };
                    nPubkeys++;
                    break;
                };
                
                //printf("opcode %d, %s, value %s.\n", opcode, GetOpName(opcode), ValueString(vch).c_str());
            };
            nInputs++;
        };
        nTransactions++;
        
        if (nTransactions % 10000 == 0) // for ScanChainForPublicKeys
        {
            printf("Scanning transaction no. %u.\n", nTransactions);
        };
    };
    return true;
};


bool SecureMsgScanBlock(CBlock& block)
{
    /*
    scan block for public key addresses
    called from ProcessMessage() in main where strCommand == "block"
    */
    
    if (fDebugSmsg)
        printf("SecureMsgScanBlock().\n");
    
    uint32_t nTransactions  = 0;
    uint32_t nInputs        = 0;
    uint32_t nPubkeys       = 0;
    uint32_t nDuplicates    = 0;
    
    {
        LOCK(cs_smsgDB);
        CTxDB txdb("r");
        
        SecMsgDB addrpkdb;
        if (!addrpkdb.Open("cw")
            || !addrpkdb.TxnBegin())
            return false;
        
        ScanBlock(block, txdb, addrpkdb,
            nTransactions, nInputs, nPubkeys, nDuplicates);
        
        addrpkdb.TxnCommit();
    }
    
    if (fDebugSmsg)
        printf("Found %u transactions, %u inputs, %u new public keys, %u duplicates.\n", nTransactions, nInputs, nPubkeys, nDuplicates);
    
    return true;
};

bool ScanChainForPublicKeys(CBlockIndex* pindexStart)
{
    printf("Scanning block chain for public keys.\n");
    int64_t nStart = GetTimeMillis();
    
    if (fDebugSmsg)
        printf("From height %u.\n", pindexStart->nHeight);
    
    // -- public keys are in txin.scriptSig
    //    matching addresses are in scriptPubKey of txin's referenced output
    
    uint32_t nBlocks        = 0;
    uint32_t nTransactions  = 0;
    uint32_t nInputs        = 0;
    uint32_t nPubkeys       = 0;
    uint32_t nDuplicates    = 0;
    
    {
        LOCK(cs_smsgDB);
    
        CTxDB txdb("r");
        
        SecMsgDB addrpkdb;
        if (!addrpkdb.Open("cw")
            || !addrpkdb.TxnBegin())
            return false;
        
        CBlockIndex* pindex = pindexStart;
        while (pindex)
        {
            nBlocks++;
            CBlock block;
            block.ReadFromDisk(pindex, true);
            
            ScanBlock(block, txdb, addrpkdb,
                nTransactions, nInputs, nPubkeys, nDuplicates);
            
            pindex = pindex->pnext;
        };
        
        addrpkdb.TxnCommit();
    };
    
    printf("Scanned %u blocks, %u transactions, %u inputs\n", nBlocks, nTransactions, nInputs);
    printf("Found %u public keys, %u duplicates.\n", nPubkeys, nDuplicates);
    printf("Took %" PRId64 " ms\n", GetTimeMillis() - nStart);
    
    return true;
};

bool SecureMsgScanBlockChain()
{
    TRY_LOCK(cs_main, lockMain);
    if (lockMain)
    {
        CBlockIndex *pindexScan = pindexGenesisBlock;
        if (pindexScan == nullptr)
        {
            printf("Error: pindexGenesisBlock not set.\n");
            return false;
        };
        
        
        try { // -- in try to catch errors opening db, 
            if (!ScanChainForPublicKeys(pindexScan))
                return false;
        } catch (std::exception& e)
        {
            printf("ScanChainForPublicKeys() threw: %s.\n", e.what());
            return false;
        };
    } else
    {
        printf("ScanChainForPublicKeys() Could not lock main.\n");
        return false;
    };
    
    return true;
};

bool SecureMsgScanBuckets()
{
    if (fDebugSmsg)
        printf("SecureMsgScanBuckets()\n");
    
    if (!fSecMsgenabled
        || pwalletMain->IsLocked())
        return false;
    
    int64_t  mStart         = GetTimeMillis();
    int64_t  now            = GetAdjustedTime();
    uint32_t nFiles         = 0;
    uint32_t nMessages      = 0;
    uint32_t nFoundMessages = 0;
    
    fs::path pathSmsgDir = GetDataDir() / "smsgStore";
    fs::directory_iterator itend;
    
    if (!fs::exists(pathSmsgDir)
        || !fs::is_directory(pathSmsgDir))
    {
        printf("Message store directory does not exist.\n");
        return 0; // not an error
    };
    
    SecureMessage smsg;
    std::vector<unsigned char> vchData;
    
    for (fs::directory_iterator itd(pathSmsgDir) ; itd != itend ; ++itd)
    {
        if (!fs::is_regular_file(itd->status()))
            continue;
        
        std::string fileType = (*itd).path().extension().string();
        
        if (fileType.compare(".dat") != 0)
            continue;
            
        std::string fileName = (*itd).path().filename().string();
        
        
        if (fDebugSmsg)
            printf("Processing file: %s.\n", fileName.c_str());
        
        nFiles++;
        
        // TODO files must be split if > 2GB
        // time_noFile.dat
        size_t sep = fileName.find_first_of("_");
        if (sep == std::string::npos)
            continue;
        
        std::string stime = fileName.substr(0, sep);
        
        int64_t fileTime = std::stoll(stime);
        
        if (fileTime < now - SMSG_RETENTION)
        {
            printf("Dropping file %s, expired.\n", fileName.c_str());
            try {
                fs::remove((*itd).path());
            } catch (const fs::filesystem_error& ex)
            {
                printf("Error removing bucket file %s, %s.\n", fileName.c_str(), ex.what());
            };
            continue;
        };
        
        if (strutil::ends_with(fileName, "_wl.dat"))
        {
            if (fDebugSmsg)
                printf("Skipping wallet locked file: %s.\n", fileName.c_str());
            continue;
        };
        
        {
            LOCK(cs_smsg);
            FILE *fp;
            errno = 0;
            if (!(fp = fopen((*itd).path().string().c_str(), "rb")))
            {
                printf("Error opening file: %s\n", strerror(errno));
                continue;
            };
            
            for (;;)
            {
                errno = 0;
                if (fread(&smsg.hash[0], sizeof(unsigned char), SMSG_HDR_LEN, fp) != static_cast<size_t>(SMSG_HDR_LEN))
                {
                    if (errno != 0)
                    {
                        printf("fread header failed: %s\n", strerror(errno));
                    } else
                    {
                        //printf("End of file.\n");
                    };
                    break;
                };
                
                try {
                    vchData.resize(smsg.nPayload);
                } catch (std::exception& e)
                {
                    printf("SecureMsgWalletUnlocked(): Could not resize vchData, %u, %s\n", smsg.nPayload, e.what());
                    fclose(fp);
                    return 1;
                };
                
                if (fread(&vchData[0], sizeof(unsigned char), smsg.nPayload, fp) != smsg.nPayload)
                {
                    printf("fread data failed: %s\n", strerror(errno));
                    break;
                };
                
                // -- don't report to gui, 
                int rv = SecureMsgScanMessage(&smsg.hash[0], &vchData[0], smsg.nPayload, false);
                
                if (rv == 0)
                {
                    nFoundMessages++;
                } else
                if (rv != 0)
                {
                    // SecureMsgScanMessage failed
                };
                
                nMessages ++;
            };
            
            fclose(fp);
            
            // -- remove wl file when scanned
            try {
                fs::remove((*itd).path());
            } catch (const std::filesystem::filesystem_error& ex)
            {
                printf("Error removing wl file %s - %s\n", fileName.c_str(), ex.what());
                return 1;
            };
        };
    };
    
    printf("Processed %u files, scanned %u messages, received %u messages.\n", nFiles, nMessages, nFoundMessages);
    printf("Took %" PRId64 " ms\n", GetTimeMillis() - mStart);
    
    return true;
}


int SecureMsgWalletUnlocked()
{
    /*
    When the wallet is unlocked scan messages received while wallet was locked.
    */
    if (!fSecMsgenabled)
        return 0;
    
    
    printf("SecureMsgWalletUnlocked()\n");
    
    if (pwalletMain->IsLocked())
    {
        printf("Error: Wallet is locked.\n");
        return 1;
    };
    
    int64_t  now            = GetAdjustedTime();
    uint32_t nFiles         = 0;
    uint32_t nMessages      = 0;
    uint32_t nFoundMessages = 0;
    
    fs::path pathSmsgDir = GetDataDir() / "smsgStore";
    fs::directory_iterator itend;
    
    if (!fs::exists(pathSmsgDir)
        || !fs::is_directory(pathSmsgDir))
    {
        printf("Message store directory does not exist.\n");
        return 0; // not an error
    };
    
    SecureMessage smsg;
    std::vector<unsigned char> vchData;
    
    for (fs::directory_iterator itd(pathSmsgDir) ; itd != itend ; ++itd)
    {
        if (!fs::is_regular_file(itd->status()))
            continue;
        
        std::string fileName = (*itd).path().filename().string();
        
        if (!strutil::ends_with(fileName, "_wl.dat"))
            continue;
        
        if (fDebugSmsg)
            printf("Processing file: %s.\n", fileName.c_str());
        
        nFiles++;
        
        // TODO files must be split if > 2GB
        // time_noFile_wl.dat
        size_t sep = fileName.find_first_of("_");
        if (sep == std::string::npos)
            continue;
        
        std::string stime = fileName.substr(0, sep);
        
        int64_t fileTime = std::stoll(stime);
        
        if (fileTime < now - SMSG_RETENTION)
        {
            printf("Dropping wallet locked file %s, expired.\n", fileName.c_str());
            try {
                fs::remove((*itd).path());
            } catch (const std::filesystem::filesystem_error& ex)
            {
                printf("Error removing wl file %s - %s\n", fileName.c_str(), ex.what());
                return 1;
            };
            continue;
        };
        
        {
            LOCK(cs_smsg);
            FILE *fp;
            errno = 0;
            if (!(fp = fopen((*itd).path().string().c_str(), "rb")))
            {
                printf("Error opening file: %s\n", strerror(errno));
                continue;
            };
            
            for (;;)
            {
                errno = 0;
                if (fread(&smsg.hash[0], sizeof(unsigned char), SMSG_HDR_LEN, fp) != static_cast<size_t>(SMSG_HDR_LEN))
                {
                    if (errno != 0)
                    {
                        printf("fread header failed: %s\n", strerror(errno));
                    } else
                    {
                        //printf("End of file.\n");
                    };
                    break;
                };
                
                try {
                    vchData.resize(smsg.nPayload);
                } catch (std::exception& e)
                {
                    printf("SecureMsgWalletUnlocked(): Could not resize vchData, %u, %s\n", smsg.nPayload, e.what());
                    fclose(fp);
                    return 1;
                };
                
                if (fread(&vchData[0], sizeof(unsigned char), smsg.nPayload, fp) != smsg.nPayload)
                {
                    printf("fread data failed: %s\n", strerror(errno));
                    break;
                };
                
                // -- don't report to gui, 
                int rv = SecureMsgScanMessage(&smsg.hash[0], &vchData[0], smsg.nPayload, false);
                
                if (rv == 0)
                {
                    nFoundMessages++;
                } else
                if (rv != 0)
                {
                    // SecureMsgScanMessage failed
                };
                
                nMessages ++;
            };
            
            fclose(fp);
            
            // -- remove wl file when scanned
            try {
                fs::remove((*itd).path());
            } catch (const std::filesystem::filesystem_error& ex)
            {
                printf("Error removing wl file %s - %s\n", fileName.c_str(), ex.what());
                return 1;
            };
        };
    };
    
    printf("Processed %u files, scanned %u messages, received %u messages.\n", nFiles, nMessages, nFoundMessages);
    
    // -- notify gui
    NotifySecMsgWalletUnlocked();
    
    return 0;
};

int SecureMsgWalletKeyChanged(std::string sAddress, std::string sLabel, ChangeType mode)
{
    if (!fSecMsgenabled)
        return 0;
    
    printf("SecureMsgWalletKeyChanged()\n");
    
    // TODO: default recv and recvAnon
    
    {
        LOCK(cs_smsg);
        
        switch(mode)
        {
            case CT_NEW:
                smsgAddresses.push_back(SecMsgAddress(sAddress, smsgOptions.fNewAddressRecv, smsgOptions.fNewAddressAnon));
                break;
            case CT_DELETED:
                for (std::vector<SecMsgAddress>::iterator it = smsgAddresses.begin(); it != smsgAddresses.end(); ++it)
                {
                    if (sAddress != it->sAddress)
                        continue;
                    smsgAddresses.erase(it);
                    break;
                };
                break;
            default:
                break;
        }
        
    }; // LOCK(cs_smsg);
    
    
    return 0;
};

int SecureMsgGetLocalKey(CKeyID& ckid, CPubKey& cpkOut)
{
    if (fDebugSmsg)
        printf("SecureMsgGetLocalKey()\n");
    
    CKey key;
    if (!pwalletMain->GetKey(ckid, key))
        return 4;
    
    key.SetCompressedPubKey(); // make sure key is compressed
    
    cpkOut = key.GetPubKey();
    if (!cpkOut.IsValid()
        || !cpkOut.IsCompressed())
    {
        printf("Public key is invalid %s.\n", ValueString(cpkOut.Raw()).c_str());
        return 1;
    };
    
    return 0;
};

int SecureMsgGetLocalPublicKey(std::string& strAddress, std::string& strPublicKey)
{
    /* returns
        0 success,
        1 error
        2 invalid address
        3 address does not refer to a key
        4 address not in wallet
    */
    
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        return 2; // Invalid coin address
    
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        return 3;
    
    int rv;
    CPubKey pubKey;
    if ((rv = SecureMsgGetLocalKey(keyID, pubKey)) != 0)
        return rv;
    
    strPublicKey = EncodeBase58(pubKey.Raw());
    
    return 0;
};

int SecureMsgGetStoredKey(CKeyID& ckid, CPubKey& cpkOut)
{
    /* returns
        0 success,
        1 error
        2 public key not in database
    */
    if (fDebugSmsg)
        printf("SecureMsgGetStoredKey().\n");
    
    {
        LOCK(cs_smsgDB);
        SecMsgDB addrpkdb;
        
        if (!addrpkdb.Open("r"))
            return 1;
        
        if (!addrpkdb.ReadPK(ckid, cpkOut))
        {
            //printf("addrpkdb.Read failed: %s.\n", coinAddress.ToString().c_str());
            return 2;
        };
    }
    
    return 0;
};

int SecureMsgAddAddress(std::string& address, std::string& publicKey)
{
    /*
        Add address and matching public key to the database
        address and publicKey are in base58
        
        returns
            0 success
            1 error
            2 publicKey is invalid
            3 publicKey != address
            4 address is already in db
            5 address is invalid
    */
    
    CBitcoinAddress coinAddress(address);
    
    if (!coinAddress.IsValid())
    {
        printf("Address is not valid: %s.\n", address.c_str());
        return 5;
    };
    
    CKeyID hashKey;
    
    if (!coinAddress.GetKeyID(hashKey))
    {
        printf("coinAddress.GetKeyID failed: %s.\n", coinAddress.ToString().c_str());
        return 5;
    };
    
    std::vector<unsigned char> vchTest;
    DecodeBase58(publicKey, vchTest);
    CPubKey pubKey(vchTest);
    
    // -- check that public key matches address hash
    CKey keyT;
    if (!keyT.SetPubKey(pubKey))
    {
        printf("SetPubKey failed.\n");
        return 2;
    };
    
    keyT.SetCompressedPubKey();
    CPubKey pubKeyT = keyT.GetPubKey();
    
    CBitcoinAddress addressT(address);
    
    if (addressT.ToString().compare(address) != 0)
    {
        printf("Public key does not hash to address, addressT %s.\n", addressT.ToString().c_str());
        return 3;
    };
    
    return SecureMsgInsertAddress(hashKey, pubKey);
};

