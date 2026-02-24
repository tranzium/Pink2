// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PINKCOIN_THREAD_GUARD_H
#define PINKCOIN_THREAD_GUARD_H

#include "net.h"  // for vnThreadsRunning, threadId

/// RAII guard that increments a thread counter on construction
/// and decrements it on destruction. Replaces manual try/catch
/// increment/decrement patterns.
struct ThreadCountGuard {
    int index;
    explicit ThreadCountGuard(int i) : index(i) { vnThreadsRunning[index]++; }
    ~ThreadCountGuard() { vnThreadsRunning[index]--; }
    ThreadCountGuard(const ThreadCountGuard&) = delete;
    ThreadCountGuard& operator=(const ThreadCountGuard&) = delete;
};

#endif // PINKCOIN_THREAD_GUARD_H
