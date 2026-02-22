#ifndef PINK_SIGNAL_H
#define PINK_SIGNAL_H

#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <algorithm>
#include <optional>

// Connection handle — allows disconnecting a slot
class Connection {
    std::shared_ptr<bool> m_alive;
public:
    Connection() : m_alive(std::make_shared<bool>(true)) {}
    void disconnect() { if (m_alive) *m_alive = false; }
    bool connected() const { return m_alive && *m_alive; }
    std::shared_ptr<bool> tracker() const { return m_alive; }
};

// Signal with void return — matches boost::signals2 usage pattern
template<typename... Args>
class Signal {
    struct Slot {
        std::function<void(Args...)> fn;
        std::shared_ptr<bool> alive;
    };
    std::vector<Slot> m_slots;
    mutable std::mutex m_mutex;
public:
    Connection connect(std::function<void(Args...)> fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Connection conn;
        m_slots.push_back({std::move(fn), conn.tracker()});
        return conn;
    }

    void operator()(Args... args) {
        std::vector<Slot> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Remove dead slots
            m_slots.erase(
                std::remove_if(m_slots.begin(), m_slots.end(),
                    [](const Slot& s) { return !*s.alive; }),
                m_slots.end());
            snapshot = m_slots;
        }
        for (auto& slot : snapshot)
            if (*slot.alive) slot.fn(args...);
    }

    void disconnect_all() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& slot : m_slots)
            *slot.alive = false;
        m_slots.clear();
    }
};

// Signal with bool return + last_value combiner (for ThreadSafeAskFee)
template<typename... Args>
class SignalLastValue {
    struct Slot {
        std::function<bool(Args...)> fn;
        std::shared_ptr<bool> alive;
    };
    std::vector<Slot> m_slots;
    mutable std::mutex m_mutex;
public:
    Connection connect(std::function<bool(Args...)> fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Connection conn;
        m_slots.push_back({std::move(fn), conn.tracker()});
        return conn;
    }

    bool operator()(Args... args) {
        std::vector<Slot> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_slots.erase(
                std::remove_if(m_slots.begin(), m_slots.end(),
                    [](const Slot& s) { return !*s.alive; }),
                m_slots.end());
            snapshot = m_slots;
        }
        bool result = false;
        for (auto& slot : snapshot)
            if (*slot.alive) result = slot.fn(args...);
        return result;  // returns last slot's result (matches last_value combiner)
    }
};

// Signal with arbitrary return type — returns std::optional<R>
// Replaces boost::signals2::signal<R(Args...)> whose operator() returns boost::optional<R>
template<typename R, typename... Args>
class SignalOptional {
    struct Slot {
        std::function<R(Args...)> fn;
        std::shared_ptr<bool> alive;
    };
    std::vector<Slot> m_slots;
    mutable std::mutex m_mutex;
public:
    Connection connect(std::function<R(Args...)> fn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Connection conn;
        m_slots.push_back({std::move(fn), conn.tracker()});
        return conn;
    }

    std::optional<R> operator()(Args... args) {
        std::vector<Slot> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_slots.erase(
                std::remove_if(m_slots.begin(), m_slots.end(),
                    [](const Slot& s) { return !*s.alive; }),
                m_slots.end());
            snapshot = m_slots;
        }
        std::optional<R> result;
        for (auto& slot : snapshot)
            if (*slot.alive) result = slot.fn(args...);
        return result;  // empty if no slots connected
    }
};

#endif // PINK_SIGNAL_H
