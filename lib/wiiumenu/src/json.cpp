#include "wiiumenu/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wiiumenu::json {

Value Value::f32(float f) {
    if (!std::isfinite(f)) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08x", bits);
        return Value(std::string(buf));  // non-finite floats are written as their bit pattern
    }
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, f);
    return number(std::string(buf, r.ptr));
}

const Object& Value::object() const {
    if (!is_object()) throw FormatError("JSON: expected object");
    return std::get<Object>(v);
}
Object& Value::object() {
    if (!is_object()) throw FormatError("JSON: expected object");
    return std::get<Object>(v);
}
const Array& Value::array() const {
    if (!is_array()) throw FormatError("JSON: expected array");
    return std::get<Array>(v);
}
Array& Value::array() {
    if (!is_array()) throw FormatError("JSON: expected array");
    return std::get<Array>(v);
}
const std::string& Value::string() const {
    if (!is_string()) throw FormatError("JSON: expected string");
    return std::get<std::string>(v);
}
bool Value::boolean() const {
    if (!std::holds_alternative<bool>(v)) throw FormatError("JSON: expected true/false");
    return std::get<bool>(v);
}
long long Value::as_int() const {
    if (is_string()) {  // allow "0x..." for hex-minded editing
        const std::string& s = string();
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return std::stoll(s.substr(2), nullptr, 16);
    }
    if (!is_number()) throw FormatError("JSON: expected number");
    const std::string& t = std::get<Number>(v).text;
    long long out = 0;
    auto r = std::from_chars(t.data(), t.data() + t.size(), out);
    if (r.ec != std::errc() || r.ptr != t.data() + t.size()) throw FormatError("JSON: expected integer, got " + t);
    return out;
}
float Value::as_f32() const {
    if (is_string()) {
        const std::string& s = string();
        if (s.size() == 10 && s[0] == '0' && s[1] == 'x') {
            std::uint32_t bits = std::uint32_t(std::stoul(s.substr(2), nullptr, 16));
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        }
    }
    if (!is_number()) throw FormatError("JSON: expected number");
    const std::string& t = std::get<Number>(v).text;
    float out = 0;
    auto r = std::from_chars(t.data(), t.data() + t.size(), out);
    if (r.ec != std::errc() || r.ptr != t.data() + t.size()) throw FormatError("JSON: bad number " + t);
    return out;
}

const Value* Value::find(std::string_view key) const {
    for (const auto& [k, val] : object())
        if (k == key) return &val;
    return nullptr;
}
const Value& Value::at(std::string_view key) const {
    const Value* p = find(key);
    if (!p) throw FormatError("JSON: missing key \"" + std::string(key) + "\"");
    return *p;
}
Value& Value::set(std::string key, Value val) {
    if (is_null()) v = Object{};
    auto& o = object();
    o.emplace_back(std::move(key), std::move(val));
    return o.back().second;
}

// ---------------- parser ----------------
namespace {

struct Parser {
    std::string_view s;
    std::size_t i = 0;
    std::size_t line = 1;

    [[noreturn]] void fail(const std::string& why) {
        throw FormatError("JSON parse error at line " + std::to_string(line) + ": " + why);
    }
    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            if (s[i] == '\n') ++line;
            ++i;
        }
    }
    char peek() { ws(); return i < s.size() ? s[i] : '\0'; }
    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++i;
    }
    static void put_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | cp >> 12); out += char(0x80 | (cp >> 6 & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | cp >> 18); out += char(0x80 | (cp >> 12 & 0x3F)); out += char(0x80 | (cp >> 6 & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    std::uint32_t hex4() {
        if (i + 4 > s.size()) fail("short \\u escape");
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= std::uint32_t(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }
    std::string str() {
        expect('"');
        std::string out;
        while (true) {
            if (i >= s.size()) fail("unterminated string");
            char c = s[i++];
            if (c == '"') break;
            if (c == '\n') fail("newline in string");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) fail("bad escape");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        std::size_t save = i;
                        i += 2;
                        std::uint32_t lo = hex4();
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else i = save;
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
        return out;
    }
    Value value() {
        char c = peek();
        if (c == '{') {
            ++i;
            Value o = Object{};
            if (peek() == '}') { ++i; return o; }
            while (true) {
                std::string k = str();
                expect(':');
                o.object().emplace_back(std::move(k), value());
                char d = peek();
                ++i;
                if (d == '}') break;
                if (d != ',') fail("expected ',' or '}'");
            }
            return o;
        }
        if (c == '[') {
            ++i;
            Value a = Array{};
            if (peek() == ']') { ++i; return a; }
            while (true) {
                a.array().push_back(value());
                char d = peek();
                ++i;
                if (d == ']') break;
                if (d != ',') fail("expected ',' or ']'");
            }
            return a;
        }
        if (c == '"') return Value(str());
        if (s.substr(i, 4) == "true") { i += 4; return Value(true); }
        if (s.substr(i, 5) == "false") { i += 5; return Value(false); }
        if (s.substr(i, 4) == "null") { i += 4; return Value(nullptr); }
        std::size_t st = i;
        while (i < s.size() && (std::strchr("+-0123456789.eE", s[i]) != nullptr)) ++i;
        if (st == i) fail("unexpected character");
        return Value::number(std::string(s.substr(st, i - st)));
    }
};

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += char(c);
                }
        }
    }
    out += '"';
}

bool scalar(const Value& v) { return !v.is_array() && !v.is_object(); }

void dump_to(std::string& out, const Value& v, int depth) {
    auto indent = [&](int d) { out += '\n'; out.append(std::size_t(d), ' '); };
    if (v.is_null()) out += "null";
    else if (std::holds_alternative<bool>(v.v)) out += std::get<bool>(v.v) ? "true" : "false";
    else if (v.is_number()) out += std::get<Number>(v.v).text;
    else if (v.is_string()) escape(out, v.string());
    else if (v.is_array()) {
        const auto& a = v.array();
        bool flat = a.size() <= 16;
        for (const auto& x : a) flat = flat && scalar(x);
        if (a.empty()) { out += "[]"; return; }
        out += '[';
        for (std::size_t k = 0; k < a.size(); ++k) {
            if (flat) { if (k) out += ", "; }
            else { if (k) out += ','; indent(depth + 1); }
            dump_to(out, a[k], depth + 1);
        }
        if (!flat) indent(depth);
        out += ']';
    } else {
        const auto& o = v.object();
        if (o.empty()) { out += "{}"; return; }
        out += '{';
        for (std::size_t k = 0; k < o.size(); ++k) {
            if (k) out += ',';
            indent(depth + 1);
            escape(out, o[k].first);
            out += ": ";
            dump_to(out, o[k].second, depth + 1);
        }
        indent(depth);
        out += '}';
    }
}

}  // namespace

Value parse(std::string_view text) {
    Parser p{text};
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        p.i = 3;  // tolerate a UTF-8 BOM from Windows editors
    Value v = p.value();
    if (p.peek() != '\0') p.fail("trailing content");
    return v;
}

std::string dump(const Value& v) {
    std::string out;
    dump_to(out, v, 0);
    out += '\n';
    return out;
}

}  // namespace wiiumenu::json
