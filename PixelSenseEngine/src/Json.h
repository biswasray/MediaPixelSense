// Json.h : Minimal JSON output helpers. The engine only ever writes JSON,
// so a full parser is unnecessary.

#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pse::json {

inline std::string quote(std::string_view s)
{
	std::string out;
	out.reserve(s.size() + 2);
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
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += static_cast<char>(c);
			}
		}
	}
	out += '"';
	return out;
}

inline std::string number(double v)
{
	std::ostringstream ss;
	ss << v;
	return ss.str();
}

// Builds a JSON object from already-encoded values.
class Object {
public:
	Object& raw(std::string_view key, std::string_view encoded)
	{
		if (!body_.empty()) body_ += ',';
		body_ += quote(key);
		body_ += ':';
		body_ += encoded;
		return *this;
	}
	Object& str(std::string_view key, std::string_view value) { return raw(key, quote(value)); }
	Object& num(std::string_view key, double value) { return raw(key, number(value)); }
	Object& boolean(std::string_view key, bool value) { return raw(key, value ? "true" : "false"); }
	std::string build() const { return "{" + body_ + "}"; }

private:
	std::string body_;
};

inline std::string array(const std::vector<std::string>& encodedItems)
{
	std::string out = "[";
	for (size_t i = 0; i < encodedItems.size(); ++i) {
		if (i) out += ',';
		out += encodedItems[i];
	}
	return out + "]";
}

} // namespace pse::json
