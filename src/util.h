// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#include "uint256.h"

#ifndef WIN32
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#endif

#include <algorithm>
#include <map>
#include <list>
#include <utility>
#include <vector>
#include <string>

#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>

#include "openssl_ptr.h"

#include "netbase.h" // for AddTimeData

// to obtain PRId64 on some old systems
#define __STDC_FORMAT_MACROS 1

#include <stdint.h>
#include <inttypes.h>

static const int64_t COIN = 100000000;
static const int64_t CENT = 1000000;

#define BEGIN(a)            (const_cast<char*>(reinterpret_cast<const char*>(&(a))))
#define END(a)              (const_cast<char*>(reinterpret_cast<const char*>(&((&(a))[1]))))
#define UBEGIN(a)           (const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(&(a))))
#define UEND(a)             (const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(&((&(a))[1]))))
#define ARRAYLEN(array)     (sizeof(array)/sizeof((array)[0]))

#define UVOIDBEGIN(a)        (reinterpret_cast<void*>(&(a)))
#define CVOIDBEGIN(a)        (reinterpret_cast<const void*>(&(a)))
#define UINTBEGIN(a)        (reinterpret_cast<uint32_t*>(&(a)))
#define CUINTBEGIN(a)        (reinterpret_cast<const uint32_t*>(&(a)))

/* Format characters for (s)size_t and ptrdiff_t */
#if defined(_MSC_VER) || defined(__MSVCRT__)
  /* (s)size_t and ptrdiff_t have the same size specifier in MSVC:
     http://msdn.microsoft.com/en-us/library/tcxf1dw6%28v=vs.100%29.aspx
   */
  #define PRIszu    "Iu"
#else /* C99 standard */
  #define PRIszu    "zu"
#endif

// This is needed because the foreach macro can't get over the comma in pair<t1, t2>
#define PAIRTYPE(t1, t2)    std::pair<t1, t2>

// Align by increasing pointer, must have extra space at end of buffer
template <size_t nBytes, typename T>
T* alignup(T* p)
{
    union
    {
        T* ptr;
        size_t n;
    } u;
    u.ptr = p;
    u.n = (u.n + (nBytes-1)) & ~(nBytes-1);
    return u.ptr;
}

#ifdef WIN32
#define MSG_NOSIGNAL        0
#define MSG_DONTWAIT        0

#ifndef S_IRUSR
#define S_IRUSR             0400
#define S_IWUSR             0200
#endif
#else
#define MAX_PATH            1024
#endif

inline void MilliSleep(int64_t n)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(n));
}

/* This GNU C extension enables the compiler to check the format string against the parameters provided.
 * X is the number of the "format string" parameter, and Y is the number of the first variadic parameter.
 * Parameters count from 1.
 */
#ifdef __GNUC__
#define ATTR_WARN_PRINTF(X,Y) __attribute__((format(printf,X,Y)))
#else
#define ATTR_WARN_PRINTF(X,Y)
#endif

#if defined(_MSC_VER) || defined(__MSVCRT__)
#undef PRId64
#define PRId64 "I64d"
#undef PRIu64
#define PRIu64 "I64u"
#undef PRIx64
#define PRIx64 "I64x"
#endif

extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;
extern bool fDebug;
extern bool fDebugNet;
extern bool fDebugSmsg;
extern bool fSmsg;
extern bool fPrintToConsole;
extern bool fPrintToDebugger;
extern bool fRequestShutdown;
extern bool fShutdown;
extern bool fDaemon;
extern bool fServer;
extern bool fCommandLine;
extern std::string strMiscWarning;
extern bool fTestNet;
extern bool fNoListen;
extern bool fLogTimestamps;
extern bool fReopenDebugLog;

void RandAddSeed();
void RandAddSeedPerfmon();
int ATTR_WARN_PRINTF(1,2) OutputDebugStringF(const char* pszFormat, ...);

/*
  Rationale for the real_strprintf / strprintf construction:
    It is not allowed to use va_start with a pass-by-reference argument.
    (C++ standard, 18.7, paragraph 3). Use a dummy argument to work around this, and use a
    macro to keep similar semantics.
*/

/** Overload strprintf for char*, so that GCC format type warnings can be given */
std::string ATTR_WARN_PRINTF(1,3) real_strprintf(const char *format, int dummy, ...);
/** Overload strprintf for std::string, to be able to use it with _ (translation).
 * This will not support GCC format type warnings (-Wformat) so be careful.
 */
std::string real_strprintf(const std::string &format, int dummy, ...);
#define strprintf(format, ...) real_strprintf(format, 0, __VA_ARGS__)
std::string vstrprintf(const char *format, va_list ap);

bool ATTR_WARN_PRINTF(1,2) error(const char *format, ...);

// Structured logging — include after strprintf so LogPrintf/LogPrint macros work
#include "logging.h"

/* Redefine printf so that it directs output to debug.log
 *
 * Do this *after* defining the other printf-like functions, because otherwise the
 * __attribute__((format(printf,X,Y))) gets expanded to __attribute__((format(OutputDebugStringF,X,Y)))
 * which confuses gcc.
 */
#define printf OutputDebugStringF

void PrintException(std::exception* pex, const char* pszThread);
void PrintExceptionContinue(std::exception* pex, const char* pszThread);
void ParseString(const std::string& str, char c, std::vector<std::string>& v);
std::string FormatMoney(int64_t n, bool fPlus=false);
bool ParseMoney(const std::string& str, int64_t& nRet);
bool ParseMoney(const char* pszIn, int64_t& nRet);
std::vector<unsigned char> ParseHex(const char* psz);
std::vector<unsigned char> ParseHex(const std::string& str);
bool IsHex(const std::string& str);
std::vector<unsigned char> DecodeBase64(const char* p, bool* pfInvalid = nullptr);
std::string DecodeBase64(const std::string& str);
std::string EncodeBase64(const unsigned char* pch, size_t len);
std::string EncodeBase64(const std::string& str);
std::vector<unsigned char> DecodeBase32(const char* p, bool* pfInvalid = nullptr);
std::string DecodeBase32(const std::string& str);
std::string EncodeBase32(const unsigned char* pch, size_t len);
std::string EncodeBase32(const std::string& str);
void ParseParameters(int argc, const char*const argv[]);
bool WildcardMatch(const char* psz, const char* mask);
bool WildcardMatch(const std::string& str, const std::string& mask);
void FileCommit(FILE *fileout);
bool RenameOver(std::filesystem::path src, std::filesystem::path dest);
std::filesystem::path GetDefaultDataDir();
const std::filesystem::path &GetDataDir(bool fNetSpecific = true);
std::filesystem::path GetConfigFile();
std::filesystem::path GetPidFile();
#ifndef WIN32
void CreatePidFile(const std::filesystem::path &path, pid_t pid);
#endif
void ReadConfigFile(std::map<std::string, std::string>& mapSettingsRet, std::map<std::string, std::vector<std::string> >& mapMultiSettingsRet);
#ifdef WIN32
std::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif
void ShrinkDebugFile();
int GetRandInt(int nMax);
uint64_t GetRand(uint64_t nMax);
uint256 GetRandHash();
int64_t GetTime();
void SetMockTime(int64_t nMockTimeIn);
void SetTimeOffset(const int64_t &nOffset);
int64_t GetAdjustedTime();
int64_t GetTimeOffset();
std::string FormatFullVersion();
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments);
void CompareTimeWithPeers(const std::vector<int64_t>& vSorted);
void AddTimeData(const CNetAddr& ip, int64_t nTime, bool fSyncTime = false);
void runCommand(std::string strCommand);









inline std::string i64tostr(int64_t n)
{
    return strprintf("%" PRId64, n);
}

inline std::string itostr(int n)
{
    return strprintf("%d", n);
}

inline int64_t atoi64(const char* psz)
{
#ifdef _MSC_VER
    return _atoi64(psz);
#else
    return strtoll(psz, nullptr, 10);
#endif
}

inline int64_t atoi64(const std::string& str)
{
#ifdef _MSC_VER
    return _atoi64(str.c_str());
#else
    return strtoll(str.c_str(), nullptr, 10);
#endif
}

inline int atoi(const std::string& str)
{
    return atoi(str.c_str());
}

inline int roundint(double d)
{
    return static_cast<int>(d > 0 ? d + 0.5 : d - 0.5);
}

inline int64_t roundint64(double d)
{
    return static_cast<int64_t>(d > 0 ? d + 0.5 : d - 0.5);
}

inline int64_t abs64(int64_t n)
{
    return (n >= 0 ? n : -n);
}

inline std::string leftTrim(std::string src, char chr)
{
    std::string::size_type pos = src.find_first_not_of(chr, 0);

    if(pos > 0)
        src.erase(0, pos);

    return src;
}

template<typename T>
std::string HexStr(const T itbegin, const T itend, bool fSpaces=false)
{
    std::string rv;
    static const char hexmap[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    rv.reserve((itend-itbegin)*3);
    for(T it = itbegin; it < itend; ++it)
    {
        unsigned char val = static_cast<unsigned char>(*it);
        if(fSpaces && it != itbegin)
            rv.push_back(' ');
        rv.push_back(hexmap[val>>4]);
        rv.push_back(hexmap[val&15]);
    }

    return rv;
}

inline std::string HexStr(const std::vector<unsigned char>& vch, bool fSpaces=false)
{
    return HexStr(vch.begin(), vch.end(), fSpaces);
}

inline int64_t GetPerformanceCounter()
{
    int64_t nCounter = 0;
#ifdef WIN32
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&nCounter));
#else
    timeval t;
    gettimeofday(&t, nullptr);
    nCounter = static_cast<int64_t>(t.tv_sec) * 1000000 + t.tv_usec;
#endif
    return nCounter;
}

inline int64_t GetTimeMillis()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t GetTimeMicros()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    time_t n = nTime;
    struct tm* ptmTime = gmtime(&n);
    char pszTime[200];
    strftime(pszTime, sizeof(pszTime), pszFormat, ptmTime);
    return pszTime;
}

static const std::string strTimestampFormat = "%Y-%m-%d %H:%M:%S UTC";
inline std::string DateTimeStrFormat(int64_t nTime)
{
    return DateTimeStrFormat(strTimestampFormat.c_str(), nTime);
}


template<typename T>
void skipspaces(T& it)
{
    while (isspace(*it))
        ++it;
}

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault=false);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);

/**
 * MWC RNG of George Marsaglia
 * This is intended to be fast. It has a period of 2^59.3, though the
 * least significant 16 bits only have a period of about 2^30.1.
 *
 * @return random value
 */
extern uint32_t insecure_rand_Rz;
extern uint32_t insecure_rand_Rw;
static inline uint32_t insecure_rand(void)
{
  insecure_rand_Rz=36969*(insecure_rand_Rz&65535)+(insecure_rand_Rz>>16);
  insecure_rand_Rw=18000*(insecure_rand_Rw&65535)+(insecure_rand_Rw>>16);
  return (insecure_rand_Rw<<16)+insecure_rand_Rz;
}

/**
 * Seed insecure_rand using the random pool.
 * @param Deterministic Use a determinstic seed
 */
void seed_insecure_rand(bool fDeterministic=false);

template<typename T1>
inline uint256 Hash(const T1 pbegin, const T1 pend)
{
    static unsigned char pblank[1];
    uint256 hash1;
    EVP_Digest((pbegin == pend ? pblank : reinterpret_cast<const unsigned char*>(&pbegin[0])), (pend - pbegin) * sizeof(pbegin[0]), reinterpret_cast<unsigned char*>(&hash1), nullptr, EVP_sha256(), nullptr);
    uint256 hash2;
    EVP_Digest(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1), reinterpret_cast<unsigned char*>(&hash2), nullptr, EVP_sha256(), nullptr);
    return hash2;
}

class CHashWriter
{
private:
    EVP_MD_CTX_ptr ctx;

public:
    int nType;
    int nVersion;

    void Init() {
        ctx.reset(EVP_MD_CTX_new());
        EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
    }

    CHashWriter(int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn) {
        Init();
    }

    CHashWriter& write(const char *pch, size_t size) {
        EVP_DigestUpdate(ctx.get(), pch, size);
        return (*this);
    }

    // invalidates the object
    uint256 GetHash() {
        uint256 hash1;
        EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(&hash1), nullptr);
        uint256 hash2;
        EVP_Digest(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1), reinterpret_cast<unsigned char*>(&hash2), nullptr, EVP_sha256(), nullptr);
        return hash2;
    }

    template<typename T>
    CHashWriter& operator<<(const T& obj) {
        // Serialize to this stream
        ::Serialize(*this, obj, nType, nVersion);
        return (*this);
    }
};


template<typename T1, typename T2>
inline uint256 Hash(const T1 p1begin, const T1 p1end,
                    const T2 p2begin, const T2 p2end)
{
    static unsigned char pblank[1];
    uint256 hash1;
    EVP_MD_CTX_ptr ctx(EVP_MD_CTX_new());
    EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx.get(), (p1begin == p1end ? pblank : reinterpret_cast<const unsigned char*>(&p1begin[0])), (p1end - p1begin) * sizeof(p1begin[0]));
    EVP_DigestUpdate(ctx.get(), (p2begin == p2end ? pblank : reinterpret_cast<const unsigned char*>(&p2begin[0])), (p2end - p2begin) * sizeof(p2begin[0]));
    EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(&hash1), nullptr);
    uint256 hash2;
    EVP_Digest(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1), reinterpret_cast<unsigned char*>(&hash2), nullptr, EVP_sha256(), nullptr);
    return hash2;
}

template<typename T1, typename T2, typename T3>
inline uint256 Hash(const T1 p1begin, const T1 p1end,
                    const T2 p2begin, const T2 p2end,
                    const T3 p3begin, const T3 p3end)
{
    static unsigned char pblank[1];
    uint256 hash1;
    EVP_MD_CTX_ptr ctx(EVP_MD_CTX_new());
    EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx.get(), (p1begin == p1end ? pblank : reinterpret_cast<const unsigned char*>(&p1begin[0])), (p1end - p1begin) * sizeof(p1begin[0]));
    EVP_DigestUpdate(ctx.get(), (p2begin == p2end ? pblank : reinterpret_cast<const unsigned char*>(&p2begin[0])), (p2end - p2begin) * sizeof(p2begin[0]));
    EVP_DigestUpdate(ctx.get(), (p3begin == p3end ? pblank : reinterpret_cast<const unsigned char*>(&p3begin[0])), (p3end - p3begin) * sizeof(p3begin[0]));
    EVP_DigestFinal_ex(ctx.get(), reinterpret_cast<unsigned char*>(&hash1), nullptr);
    uint256 hash2;
    EVP_Digest(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1), reinterpret_cast<unsigned char*>(&hash2), nullptr, EVP_sha256(), nullptr);
    return hash2;
}

template<typename T>
uint256 SerializeHash(const T& obj, int nType=SER_GETHASH, int nVersion=PROTOCOL_VERSION)
{
    CHashWriter ss(nType, nVersion);
    ss << obj;
    return ss.GetHash();
}

inline uint160 Hash160(const std::vector<unsigned char>& vch)
{
    uint256 hash1;
    EVP_Digest(&vch[0], vch.size(), reinterpret_cast<unsigned char*>(&hash1), nullptr, EVP_sha256(), nullptr);
    uint160 hash2;
    EVP_Digest(reinterpret_cast<const unsigned char*>(&hash1), sizeof(hash1), reinterpret_cast<unsigned char*>(&hash2), nullptr, EVP_ripemd160(), nullptr);
    return hash2;
}

/**
 * Timing-attack-resistant comparison.
 * Takes time proportional to length
 * of first argument.
 */
template <typename T>
bool TimingResistantEqual(const T& a, const T& b)
{
    if (b.size() == 0) return a.size() == 0;
    size_t accumulator = a.size() ^ b.size();
    for (size_t i = 0; i < a.size(); i++)
        accumulator |= a[i] ^ b[i%b.size()];
    return accumulator == 0;
}

/** Median filter over a stream of values.
 * Returns the median of the last N numbers
 */
template <typename T> class CMedianFilter
{
private:
    std::vector<T> vValues;
    std::vector<T> vSorted;
    unsigned int nSize;
public:
    CMedianFilter(unsigned int size, T initial_value):
        nSize(size)
    {
        vValues.reserve(size);
        vValues.push_back(initial_value);
        vSorted = vValues;
    }

    void input(T value)
    {
        if(vValues.size() == nSize)
        {
            vValues.erase(vValues.begin());
        }
        vValues.push_back(value);

        vSorted.resize(vValues.size());
        std::copy(vValues.begin(), vValues.end(), vSorted.begin());
        std::sort(vSorted.begin(), vSorted.end());
    }

    T median() const
    {
        int size = vSorted.size();
        assert(size>0);
        if(size & 1) // Odd number of elements
        {
            return vSorted[size/2];
        }
        else // Even number of elements
        {
            return (vSorted[size/2-1] + vSorted[size/2]) / 2;
        }
    }

    int size() const
    {
        return vValues.size();
    }

    std::vector<T> sorted () const
    {
        return vSorted;
    }
};

bool NewThread(void(*pfn)(void*), void* parg);

// Minimal std::thread-based replacement for boost::thread_group.
// Stores joinable threads created via create_thread() and provides join_all().
// interrupt_all() sets fShutdown — threads must poll fShutdown to exit.
class ThreadGroup {
    std::vector<std::thread> m_threads;
public:
    ThreadGroup() = default;
    ~ThreadGroup() { join_all(); }

    ThreadGroup(const ThreadGroup&) = delete;
    ThreadGroup& operator=(const ThreadGroup&) = delete;

    template <typename Callable>
    void create_thread(Callable&& f) {
        m_threads.emplace_back(std::forward<Callable>(f));
    }

    void interrupt_all() {
        // std::thread has no interruption mechanism.
        // Threads must check fShutdown to exit gracefully.
    }

    void join_all() {
        for (auto& t : m_threads)
            if (t.joinable()) t.join();
        m_threads.clear();
    }
};

#ifdef WIN32
inline void SetThreadPriority(int nPriority)
{
    SetThreadPriority(GetCurrentThread(), nPriority);
}
#else

#define THREAD_PRIORITY_LOWEST          PRIO_MAX
#define THREAD_PRIORITY_BELOW_NORMAL    2
#define THREAD_PRIORITY_NORMAL          0
#define THREAD_PRIORITY_ABOVE_NORMAL    0

inline void SetThreadPriority(int nPriority)
{
    // It's unclear if it's even possible to change thread priorities on Linux,
    // but we really and truly need it for the generation threads.
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif
}

inline void ExitThread(size_t nExitCode)
{
    pthread_exit(reinterpret_cast<void*>(nExitCode));
}
#endif

void RenameThread(const char* name);

inline uint32_t ByteReverse(uint32_t value)
{
    value = ((value & 0xFF00FF00) >> 8) | ((value & 0x00FF00FF) << 8);
    return (value<<16) | (value>>16);
}

// Standard wrapper for do-something-forever thread functions.
// "Forever" really means until fShutdown is set.
template <typename Callable> void LoopForever(const char* name, Callable func, int64_t msecs)
{
    std::string s = strprintf("pinkcoin-%s", name);
    RenameThread(s.c_str());
    printf("%s thread start\n", name);
    try
    {
        while (1)
        {
            func();
            MilliSleep(msecs);
        }
    }
    catch (std::exception& e) {
        PrintException(&e, name);
    }
    catch (...) {
        PrintException(nullptr, name);
    }
}
// .. and a wrapper that just calls func once
template <typename Callable> void TraceThread(const char* name, Callable func)
{
    std::string s = strprintf("pinkcoin-%s", name);
    RenameThread(s.c_str());
    try
    {
        printf("%s thread start\n", name);
        func();
        printf("%s thread exit\n", name);
    }
    catch (std::exception& e) {
        PrintException(&e, name);
    }
    catch (...) {
        PrintException(nullptr, name);
    }
}

#endif
