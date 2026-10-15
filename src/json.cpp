#include "sanity/json.hpp"

#include <cmath>
#include <cstdio>

namespace sanity {

std::string JsonWriter::quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    o += "\"";
    return o;
}

std::string JsonWriter::number(double v) {
    if (std::isnan(v) || std::isinf(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    std::string s(buf);
    // Trim trailing zeros but keep at least one decimal digit.
    if (s.find('.') != std::string::npos) {
        std::size_t last = s.find_last_not_of('0');
        if (s[last] == '.') last++;
        s.erase(last + 1);
    }
    return s;
}

}  // namespace sanity
