#ifndef RESULT_H
#define RESULT_H

#include <string>
#include <optional>
#include <stdexcept>
#include <utility>

// Lightweight Result<T> for new code paths.
// Use instead of bool+error string or throw for expected failures.
template<typename T>
class Result {
    std::optional<T> m_value;
    std::string m_error;
public:
    // Success constructor
    Result(T value) : m_value(std::move(value)) {}

    // Error constructor
    static Result Error(std::string msg) {
        Result r;
        r.m_error = std::move(msg);
        return r;
    }

    bool ok() const { return m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    const T& value() const {
        if (!ok()) throw std::runtime_error("Result::value() called on error: " + m_error);
        return *m_value;
    }

    T& value() {
        if (!ok()) throw std::runtime_error("Result::value() called on error: " + m_error);
        return *m_value;
    }

    const std::string& error() const {
        if (ok()) throw std::runtime_error("Result::error() called on success");
        return m_error;
    }

    // Value-or-default
    T value_or(T default_val) const {
        return ok() ? *m_value : std::move(default_val);
    }

private:
    Result() = default;
};

// Specialization for void (success/failure without a value)
template<>
class Result<void> {
    std::string m_error;
    bool m_ok;
public:
    Result() : m_ok(true) {}
    static Result Success() { return Result(); }
    static Result Error(std::string msg) {
        Result r;
        r.m_ok = false;
        r.m_error = std::move(msg);
        return r;
    }

    bool ok() const { return m_ok; }
    explicit operator bool() const { return m_ok; }
    const std::string& error() const {
        if (m_ok) throw std::runtime_error("Result::error() called on success");
        return m_error;
    }
};

#endif // RESULT_H
