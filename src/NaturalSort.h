#pragma once

#include <string>

namespace NaturalSort {

// "Track 2" before "Track 10", case-insensitively.
int compare(const std::string &a, const std::string &b);
inline bool less(const std::string &a, const std::string &b) { return compare(a, b) < 0; }

} // namespace NaturalSort
