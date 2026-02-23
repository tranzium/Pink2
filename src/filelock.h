// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FILELOCK_H
#define FILELOCK_H

#include <string>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Cross-platform file lock -- replaces boost::interprocess::file_lock.
// RAII: lock acquired in try_lock(), released in destructor.
class FileLock {
public:
    explicit FileLock(const std::string& path)
#ifdef _WIN32
        : hFile(CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
        if (hFile == INVALID_HANDLE_VALUE)
            throw std::runtime_error("FileLock: cannot open " + path);
    }
#else
        : fd(open(path.c_str(), O_RDWR | O_CREAT, 0600))
    {
        if (fd < 0)
            throw std::runtime_error("FileLock: cannot open " + path);
    }
#endif

    ~FileLock()
    {
#ifdef _WIN32
        if (locked) {
            OVERLAPPED ov = {};
            UnlockFileEx(hFile, 0, 1, 0, &ov);
        }
        if (hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
#else
        if (locked)
            flock(fd, LOCK_UN);
        if (fd >= 0)
            close(fd);
#endif
    }

    bool try_lock()
    {
#ifdef _WIN32
        OVERLAPPED ov = {};
        locked = LockFileEx(hFile, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                           0, 1, 0, &ov) != 0;
#else
        locked = (flock(fd, LOCK_EX | LOCK_NB) == 0);
#endif
        return locked;
    }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    bool locked = false;
#ifdef _WIN32
    HANDLE hFile;
#else
    int fd;
#endif
};

#endif // FILELOCK_H
