// KDTreeSingleIndexDynamicAdaptor driven by a fuzzer-chosen sequence of operations: append points to
// the dataset and addPoints() them, removePoint(), re-add a removed point, kNN and radius queries.
#include <nanoflann.hpp>

#include <vector>

#include "fuzz_common.h"

namespace
{
constexpr size_t kMaxOps    = 96;
constexpr size_t kMaxPoints = 1024;

template <typename T, typename D, template <class, class, class, class> class Metric, int DIM>
void run(Reader& r)
{
    using Tree = nanoflann::KDTreeSingleIndexDynamicAdaptor<Metric<T, Cloud<T>, D, size_t>, Cloud<T>, DIM>;

    const size_t dim  = DIM > 0 ? static_cast<size_t>(DIM) : 1 + r.u8() % 4;
    const size_t leaf = 1 + r.u8() % 16;

    Cloud<T> cloud;
    cloud.dim = dim;
    Tree index(static_cast<int>(dim), cloud, nanoflann::KDTreeSingleIndexAdaptorParams(leaf), 4096);

    std::vector<bool> active;
    std::vector<T>    query(dim);

    for (size_t op = 0; op < kMaxOps && r.remaining(); op++)
    {
        switch (r.u8() % 5)
        {
            case 0:  // append a batch and index it
            {
                const size_t start = active.size();
                const size_t m     = 1 + r.u8() % 24;
                for (size_t i = 0; i < m && active.size() < kMaxPoints; i++)
                {
                    for (size_t d = 0; d < dim; d++) cloud.v.push_back(r.coord<T>());
                    active.push_back(true);
                }
                if (active.size() > start)
                    index.addPoints(static_cast<uint32_t>(start), static_cast<uint32_t>(active.size() - 1));
                break;
            }
            case 1:  // remove
            {
                const uint32_t i = r.u16();
                if (!active.empty())
                {
                    index.removePoint(i % active.size());
                    active[i % active.size()] = false;
                }
                break;
            }
            case 2:  // re-add a previously removed point
            {
                const size_t i = r.u16();
                if (!active.empty() && !active[i % active.size()])
                {
                    const uint32_t j = static_cast<uint32_t>(i % active.size());
                    index.addPoints(j, j);
                    active[j] = true;
                }
                break;
            }
            case 3:  // kNN
            {
                const size_t k = 1 + r.u8() % 8;
                for (auto& q : query) q = r.coord<T>();
                std::vector<size_t> idx(k);
                std::vector<D>      dst(k);
                nanoflann::KNNResultSet<D, size_t> rs(k);
                rs.init(idx.data(), dst.data());
                (void)index.findNeighbors(rs, query.data(), nanoflann::SearchParameters(0, true));
                // Documented: removePoint() removes the point from the set (treeIndex_ == -1), addPoints() indexes only valid dataset indices.
                for (size_t i = 0; i < rs.size(); i++)
                    if (idx[i] >= active.size() || !active[idx[i]]) __builtin_trap();
                break;
            }
            default:  // radius
            {
                const D radius = static_cast<D>(r.coord<T>());
                for (auto& q : query) q = r.coord<T>();
                std::vector<nanoflann::ResultItem<uint32_t, D>> found;
                nanoflann::RadiusResultSet<D, uint32_t> rs(radius, found);
                (void)index.findNeighbors(rs, query.data(), nanoflann::SearchParameters(0, r.u8() & 1));
                // Same documented guarantee as the kNN check above.
                for (const auto& f : found)
                    if (f.first >= active.size() || !active[f.first]) __builtin_trap();
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
