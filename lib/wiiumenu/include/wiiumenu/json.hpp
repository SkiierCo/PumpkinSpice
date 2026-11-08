// Minimal JSON value, parser and pretty printer.
//
// Numbers keep their source text, so a float written with the shortest
// round-trip representation parses back to the identical bit pattern.
// Objects keep insertion order so files stay stable and diff cleanly.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "wiiumenu/bytes.hpp"

namespace wiiumenu::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Number { std::string text; };

struct Value {
    std::variant<std::nullptr_t, bool, Number, std::string, Array, Object> v = nullptr;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v(b) {}
    Value(const char* s) : v(std::string(s)) {}
    Value(std::string s) : v(std::move(s)) {}
    Value(Array a) : v(std::move(a)) {}
    Value(Object o) : v(std::move(o)) {}
    static Value number(std::string text) { Value x; x.v = Number{std::move(text)}; return x; }
    static Value integer(long long i) { return number(std::to_string(i)); }
    static Value f32(float f);

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(v); }
    bool is_object() const { return std::holds_alternative<Object>(v); }
    bool is_array() const { return std::holds_alternative<Array>(v); }
    bool is_string() const { return std::holds_alternative<std::string>(v); }
    bool is_number() const { return std::holds_alternative<Number>(v); }

    const Object& object() const;
    Object& object();
    const Array& array() const;
    Array& array();
    const std::string& string() const;
    bool boolean() const;
    long long as_int() const;
    float as_f32() const;

    // Object access. find() returns nullptr when absent; at() throws.
    const Value* find(std::string_view key) const;
    const Value& at(std::string_view key) const;
    Value& set(std::string key, Value val);  // appends (keys are unique by construction)
};

Value parse(std::string_view text);
std::string dump(const Value& v);  // pretty, 1-space indent, short arrays of scalars on one line

}  // namespace wiiumenu::json
