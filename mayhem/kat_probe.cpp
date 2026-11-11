// Known-answer probe for mayhem/test.sh: a fixed 5-point cloud, fixed query, exact expected output.
#include <nanoflann.hpp>

#include <cstdio>
#include <vector>

struct Pts
{
    std::vector<double> v;
    size_t kdtree_get_point_count() const { return v.size() / 3; }
    double kdtree_get_pt(size_t i, size_t d) const { return v[i * 3 + d]; }
    template <class B>
    bool kdtree_get_bbox(B&) const { return false; }
};

int main()
{
    Pts p;
    p.v = {0, 0, 0, 1, 0, 0, 0, 2, 0, 3, 3, 3, 1, 1, 1};
    using Tree = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<double, Pts>, Pts, 3>;
    Tree         index(3, p, nanoflann::KDTreeSingleIndexAdaptorParams(2));
    const double q[3] = {0.9, 0.1, 0.0};

    uint32_t i3[3];
    double d3[3];
    const size_t n = index.knnSearch(q, 3, i3, d3);
    std::printf("nn=%u d=%.4f\n", i3[0], d3[0]);
    std::printf("knn=%zu:%u,%u,%u\n", n, i3[0], i3[1], i3[2]);

    std::vector<nanoflann::ResultItem<uint32_t, double>> found;
    const size_t cnt = index.radiusSearch(q, 2.0, found);
    std::printf("radius_count=%zu last=%u d=%.4f\n", cnt, found.back().first, found.back().second);
    return 0;
}
