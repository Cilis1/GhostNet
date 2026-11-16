// Minimal, strict JSON parser for the GitHub Releases API response.
// Header-only and platform-neutral so it can be unit-tested anywhere.
#pragma once
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<Value> arr;
    std::map<std::string, Value> obj;

    const Value* get(const std::string& k) const {
        if (type != Type::Object) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    std::string getString(const std::string& k, const std::string& def = "") const {
        const Value* v = get(k);
        return v && v->type == Type::String ? v->str : def;
    }
    double getNumber(const std::string& k, double def = -1) const {
        const Value* v = get(k);
        return v && v->type == Type::Number ? v->num : def;
    }
    bool getBool(const std::string& k, bool def = false) const {
        const Value* v = get(k);
        return v && v->type == Type::Bool ? v->b : def;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}
    bool parse(Value& out, std::string& err) {
        try {
            ws();
            value(out, 0);
            ws();
            if (i_ != s_.size()) fail("trailing characters");
            return true;
        } catch (const std::string& e) {
            err = e + " at byte " + std::to_string(i_);
            return false;
        }
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    [[noreturn]] void fail(const char* m) { throw std::string(m); }
    void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    char peek() { if (i_ >= s_.size()) fail("unexpected end"); return s_[i_]; }
    void expect(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (i_ >= s_.size() || s_[i_] != *p) fail("bad literal");
            ++i_;
        }
    }
    void value(Value& v, int depth) {
        if (depth > 64) fail("nesting too deep");
        char c = peek();
        if (c == '{') object(v, depth);
        else if (c == '[') array(v, depth);
        else if (c == '"') { v.type = Value::Type::String; v.str = string(); }
        else if (c == 't') { expect("true"); v.type = Value::Type::Bool; v.b = true; }
        else if (c == 'f') { expect("false"); v.type = Value::Type::Bool; v.b = false; }
        else if (c == 'n') { expect("null"); v.type = Value::Type::Null; }
        else number(v);
    }
    void object(Value& v, int depth) {
        v.type = Value::Type::Object;
        ++i_;
        ws();
        if (peek() == '}') { ++i_; return; }
        for (;;) {
            ws();
            if (peek() != '"') fail("expected key");
            std::string k = string();
            ws();
            if (peek() != ':') fail("expected ':'");
            ++i_;
            ws();
            Value child;
            value(child, depth + 1);
            v.obj[k] = std::move(child);
            ws();
            char c = peek();
            ++i_;
            if (c == '}') return;
            if (c != ',') fail("expected ',' or '}'");
        }
    }
    void array(Value& v, int depth) {
        v.type = Value::Type::Array;
        ++i_;
        ws();
        if (peek() == ']') { ++i_; return; }
        for (;;) {
            ws();
            Value child;
            value(child, depth + 1);
            v.arr.push_back(std::move(child));
            ws();
            char c = peek();
            ++i_;
            if (c == ']') return;
            if (c != ',') fail("expected ',' or ']'");
        }
    }
    void number(Value& v) {
        size_t st = i_;
        if (peek() == '-') ++i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' || s_[i_] == 'e' ||
                                  s_[i_] == 'E' || s_[i_] == '+' || s_[i_] == '-'))
            ++i_;
        if (st == i_ || (i_ - st == 1 && s_[st] == '-')) fail("bad value");
        v.type = Value::Type::Number;
        try { v.num = std::stod(s_.substr(st, i_ - st)); } catch (...) { fail("bad number"); }
    }
    static void utf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    uint32_t hex4() {
        if (i_ + 4 > s_.size()) fail("bad \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else fail("bad hex digit");
        }
        return v;
    }
    std::string string() {
        ++i_;  // opening quote
        std::string o;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            char c = s_[i_++];
            if (c == '"') return o;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { o += c; continue; }
            if (i_ >= s_.size()) fail("bad escape");
            char e = s_[i_++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i_ + 2 > s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') fail("lone surrogate");
                        i_ += 2;
                        uint32_t lo = hex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) fail("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        fail("lone surrogate");
                    }
                    utf8(o, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
};

inline bool parse(const std::string& s, Value& out, std::string& err) { return Parser(s).parse(out, err); }

}  // namespace json
