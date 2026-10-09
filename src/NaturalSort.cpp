#include "NaturalSort.h"

#include "Track.h"

namespace {

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

int compareFolded(const std::string &a, const std::string &b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            size_t ei = i, ej = j;
            while (ei < a.size() && isDigit(a[ei]))
                ++ei;
            while (ej < b.size() && isDigit(b[ej]))
                ++ej;
            // Compare digit runs by value: strip leading zeros, then length, then digits.
            size_t zi = i, zj = j;
            while (zi < ei - 1 && a[zi] == '0')
                ++zi;
            while (zj < ej - 1 && b[zj] == '0')
                ++zj;
            if (ei - zi != ej - zj)
                return ei - zi < ej - zj ? -1 : 1;
            const int digits = a.compare(zi, ei - zi, b, zj, ej - zj);
            if (digits != 0)
                return digits < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size())
        return i < a.size() ? 1 : -1;
    return 0;
}

} // namespace

namespace NaturalSort {

int compare(const std::string &a, const std::string &b)
{
    const int folded = compareFolded(Text::casefold(a), Text::casefold(b));
    if (folded != 0)
        return folded;
    const int raw = a.compare(b); // e.g. "a1" vs "a01": fall back to a stable order
    return raw < 0 ? -1 : (raw > 0 ? 1 : 0);
}

} // namespace NaturalSort
