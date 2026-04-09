#include <cstdint>
#include <compare>

// Price represents a price
// on the orderbook.
struct Price {
    int64_t value;
    auto operator<=>(const Price& other) const = default;
};
