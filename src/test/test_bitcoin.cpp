#define BOOST_TEST_MODULE Bitcoin Test Suite
#include <boost/test/unit_test.hpp>

#include "db.h"
#include "main.h"
#include "wallet.h"
#include "checkpoints.h"

#include <filesystem>
#include <memory>

std::unique_ptr<CWallet> pwalletMain;
CClientUIInterface uiInterface;

// Global variables that would normally come from init.cpp
bool fConfChange;
bool fNTPSuccess;
unsigned int nNodeLifespan;
unsigned int nDerivationMethodIndex;
bool fUseFastIndex;
enum Checkpoints::CPMode CheckpointsMode;
std::unique_ptr<CWallet> pstakeDB;

extern bool fPrintToConsole;
extern void noui_connect();

struct TestingSetup {
    TestingSetup() {
        fPrintToDebugger = true; // don't want to write to debug.log file
        noui_connect();

        // Remove stale block data from previous test runs.
        // LevelDB (txleveldb/) and block files (blk*.dat) persist on disk
        // at GetDataDir() and would corrupt integration test chain state.
        {
            std::filesystem::path dataDir = GetDataDir();
            std::filesystem::remove_all(dataDir / "txleveldb");
            for (unsigned int nFile = 1; ; ++nFile) {
                auto blkPath = dataDir / strprintf("blk%04u.dat", nFile);
                if (!std::filesystem::exists(blkPath)) break;
                std::filesystem::remove(blkPath);
            }
        }

        bitdb.MakeMock();
        LoadBlockIndex(true);
        bool fFirstRun;
        pwalletMain = std::make_unique<CWallet>("wallet.dat");
        pwalletMain->LoadWallet(fFirstRun);
        RegisterWallet(pwalletMain.get());

        // Initialize pstakeDB for staking tests (file-backed for RPC tests)
        pstakeDB = std::make_unique<CWallet>("stake.dat");
    }
    ~TestingSetup()
    {
        pstakeDB.reset();
        pwalletMain.reset();
        bitdb.Flush(true);
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);

void Shutdown(void* parg)
{
  exit(0);
}

void StartShutdown()
{
  exit(0);
}

