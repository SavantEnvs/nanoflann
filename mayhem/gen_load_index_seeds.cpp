// Writes the starter seeds in mayhem/fuzz_load_index/testsuite/: [variant][n][dim][pad] + saveIndex()
// output over the same deterministic cloud the harness rebuilds. Not part of the build; run by hand
// (and re-run when upstream's NANOFLANN_VERSION changes, since the stream embeds it):
//   clang++ -std=c++17 -Iinclude -Imayhem mayhem/gen_load_index_seeds.cpp -o /tmp/gen && /tmp/gen mayhem/fuzz_load_index/testsuite
#include <nanoflann.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "fuzz_common.h"

static void emit(const std::string& dir, const std::string& name, unsigned variant, unsigned n, unsigned dim, const std::string& blob)
{
    std::ofstream f(dir + "/" + name, std::ios::binary);
    const char    hdr[4] = {static_cast<char>(variant), static_cast<char>(n), static_cast<char>(dim - 1), 0};
    f.write(hdr, 4);
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
}

template <typename T, typename D, template <class, class, class, class> class M, int DIM>
static std::string save_static(size_t dim, size_t n)
{
    using Tree = nanoflann::KDTreeSingleIndexAdaptor<M<T, Cloud<T>, D, size_t>, Cloud<T>, DIM>;
    Cloud<T> c;
    make_cloud(c, DIM > 0 ? static_cast<size_t>(DIM) : dim, n);
    Tree idx(static_cast<typename Tree::Dimension>(c.dim), c, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    std::ostringstream o;
    idx.saveIndex(o);
    return o.str();
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    emit(dir, "static_d3_n40", 0, 40, 3, save_static<double, double, nanoflann::L2_Simple_Adaptor, 3>(3, 40));
    emit(dir, "static_d3_n0", 0, 0, 3, save_static<double, double, nanoflann::L2_Simple_Adaptor, 3>(3, 0));
    emit(dir, "static_f_dyn_n33", 1, 33, 4, save_static<float, float, nanoflann::L1_Adaptor, -1>(4, 33));
    emit(dir, "static_u8_dyn_n20", 2, 20, 2, save_static<uint8_t, int32_t, nanoflann::L2_Adaptor, -1>(2, 20));
    {
        using Tree = nanoflann::KDTreeSingleIndexIncrementalAdaptor<
            nanoflann::L2_Simple_Adaptor<double, Cloud<double>, double, size_t>, Cloud<double>, 3>;
        Cloud<double> c;
        make_cloud(c, 3, 30);
        Tree idx(3, c);
        idx.addPoints(0, 29);
        idx.removePoint(7);
        std::ostringstream o;
        idx.saveIndex(o);
        emit(dir, "incremental_d3_n30", 3, 30, 3, o.str());
    }
    return 0;
}
