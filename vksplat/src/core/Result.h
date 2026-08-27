#pragma once

#include <string>
#include <utility>
#include <variant>

namespace core {

struct Error {
    std::string message;
};

template <typename T>
class Result {
public:
    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}

    explicit operator bool() const { return std::holds_alternative<T>(storage_); }

    const T& value() const { return std::get<T>(storage_); }
    T&& take() { return std::move(std::get<T>(storage_)); }
    const std::string& error() const { return std::get<Error>(storage_).message; }

private:
    std::variant<T, Error> storage_;
};

}
