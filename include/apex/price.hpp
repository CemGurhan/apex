#include <cstdint>

// Price represents a price
// on the orderbook.
struct Price {
    int64_t value;

    bool operator==(const Price& other) const {
        return value == other.value;
    }
    bool operator<(const Price& other) const {
        return value < other.value;
    }
    bool operator>(const Price& other) const {
        return other < *this;
    }
    bool operator<=(const Price& other) const {
        return !(other < *this);
    }
    bool operator>=(const Price& other) const {
        return !(*this < other);
    }
    bool operator!=(const Price& other) const {
        return !(*this == other);
    }
};
