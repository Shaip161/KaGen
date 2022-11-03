#include "kagen.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "kagen/context.h"
#include "kagen/facade.h"

namespace kagen {
namespace {}

KaGen::KaGen(MPI_Comm comm) : comm_(comm), config_(std::make_unique<PGeneratorConfig>()) {
    SetDefaults();
}

KaGen::KaGen(KaGen&&) noexcept            = default;
KaGen& KaGen::operator=(KaGen&&) noexcept = default;

KaGen::~KaGen() = default;

void KaGen::SetSeed(const int seed) {
    config_->seed = seed;
}

void KaGen::EnableUndirectedGraphVerification() {
    config_->validate_simple_graph = true;
}

void KaGen::EnableBasicStatistics() {
    config_->statistics_level = StatisticsLevel::BASIC;
    config_->quiet            = false;
}

void KaGen::EnableAdvancedStatistics() {
    config_->statistics_level = StatisticsLevel::ADVANCED;
    config_->quiet            = false;
}

void KaGen::EnableOutput(const bool header) {
    config_->quiet        = false;
    config_->print_header = header;
}

void KaGen::UseHPFloats(const bool state) {
    config_->hp_floats = state ? 1 : -1;
}

void KaGen::SetNumberOfChunks(const SInt k) {
    config_->k = k;
}

namespace {
using Options = std::unordered_map<std::string, std::string>;

// Parse a string such as 'key1=value1;key2=value2;key3;key4'
Options ParseOptionString(const std::string& options) {
    std::istringstream toker(options);
    std::string        pair;
    Options            result;

    while (std::getline(toker, pair, ';')) {
        const auto eq_pos = pair.find('=');
        if (eq_pos == std::string::npos) { // No equal sign -> Option is a flag
            result[pair] = "1";
        } else {
            const std::string key   = pair.substr(0, eq_pos);
            const std::string value = pair.substr(eq_pos + 1);
            result[key]             = value;
        }
    }

    return result;
}

auto GenericGenerateFromOptionString(const std::string& options_str, PGeneratorConfig& config, MPI_Comm comm) {
    auto options = ParseOptionString(options_str);

    const std::string type_str = options["type"];
    const auto        type_map = GetGeneratorTypeMap();
    const auto        type_it  = type_map.find(type_str);
    if (type_it == type_map.end()) {
        throw std::runtime_error("invalid generator type");
    }

    auto get_sint_or_default = [&](const std::string& option, const SInt default_value = 0) {
        const auto it = options.find(option);
        return (it == options.end() ? default_value : std::stoll(it->second));
    };
    auto get_hpfloat_or_default = [&](const std::string& option, const HPFloat default_value = 0.0) {
        const auto it = options.find(option);
        return (it == options.end() ? default_value : std::stod(it->second));
    };
    auto get_bool_or_default = [&](const std::string& option, const bool default_value = false) {
        const auto it = options.find(option);
        return (
            it == options.end() ? default_value : (it->second == "1" || it->second == "true" || it->second == "yes"));
    };

    config.generator   = type_it->second;
    config.n           = get_sint_or_default("n", 1ull << get_sint_or_default("N"));
    config.m           = get_sint_or_default("m", 1ull << get_sint_or_default("M"));
    config.k           = get_sint_or_default("k");
    config.p           = get_hpfloat_or_default("prob");
    config.r           = get_hpfloat_or_default("radius");
    config.plexp       = get_hpfloat_or_default("gamma");
    config.periodic    = get_bool_or_default("periodic");
    config.avg_degree  = get_hpfloat_or_default("avg_degree");
    config.min_degree  = get_sint_or_default("min_degree");
    config.grid_x      = get_sint_or_default("grid_x");
    config.grid_y      = get_sint_or_default("grid_y");
    config.grid_z      = get_sint_or_default("grid_z");
    config.rmat_a      = get_sint_or_default("rmat_a");
    config.rmat_b      = get_sint_or_default("rmat_b");
    config.rmat_c      = get_sint_or_default("rmat_c");
    config.coordinates = get_bool_or_default("coordinates");

    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateFromOptionString(const std::string& options) {
    return GenericGenerateFromOptionString(options, *config_, comm_);
}

KaGenResult2D KaGen::GenerateFromOptionString2D(const std::string& options) {
    return GenericGenerateFromOptionString(options + ";coordinates", *config_, comm_);
}

KaGenResult3D KaGen::GenerateFromOptionString3D(const std::string& options) {
    return GenericGenerateFromOptionString(options + ";coordinates", *config_, comm_);
}

KaGenResult KaGen::GenerateDirectedGNM(const SInt n, const SInt m, const bool self_loops) {
    config_->generator  = GeneratorType::GNM_DIRECTED;
    config_->n          = n;
    config_->m          = m;
    config_->self_loops = self_loops;
    return Generate(*config_, comm_);
}

KaGenResult KaGen::GenerateUndirectedGNM(const SInt n, const SInt m, const bool self_loops) {
    config_->generator  = GeneratorType::GNM_UNDIRECTED;
    config_->n          = n;
    config_->m          = m;
    config_->self_loops = self_loops;
    return Generate(*config_, comm_);
}

KaGenResult KaGen::GenerateDirectedGNP(const SInt n, const LPFloat p, const bool self_loops) {
    config_->generator  = GeneratorType::GNP_DIRECTED;
    config_->n          = n;
    config_->p          = p;
    config_->self_loops = self_loops;
    return Generate(*config_, comm_);
}

KaGenResult KaGen::GenerateUndirectedGNP(const SInt n, const LPFloat p, const bool self_loops) {
    config_->generator  = GeneratorType::GNP_UNDIRECTED;
    config_->n          = n;
    config_->p          = p;
    config_->self_loops = self_loops;
    return Generate(*config_, comm_);
}

namespace {
KaGenResult2D GenerateRGG2D_Impl(
    PGeneratorConfig& config, const SInt n, const SInt m, const LPFloat r, const bool coordinates, MPI_Comm comm) {
    config.generator   = GeneratorType::RGG_2D;
    config.n           = n;
    config.m           = m;
    config.r           = r;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateRGG2D(const SInt n, const LPFloat r) {
    return GenerateRGG2D_Impl(*config_, n, 0, r, false, comm_);
}

KaGenResult KaGen::GenerateRGG2D_NM(const SInt n, const SInt m) {
    return GenerateRGG2D_Impl(*config_, n, m, 0.0, false, comm_);
}

KaGenResult KaGen::GenerateRGG2D_MR(const SInt m, const LPFloat r) {
    return GenerateRGG2D_Impl(*config_, 0, m, r, false, comm_);
}

KaGenResult2D KaGen::GenerateRGG2D_Coordinates(const SInt n, const LPFloat r) {
    return GenerateRGG2D_Impl(*config_, n, 0, r, true, comm_);
}

namespace {
KaGenResult3D GenerateRGG3D_Impl(
    PGeneratorConfig& config, const SInt n, const SInt m, const LPFloat r, const bool coordinates, MPI_Comm comm) {
    config.generator   = GeneratorType::RGG_3D;
    config.m           = m;
    config.n           = n;
    config.r           = r;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateRGG3D(const SInt n, const LPFloat r) {
    return GenerateRGG3D_Impl(*config_, n, 0, r, false, comm_);
}

KaGenResult KaGen::GenerateRGG3D_NM(const SInt n, const SInt m) {
    return GenerateRGG3D_Impl(*config_, n, m, 0.0, false, comm_);
}

KaGenResult KaGen::GenerateRGG3D_MR(const SInt m, const LPFloat r) {
    return GenerateRGG3D_Impl(*config_, 0, m, r, false, comm_);
}

KaGenResult3D KaGen::GenerateRGG3D_Coordinates(const SInt n, const LPFloat r) {
    return GenerateRGG3D_Impl(*config_, n, 0, r, true, comm_);
}

#ifdef KAGEN_CGAL_FOUND
namespace {
KaGenResult2D GenerateRDG2D_Impl(
    PGeneratorConfig& config, const SInt n, const SInt m, const bool periodic, const bool coordinates, MPI_Comm comm) {
    config.generator   = GeneratorType::RDG_2D;
    config.n           = n;
    config.m           = m;
    config.periodic    = periodic;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateRDG2D(const SInt n, const bool periodic) {
    return GenerateRDG2D_Impl(*config_, n, 0, periodic, false, comm_);
}

KaGenResult KaGen::GenerateRDG2D_M(const SInt m, const bool periodic) {
    return GenerateRDG2D_Impl(*config_, 0, m, periodic, false, comm_);
}

KaGenResult2D KaGen::GenerateRDG2D_Coordinates(const SInt n, const bool periodic) {
    return GenerateRDG2D_Impl(*config_, n, 0, periodic, true, comm_);
}

namespace {
KaGenResult3D
GenerateRDG3D_Impl(PGeneratorConfig& config, const SInt n, const SInt m, const bool coordinates, MPI_Comm comm) {
    config.generator   = GeneratorType::RDG_3D;
    config.n           = n;
    config.m           = m;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateRDG3D(const SInt n) {
    return GenerateRDG3D_Impl(*config_, n, 0, false, comm_);
}

KaGenResult KaGen::GenerateRDG3D_M(const SInt m) {
    return GenerateRDG3D_Impl(*config_, 0, m, false, comm_);
}

KaGenResult3D KaGen::GenerateRDG3D_Coordinates(const SInt n) {
    return GenerateRDG3D_Impl(*config_, n, 0, true, comm_);
}
#else  // KAGEN_CGAL_FOUND
KaGenResult KaGen::GenerateRDG2D(SInt, bool) {
    throw std::runtime_error("Library was compiled without CGAL. Thus, delaunay generators are not available.");
}

KaGenResult KaGen::GenerateRDG2D_M(SInt, bool) {
    throw std::runtime_error("Library was compiled without CGAL. Thus, delaunay generators are not available.");
}

KaGenResult2D KaGen::GenerateRDG2D_Coordinates(SInt, bool) {
    throw std::runtime_error("Library was compiled without CGAL. Thus, delaunay generators are not available.");
}

KaGenResult KaGen::GenerateRDG3D(SInt) {
    throw std::runtime_error("Library was compiled without CGAL. Thus, delaunay generators are not available.");
}

KaGenResult KaGen::GenerateRDG3D_M(SInt) {
    throw std::runtime_error("Library was compiled without CGAL. Thus, delaunay generators are not available.");
}

KaGenResult3D KaGen::GenerateRDG3D_Coordinates(SInt) {
    throw std::runtime_error("Library was compiled without CGAL. Thus, delaunay generators are not available.");
}
#endif // KAGEN_CGAL_FOUND

namespace {
KaGenResult GenerateBA_Impl(
    PGeneratorConfig& config, const SInt n, const SInt m, const SInt d, const bool directed, const bool self_loops,
    MPI_Comm comm) {
    config.generator  = GeneratorType::BA;
    config.m          = m;
    config.n          = n;
    config.min_degree = d;
    config.self_loops = self_loops;
    config.directed   = directed;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateBA(const SInt n, const SInt d, const bool directed, const bool self_loops) {
    return GenerateBA_Impl(*config_, n, 0, d, directed, self_loops, comm_);
}

KaGenResult KaGen::GenerateBA_NM(const SInt n, const SInt m, const bool directed, const bool self_loops) {
    return GenerateBA_Impl(*config_, n, m, 0.0, directed, self_loops, comm_);
}

KaGenResult KaGen::GenerateBA_MD(const SInt m, const SInt d, const bool directed, const bool self_loops) {
    return GenerateBA_Impl(*config_, 0, m, d, directed, self_loops, comm_);
}

namespace {
KaGenResult2D GenerateRHG_Impl(
    PGeneratorConfig& config, const LPFloat gamma, const SInt n, const SInt m, const LPFloat d, const bool coordinates,
    MPI_Comm comm) {
    config.generator   = GeneratorType::RHG;
    config.n           = n;
    config.m           = m;
    config.avg_degree  = d;
    config.plexp       = gamma;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateRHG(const LPFloat gamma, const SInt n, const LPFloat d) {
    return GenerateRHG_Impl(*config_, gamma, n, 0, d, false, comm_);
}

KaGenResult KaGen::GenerateRHG_NM(const LPFloat gamma, const SInt n, const SInt m) {
    return GenerateRHG_Impl(*config_, gamma, n, m, 0.0, false, comm_);
}

KaGenResult KaGen::GenerateRHG_MD(const LPFloat gamma, const SInt m, const LPFloat d) {
    return GenerateRHG_Impl(*config_, gamma, 0, m, d, false, comm_);
}

KaGenResult2D KaGen::GenerateRHG_Coordinates(const LPFloat gamma, const SInt n, const LPFloat d) {
    return GenerateRHG_Impl(*config_, gamma, n, 0, d, true, comm_);
}

KaGenResult2D KaGen::GenerateRHG_Coordinates_NM(const LPFloat gamma, const SInt n, const SInt m) {
    return GenerateRHG_Impl(*config_, gamma, n, m, 0.0, true, comm_);
}

KaGenResult2D KaGen::GenerateRHG_Coordinates_MD(const LPFloat gamma, const SInt m, const LPFloat d) {
    return GenerateRHG_Impl(*config_, gamma, 0, m, d, true, comm_);
}

namespace {
KaGenResult2D GenerateGrid2D_Impl(
    PGeneratorConfig& config, const SInt grid_x, const SInt grid_y, const LPFloat p, const SInt m, const bool periodic,
    const bool coordinates, MPI_Comm comm) {
    config.generator   = GeneratorType::GRID_2D;
    config.grid_x      = grid_x;
    config.grid_y      = grid_y;
    config.p           = p;
    config.m           = m;
    config.periodic    = periodic;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult KaGen::GenerateGrid2D(const SInt grid_x, const SInt grid_y, const LPFloat p, const bool periodic) {
    return GenerateGrid2D_Impl(*config_, grid_x, grid_y, p, 0, periodic, false, comm_);
}

KaGenResult KaGen::GenerateGrid2D_N(const SInt n, const LPFloat p, const bool periodic) {
    const SInt sqrt_n = std::sqrt(n);
    return GenerateGrid2D(sqrt_n, sqrt_n, p, periodic);
}

KaGenResult KaGen::GenerateGrid2D_NM(const SInt n, const SInt m, const bool periodic) {
    const SInt sqrt_n = std::sqrt(n);
    return GenerateGrid2D_Impl(*config_, sqrt_n, sqrt_n, 0.0, m, periodic, false, comm_);
}

KaGenResult2D
KaGen::GenerateGrid2D_Coordinates(const SInt grid_x, const SInt grid_y, const LPFloat p, const bool periodic) {
    return GenerateGrid2D_Impl(*config_, grid_x, grid_y, p, 0, periodic, true, comm_);
}

namespace {
KaGenResult3D GenerateGrid3D_Impl(
    PGeneratorConfig& config, const SInt grid_x, const SInt grid_y, const SInt grid_z, const LPFloat p, const SInt m,
    const bool periodic, const bool coordinates, MPI_Comm comm) {
    config.generator   = GeneratorType::GRID_3D;
    config.grid_x      = grid_x;
    config.grid_y      = grid_y;
    config.grid_z      = grid_z;
    config.p           = p;
    config.m           = m;
    config.periodic    = periodic;
    config.coordinates = coordinates;
    return Generate(config, comm);
}
} // namespace

KaGenResult
KaGen::GenerateGrid3D(const SInt grid_x, const SInt grid_y, const SInt grid_z, const LPFloat p, const bool periodic) {
    return GenerateGrid3D_Impl(*config_, grid_x, grid_y, grid_z, p, 0, periodic, false, comm_);
}

KaGenResult KaGen::GenerateGrid3D_N(const SInt n, const LPFloat p, const bool periodic) {
    const SInt cbrt_n = std::cbrt(n);
    return GenerateGrid3D(cbrt_n, cbrt_n, cbrt_n, p, periodic);
}

KaGenResult KaGen::GenerateGrid3D_NM(const SInt n, const SInt m, const bool periodic) {
    const SInt cbrt_n = std::cbrt(n);
    return GenerateGrid3D_Impl(*config_, cbrt_n, cbrt_n, cbrt_n, 0.0, m, periodic, false, comm_);
}

KaGenResult3D KaGen::GenerateGrid3D_Coordinates(
    const SInt grid_x, const SInt grid_y, const SInt grid_z, const LPFloat p, const bool periodic) {
    return GenerateGrid3D_Impl(*config_, grid_x, grid_y, grid_z, p, 0, periodic, true, comm_);
}

KaGenResult KaGen::GenerateKronecker(const SInt n, const SInt m, const bool directed, const bool self_loops) {
    config_->generator  = GeneratorType::KRONECKER;
    config_->n          = n;
    config_->m          = m;
    config_->directed   = directed;
    config_->self_loops = self_loops;
    return Generate(*config_, comm_);
}

KaGenResult KaGen::GenerateRMAT(
    const SInt n, const SInt m, const LPFloat a, const LPFloat b, const LPFloat c, const bool directed,
    const bool self_loops) {
    config_->generator  = GeneratorType::RMAT;
    config_->n          = n;
    config_->m          = m;
    config_->rmat_a     = a;
    config_->rmat_b     = b;
    config_->rmat_c     = c;
    config_->directed   = directed;
    config_->self_loops = self_loops;
    return Generate(*config_, comm_);
}

void KaGen::SetDefaults() {
    config_->quiet         = true;
    config_->output_format = OutputFormat::NONE; // ignored anyways
    // keep all other defaults
}
} // namespace kagen
