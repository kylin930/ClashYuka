#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace clashyuka::core {

enum class ErrorKind {
    None,
    InvalidConfig,
    ConnectionFailed,
    KernelNotRunning,
    Timeout,
    HttpStatus,
    AuthFailed,
    InvalidJson,
    MissingField,
    TypeMismatch,
    Cancelled,
    KernelNotFound,
    ConfigFailed,
    StartFailed,
    DownloadFailed,
    ChecksumMissing,
    ChecksumMismatch,
    ExtractFailed,
    Internal,
    ProfilePreflightRejected,
};

struct Error {
    ErrorKind kind = ErrorKind::None;
    std::string message;
    int httpStatus = 0;

    explicit operator bool() const noexcept { return kind != ErrorKind::None; }
};

inline Error MakeError(ErrorKind kind, std::string message, int httpStatus = 0) {
    return Error{kind, std::move(message), httpStatus};
}

// Lightweight expected-like type used across the Mihomo layer so callers never
// see cpp-httplib or nlohmann exception types.
template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)), ok_(true) {}
    Result(Error error) : error_(std::move(error)), ok_(false) {
        if (error_.kind == ErrorKind::None) {
            error_.kind = ErrorKind::Internal;
        }
    }

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    T& value() & {
        EnsureOk();
        return value_;
    }
    const T& value() const& {
        EnsureOk();
        return value_;
    }
    T Take() {
        EnsureOk();
        return std::move(value_);
    }

    const Error& error() const noexcept { return error_; }

private:
    void EnsureOk() const {
        if (!ok_) {
            throw std::logic_error("Result does not contain a value: " + error_.message);
        }
    }

    T value_{};
    Error error_{};
    bool ok_ = false;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), ok_(false) {
        if (error_.kind == ErrorKind::None) {
            error_.kind = ErrorKind::Internal;
        }
    }

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Error& error() const noexcept { return error_; }

private:
    Error error_{};
    bool ok_ = true;
};

}  // namespace clashyuka::core
