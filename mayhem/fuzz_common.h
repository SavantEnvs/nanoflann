// Shared helpers for the nanoflann harnesses: a byte reader and a flat point-cloud dataset adaptor.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

struct Reader
{
    const uint8_t* p;
    size_t         n;
    Reader(const uint8_t* d, size_t s) : p(d), n(s) {}
    size_t remaining() const { return n; }
    uint8_t u8()
    {
        if (!n) return 0;
        n--;
        return *p++;
    }
    uint16_t u16()
    {
        const uint16_t a = u8();
        return static_cast<uint16_t>(a | (static_cast<uint16_t>(u8()) << 8));
    }
    uint32_t u32()
    {
        const uint32_t a = u16();
        return a | (static_cast<uint32_t>(u16()) << 16);
    }
    // One coordinate: floating types are decoded from an int32 / 256 (always finite, exactly
    // representable), integer types are the raw bytes.
    template <typename T>
    T coord()
    {
        if constexpr (std::is_floating_point<T>::value)
            return static_cast<T>(static_cast<int32_t>(u32())) / static_cast<T>(256);
        else if constexpr (sizeof(T) == 1)
            return static_cast<T>(u8());
        else
            return static_cast<T>(u16());
    }
};

template <typename T>
struct Cloud
{
    using coord_t = T;
    size_t         dim = 3;
    std::vector<T> v;

    size_t kdtree_get_point_count() const { return dim ? v.size() / dim : 0; }
    T      kdtree_get_pt(const size_t idx, const size_t d) const { return v[idx * dim + d]; }
    template <class BBOX>
    bool kdtree_get_bbox(BBOX&) const
    {
        return false;
    }
};

// Deterministic cloud of n points so a saved index can be reloaded against "the same dataset".
template <typename T>
inline void make_cloud(Cloud<T>& c, size_t dim, size_t n)
{
    c.dim = dim;
    c.v.resize(n * dim);
    uint32_t s = static_cast<uint32_t>(n) * 2654435761u + 12345u;
    for (auto& x : c.v)
    {
        s = s * 1664525u + 1013904223u;
        const int32_t k = static_cast<int32_t>(s >> 16) % 4096;
        x = static_cast<T>(static_cast<double>(k) / 16.0);
    }
}
