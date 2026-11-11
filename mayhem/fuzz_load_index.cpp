// loadIndex() on untrusted bytes: a dataset is rebuilt deterministically from the first bytes of the
// input, the remainder is fed to loadIndex() of the static and incremental adaptors, and a query runs
// on whatever tree was accepted. A rejected stream (std::runtime_error, the documented error) is
// never added to the corpus.
//
// The dataset adaptor is total: a corrupt stream can name a point index outside the dataset, and
// nanoflann hands that index to kdtree_get_pt() (the library only requires the adaptor to return the
// idx-th point, loadIndex()'s precondition being a stream written by saveIndex() for this very
// dataset). Returning 0 there keeps such reads out of the harness's own frames; whatever the library
// itself then reads out of bounds (stream-supplied split axis, bounding-box length, node-map index)
// is reported in nanoflann frames.
#include <nanoflann.hpp>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fuzz_common.h"

namespace
{
template <typename T>
struct LoadCloud : Cloud<T>
{
    T kdtree_get_pt(const size_t idx, const size_t d) const
    {
        const size_t i = idx * this->dim + d;
        return d < this->dim && i < this->v.size() ? this->v[i] : T(0);
    }
};

template <typename Tree, typename T, typename D>
void query_tree(const Tree& index, const LoadCloud<T>& cloud)
{
    std::vector<T> q(cloud.dim, T(0));
    for (size_t d = 0; d < cloud.dim && !cloud.v.empty(); d++) q[d] = cloud.v[d];
    uint32_t idx[4];
    D        dst[4];
    try
    {
        (void)index.knnSearch(q.data(), 4, idx, dst);
        std::vector<nanoflann::ResultItem<uint32_t, D>> found;
        (void)index.radiusSearch(q.data(), static_cast<D>(64), found);
    }
    catch (const std::runtime_error&)
    {
        // findNeighbors() documents this error for an index that was never built; loadIndex()
        // accepts an empty node array alongside a non-zero point count.
    }
}

template <typename T, typename D, template <class, class, class, class> class Metric, int DIM>
bool run_static(size_t dim, size_t n, const std::string& blob)
{
    using Tree = nanoflann::KDTreeSingleIndexAdaptor<Metric<T, LoadCloud<T>, D, size_t>, LoadCloud<T>, DIM>;
    LoadCloud<T> cloud;
    make_cloud(cloud, DIM > 0 ? static_cast<size_t>(DIM) : dim, n);
    Tree index(
        static_cast<typename Tree::Dimension>(cloud.dim), cloud,
        nanoflann::KDTreeSingleIndexAdaptorParams(
            10, nanoflann::KDTreeSingleIndexAdaptorFlags::SkipInitialBuildIndex));
    std::istringstream in(blob);
    try
    {
        index.loadIndex(in);
    }
    catch (const std::runtime_error&)
    {
        return false;
    }
    query_tree<Tree, T, D>(index, cloud);
    return true;
}

template <typename T, typename D, int DIM>
bool run_incremental(size_t dim, size_t n, const std::string& blob)
{
    using Tree = nanoflann::KDTreeSingleIndexIncrementalAdaptor<
        nanoflann::L2_Simple_Adaptor<T, LoadCloud<T>, D, size_t>, LoadCloud<T>, DIM>;
    LoadCloud<T> cloud;
    make_cloud(cloud, DIM > 0 ? static_cast<size_t>(DIM) : dim, n);
    Tree index(static_cast<typename Tree::Dimension>(cloud.dim), cloud);
    std::istringstream in(blob);
    try
    {
        index.loadIndex(in);
    }
    catch (const std::runtime_error&)
    {
        return false;
    }
    query_tree<Tree, T, D>(index, cloud);
    return true;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 4) return -1;
    Reader       r(data, size);
    const unsigned variant = r.u8() % 4;
    const size_t   n       = r.u8() % 65;
    const size_t   dim     = 1 + r.u8() % 5;
    r.u8();
    const std::string blob(reinterpret_cast<const char*>(r.p), r.n);

    bool ok = false;
    switch (variant)
    {
        case 0: ok = run_static<double, double, nanoflann::L2_Simple_Adaptor, 3>(dim, n, blob); break;
        case 1: ok = run_static<float, float, nanoflann::L1_Adaptor, -1>(dim, n, blob); break;
        case 2: ok = run_static<uint8_t, int32_t, nanoflann::L2_Adaptor, -1>(dim, n, blob); break;
        default: ok = run_incremental<double, double, 3>(dim, n, blob); break;
    }
    return ok ? 0 : -1;
}
