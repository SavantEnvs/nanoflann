// Build a static KDTreeSingleIndexAdaptor over fuzzer-chosen points and run kNN, radius and
// radius-bounded kNN queries, across element types, metrics and fixed/dynamic dimensionality.
#include <nanoflann.hpp>

#include <vector>

#include "fuzz_common.h"

namespace
{
constexpr size_t kMaxPoints = 512;

template <typename T, typename D, template <class, class, class, class> class Metric, int DIM>
void run(Reader& r)
{
    using Dist = Metric<T, Cloud<T>, D, size_t>;
    using Tree = nanoflann::KDTreeSingleIndexAdaptor<Dist, Cloud<T>, DIM>;

    const size_t dim  = DIM > 0 ? static_cast<size_t>(DIM) : 1 + r.u8() % 6;
    const size_t leaf = 1 + r.u8() % 24;
    const size_t k    = r.u8() % 12;
    const float  eps  = static_cast<float>(r.u8() % 4) / 2.0f;
    const bool   sorted = r.u8() & 1;
    const D      radius = static_cast<D>(r.coord<T>());

    std::vector<T> query(dim);
    for (auto& q : query) q = r.coord<T>();

    Cloud<T> cloud;
    cloud.dim = dim;
    while (cloud.v.size() / dim < kMaxPoints && r.remaining() >= dim * (sizeof(T) < 4 ? sizeof(T) : 4))
        for (size_t d = 0; d < dim; d++) cloud.v.push_back(r.coord<T>());

    Tree index(static_cast<typename Tree::Dimension>(dim), cloud, nanoflann::KDTreeSingleIndexAdaptorParams(leaf));

    std::vector<uint32_t> idx(k + 1);
    std::vector<D>      dst(k + 1);
    const size_t        nk = index.knnSearch(query.data(), k, idx.data(), dst.data());
    if (nk > k) __builtin_trap();

    std::vector<nanoflann::ResultItem<uint32_t, D>> found;
    nanoflann::SearchParameters sp(eps, sorted);
    (void)index.radiusSearch(query.data(), radius, found, sp);

    const size_t nr = index.rknnSearch(query.data(), k, idx.data(), dst.data(), radius);
    if (nr > k) __builtin_trap();
}

template <typename T, typename D, int DIM>
void pick_metric(unsigned m, Reader& r)
{
    switch (m % 3)
    {
        case 0: run<T, D, nanoflann::L2_Adaptor, DIM>(r); break;
        case 1: run<T, D, nanoflann::L2_Simple_Adaptor, DIM>(r); break;
        default: run<T, D, nanoflann::L1_Adaptor, DIM>(r); break;
    }
}

template <typename T, typename D>
void pick_dim(bool fixed, unsigned m, Reader& r)
{
    if (fixed)
        pick_metric<T, D, 3>(m, r);
    else
        pick_metric<T, D, -1>(m, r);
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 8) return -1;
    Reader         r(data, size);
    const unsigned t     = r.u8();
    const unsigned m     = r.u8();
    const bool     fixed = r.u8() & 1;
    switch (t % 4)
    {
        case 0: pick_dim<float, float>(fixed, m, r); break;
        case 1: pick_dim<double, double>(fixed, m, r); break;
        case 2: pick_dim<uint8_t, int32_t>(fixed, m, r); break;
        default: pick_dim<int16_t, int64_t>(fixed, m, r); break;
    }
    return 0;
}
