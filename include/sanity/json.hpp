// podcast-sanity - minimal dependency-free JSON writer
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace sanity {

class JsonWriter {
public:
    explicit JsonWriter(int indent = 2) : indent_(indent) {}

    void begin_object() { open('{'); }
    void end_object() { close('}'); }
    void begin_array() { open('['); }
    void end_array() { close(']'); }

    void key(const std::string& k) {
        comma();
        newline();
        out_ += quote(k);
        out_ += ": ";
        after_key_ = true;
    }

    void value_string(const std::string& v) { comma(); newline(); out_ += quote(v); }
    void value_double(double v) { comma(); newline(); out_ += number(v); }
    void value_int(long long v) { comma(); newline(); out_ += std::to_string(v); }
    void value_bool(bool v) { comma(); newline(); out_ += v ? "true" : "false"; }
    void value_null() { comma(); newline(); out_ += "null"; }

    // Inline form used for arrays of numbers.
    void value_double_inline(double v) {
        comma();
        out_ += number(v);
    }

    void raw_value(const std::string& v) { comma(); newline(); out_ += v; }

    void member(const std::string& k, const std::string& v) { key(k); value_string(v); }
    void member(const std::string& k, double v) { key(k); value_double(v); }
    void member(const std::string& k, long long v) { key(k); value_int(v); }
    void member(const std::string& k, int v) { key(k); value_int(v); }
    void member(const std::string& k, bool v) { key(k); value_bool(v); }

    const std::string& str() const { return out_; }

private:
    void open(char c) {
        comma();
        newline();
        out_ += c;
        stack_.push_back(true);
        after_key_ = false;
    }
    void close(char c) {
        const bool empty = stack_.empty() || stack_.back();
        stack_.pop_back();
        if (!empty) newline();
        out_ += c;
        after_key_ = false;
    }
    void comma() {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (!stack_.empty() && !stack_.back()) out_ += ",";
        if (!stack_.empty()) stack_.back() = false;
    }
    void newline() {
        if (indent_ <= 0) return;
        out_ += "\n";
        out_.append(static_cast<std::size_t>(indent_) * stack_.size(), ' ');
    }

    static std::string quote(const std::string& s);
    static std::string number(double v);

    std::string out_;
    std::vector<bool> stack_;
    int indent_ = 2;
    bool after_key_ = false;
};

}  // namespace sanity
