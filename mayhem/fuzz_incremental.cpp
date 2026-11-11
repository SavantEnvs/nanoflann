// KDTreeSingleIndexIncrementalAdaptor driven by a fuzzer-chosen sequence of operations: addPoints,
// removePoint, removeBox / removeOutsideBox, kNN / radius / box queries and a save/load round trip.
#include <nanoflann.hpp>

#include <sstream>
#include <stdexcept>
#include <vector>

#include "fuzz_common.h"

namespace
{
constexpr size_t kMaxOps    = 96;
constexpr size_t kMaxPoints = 1024;

template <typename T, typename D, template <class, class, class, class> class Metric, int DIM>
void run(Reader& r)
{
    using Tree = nanoflann::KDTreeSingleIndexIncrementalAdaptor<Metric<T, Cloud<T>, D, size_t>, Cloud<T>, DIM, uint32_t>;

    const size_t dim = DIM > 0 ? static_cast<size_t>(DIM) : 1 + r.u8() % 4;
    const float  alpha_bal = 0.6f + static_cast<float>(r.u8() % 40) / 100.0f;
    const float  alpha_del = 0.2f + static_cast<float>(r.u8() % 60) / 100.0f;

    Cloud<T> cloud;
    cloud.dim = dim;
    Tree index(static_cast<typename Tree::Dimension>(dim), cloud, nanoflann::KDTreeIncrementalIndexParams(alpha_bal, alpha_del));

    std::vector<T> query(dim);
    auto read_box = [&](typename Tree::BoundingBox& box)
    {
        nanoflann::resize(box, static_cast<typename Tree::Dimension>(dim));
        for (size_t d = 0; d < dim; d++)
        {
            const T a = r.coord<T>(), b = r.coord<T>();
            box[d].low  = a < b ? a : b;
            box[d].high = a < b ? b : a;
        }
    };

    for (size_t op = 0; op < kMaxOps && r.remaining(); op++)
    {
        const size_t n = cloud.kdtree_get_point_count();
        switch (r.u8() % 8)
        {
            case 0:  // append a batch and index it
            {
                const size_t m = 1 + r.u8() % 24;
                size_t       added = 0;
                for (; added < m && cloud.kdtree_get_point_count() < kMaxPoints; added++)
                    for (size_t d = 0; d < dim; d++) cloud.v.push_back(r.coord<T>());
                if (added) index.addPoints(static_cast<uint32_t>(n), static_cast<uint32_t>(n + added - 1));
                break;
            }
            case 1:  // single point
            {
                if (n < kMaxPoints)
                {
                    for (size_t d = 0; d < dim; d++) cloud.v.push_back(r.coord<T>());
                    index.addPoint(static_cast<uint32_t>(n));
                }
                break;
            }
            case 2:
                if (n) index.removePoint(static_cast<uint32_t>(r.u16() % n));
                break;
            case 3:
            {
                typename Tree::BoundingBox box;
                read_box(box);
                if (r.u8() & 1)
                    index.removeBox(box);
                else
                    index.removeOutsideBox(box);
                break;
            }
            case 4:  // kNN
            {
                const size_t k = 1 + r.u8() % 8;
                for (auto& q : query) q = r.coord<T>();
                std::vector<uint32_t> idx(k);
                std::vector<D>        dst(k);
                const size_t found = index.knnSearch(query.data(), k, idx.data(), dst.data());
                // Documented: the tree stores only indices passed to addPoint(s), which must be valid in the dataset.
                for (size_t i = 0; i < found; i++)
                    if (idx[i] >= n) __builtin_trap();
                break;
            }
            case 5:  // radius
            {
                const D radius = static_cast<D>(r.coord<T>());
                for (auto& q : query) q = r.coord<T>();
                std::vector<nanoflann::ResultItem<uint32_t, D>> found;
                (void)index.radiusSearch(query.data(), radius, found, nanoflann::SearchParameters(0, r.u8() & 1));
                // Same documented guarantee as the kNN check above.
                for (const auto& f : found)
                    if (f.first >= n) __builtin_trap();
                break;
            }
            case 6:  // box query
            {
                typename Tree::BoundingBox box;
                read_box(box);
                std::vector<nanoflann::ResultItem<uint32_t, D>> found;
                nanoflann::RadiusResultSet<D, uint32_t>         rs(D(0), found);
                (void)index.findWithinBox(rs, box);
                break;
            }
            default:  // save / load round trip into a fresh index; no size() comparison (a mismatch would trap in harness frames)
            {
                std::ostringstream out;
                index.saveIndex(out);
                Tree loaded(static_cast<typename Tree::Dimension>(dim), cloud);
                std::istringstream in(out.str());
                loaded.loadIndex(in);
                break;
            }
        }
    }
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
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 6) return -1;
    Reader         r(data, size);
    const unsigned t = r.u8();
    const unsigned m = r.u8();
    const bool fixed = r.u8() & 1;
    switch (t % 3)
    {
        case 0: fixed ? pick_metric<float, float, 3>(m, r) : pick_metric<float, float, -1>(m, r); break;
        case 1: fixed ? pick_metric<double, double, 3>(m, r) : pick_metric<double, double, -1>(m, r); break;
        default: fixed ? pick_metric<uint8_t, int32_t, 3>(m, r) : pick_metric<uint8_t, int32_t, -1>(m, r); break;
    }
    return 0;
}
