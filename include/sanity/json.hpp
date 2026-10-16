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
        separator(true);
        out_ += quote(k);
        out_ += ": ";
        after_key_ = true;
    }

    void value_string(const std::string& v) { separator(true); out_ += quote(v); }
    void value_double(double v) { separator(true); out_ += number(v); }
    void value_int(long long v) { separator(true); out_ += std::to_string(v); }
    void value_bool(bool v) { separator(true); out_ += v ? "true" : "false"; }
    void value_null() { separator(true); out_ += "null"; }

    // Inline form used for arrays of numbers.
    void value_double_inline(double v) {
        separator(false);
        out_ += number(v);
    }

    void raw_value(const std::string& v) { separator(true); out_ += v; }

    void member(const std::string& k, const std::string& v) { key(k); value_string(v); }
    void member(const std::string& k, double v) { key(k); value_double(v); }
    void member(const std::string& k, long long v) { key(k); value_int(v); }
    void member(const std::string& k, int v) { key(k); value_int(v); }
    void member(const std::string& k, bool v) { key(k); value_bool(v); }

    const std::string& str() const { return out_; }

private:
    void open(char c) {
        separator(true);
        out_ += c;
        stack_.push_back(true);
        inline_.push_back(true);
        after_key_ = false;
    }
    void close(char c) {
        const bool empty = stack_.empty() || stack_.back();
        // A container that only ever held inline values closes on its own line:
        // "e": [1.0,2.25] stays one line, top level arrays of numbers too.
        const bool inline_only = !inline_.empty() && inline_.back();
        if (!stack_.empty()) stack_.pop_back();
        if (!inline_.empty()) inline_.pop_back();
        if (!empty && !inline_only) newline();
        out_ += c;
        after_key_ = false;
    }
    // Emits the separator before a new item. After a key nothing is emitted:
    // the value belongs on the same line as its key.
    void separator(bool newline_ok) {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (!stack_.empty() && !stack_.back()) out_ += ",";
        if (!stack_.empty()) stack_.back() = false;
        if (newline_ok) {
            if (!inline_.empty()) inline_.back() = false;
            newline();
        }
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
    std::vector<bool> inline_;
    int indent_ = 2;
    bool after_key_ = false;
};

}  // namespace sanity
