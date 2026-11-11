#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace hello {

// The host hands this plugin small, already validated JSON objects whose interesting members
// are strings ({"name": "..."}, {"greeting": "..."}), so a tiny reader is enough and the plugin
// stays free of any library dependency.

// Returns the string members of a JSON object (other members are skipped), or nothing when the
// text is not a well-formed JSON object.
[[nodiscard]] std::optional<std::map<std::string, std::string>> stringMembers(std::string_view json);

// Returns `text` as a quoted JSON string literal.
[[nodiscard]] std::string quoted(std::string_view text);

}  // namespace hello
