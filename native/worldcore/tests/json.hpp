// json.hpp - just enough JSON to read golden files and native/data tables.
// Numbers go through strtod, which round-trips the shortest representation
// JavaScript emits, so golden doubles arrive bit-exact.
#pragma once

#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::map<std::string, Value> o;

    const Value& operator[](const std::string& key) const {
        auto it = o.find(key);
        if (it == o.end()) throw std::runtime_error("json: missing key " + key);
        return it->second;
    }
    const Value& operator[](size_t i) const { return a.at(i); }
    bool has(const std::string& key) const { return o.count(key) != 0; }
    size_t size() const { return kind == Array ? a.size() : o.size(); }
    double num() const { return kind == Bool ? (b ? 1 : 0) : n; }
};

class Parser {
public:
    explicit Parser(const std::string& text) : t_(text) {}

    Value parse() {
        Value v = value();
        ws();
        if (i_ != t_.size()) fail("trailing data");
        return v;
    }

private:
    [[noreturn]] void fail(const char* what) {
        throw std::runtime_error(std::string("json: ") + what + " at " + std::to_string(i_));
    }
    void ws() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t')) i_++;
    }
    bool lit(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (t_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }
    std::string str() {
        if (t_[i_] != '"') fail("expected string");
        std::string out;
        for (i_++; i_ < t_.size() && t_[i_] != '"'; i_++) {
            char c = t_[i_];
            if (c == '\\') {
                c = t_[++i_];
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'u': out += '?'; i_ += 4; break;  // not needed for our data
                    default: out += c;
                }
            } else {
                out += c;
            }
        }
        i_++;
        return out;
    }
    Value value() {
        ws();
        if (i_ >= t_.size()) fail("unexpected end");
        Value v;
        char c = t_[i_];
        if (c == '{') {
            v.kind = Value::Object;
            i_++;
            ws();
            if (t_[i_] == '}') { i_++; return v; }
            for (;;) {
                ws();
                std::string k = str();
                ws();
                if (t_[i_++] != ':') fail("expected :");
                v.o[k] = value();
                ws();
                if (t_[i_] == ',') { i_++; continue; }
                if (t_[i_] == '}') { i_++; return v; }
                fail("expected , or }");
            }
        }
        if (c == '[') {
            v.kind = Value::Array;
            i_++;
            ws();
            if (t_[i_] == ']') { i_++; return v; }
            for (;;) {
                v.a.push_back(value());
                ws();
                if (t_[i_] == ',') { i_++; continue; }
                if (t_[i_] == ']') { i_++; return v; }
                fail("expected , or ]");
            }
        }
        if (c == '"') { v.kind = Value::String; v.s = str(); return v; }
        if (lit("true")) { v.kind = Value::Bool; v.b = true; return v; }
        if (lit("false")) { v.kind = Value::Bool; return v; }
        if (lit("null")) return v;
        char* end = nullptr;
        v.n = std::strtod(t_.c_str() + i_, &end);
        if (end == t_.c_str() + i_) fail("bad value");
        v.kind = Value::Number;
        i_ = static_cast<size_t>(end - t_.c_str());
        return v;
    }

    const std::string& t_;
    size_t i_ = 0;
};

inline Value load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("json: cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    return Parser(text).parse();
}

}  // namespace json
