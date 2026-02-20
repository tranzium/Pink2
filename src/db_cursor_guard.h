#ifndef DB_CURSOR_GUARD_H
#define DB_CURSOR_GUARD_H

#include <db_cxx.h>

// RAII guard for BerkeleyDB Dbc* cursors.
// Ensures cursor is closed on scope exit, even on exception.
class BdbCursorGuard {
    Dbc* m_cursor;
public:
    explicit BdbCursorGuard(Dbc* cursor) noexcept : m_cursor(cursor) {}
    ~BdbCursorGuard() { if (m_cursor) m_cursor->close(); }

    BdbCursorGuard(const BdbCursorGuard&) = delete;
    BdbCursorGuard& operator=(const BdbCursorGuard&) = delete;

    Dbc* get() const noexcept { return m_cursor; }
    explicit operator bool() const noexcept { return m_cursor != nullptr; }

    Dbc* release() noexcept {
        Dbc* p = m_cursor;
        m_cursor = nullptr;
        return p;
    }
};

#endif // DB_CURSOR_GUARD_H
