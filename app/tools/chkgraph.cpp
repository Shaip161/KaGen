#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

#include <mpi.h>

#include "../CLI11.h"
#include "kagen/context.h"
#include "kagen/definitions.h"
#include "kagen/io.h"
#include "kagen/tools/postprocessor.h"
#include "kagen/tools/validator.h"

using namespace kagen;

namespace {
std::pair<SInt, SInt> ComputeRange(const SInt n, const PEID size, const PEID rank) {
    const SInt chunk = n / size;
    const SInt rem   = n % size;
    const SInt from  = rank * chunk + std::min<SInt>(rank, rem);
    const SInt to    = std::min<SInt>(from + ((static_cast<SInt>(rank) < rem) ? chunk + 1 : chunk), n);
    return {from, to};
}
} // namespace

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    PEID rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    InputGraphConfig config;
    config.width = 64;

    bool quiet                           = false;
    bool warn_64bits                     = false;
    bool no_warn_self_loops              = false;
    bool no_warn_directed                = false;
    bool no_warn_multi_edges             = false;
    bool no_warn_negative_edge_weights   = false;
    bool no_warn_negative_vertex_weights = false;

    CLI::App app("chkgraph");
    app.add_option("format", config.format)->transform(CLI::CheckedTransformer(GetInputFormatMap()))->required();
    app.add_option("input graph", config.filename, "Input graph")->check(CLI::ExistingFile)->required();
    app.add_flag("-q,--quiet", quiet, "Suppress any output to stdout.");
    app.add_flag("--W64bit", warn_64bits, "Warn if the graph requires 64 bit ID or weight types.")
        ->capture_default_str();
    app.add_flag("--Wno-self-loops", no_warn_self_loops, "Do not warn if the graph contains self loops.")
        ->capture_default_str();
    app.add_flag("--Wno-directed", no_warn_directed, "Do not warn if the graph misses some reverse edges.")
        ->capture_default_str();
    app.add_flag("--Wno-multi-edges", no_warn_multi_edges, "Do not warn if the graph contains multi edges.")
        ->capture_default_str();
    app.add_flag(
           "--Wno-negative-edge-weights", no_warn_negative_edge_weights,
           "Do not warn if the graph contains negative edge weights.")
        ->capture_default_str();
    app.add_flag(
           "--Wno-negative-vertex-weights", no_warn_negative_vertex_weights,
           "Do not warn if the graph contains negative vertex weights.")
        ->capture_default_str();
    CLI11_PARSE(app, argc, argv);

    auto reader = CreateGraphReader(config.format, config, rank, size);

    if (!quiet && rank == 0) {
        std::cout << "Reading graph from " << config.filename << ", format: " << config.format << " ..." << std::endl;
    }

    SInt  n, m;
    Graph graph;
    try {
        std::tie(n, m)        = reader->ReadSize();
        const auto [from, to] = ComputeRange(n, size, rank);
        graph = reader->Read(from, to, std::numeric_limits<SInt>::max(), GraphRepresentation::EDGE_LIST);

        if (reader->Deficits() & ReaderDeficits::REQUIRES_REDISTRIBUTION) {
            if (!quiet && rank == 0) {
                std::cout << "Redistributing graph for parallel processing ..." << std::endl;
            }

            SInt actual_n = 0;
            for (const auto& [u, v]: graph.edges) {
                actual_n = std::max(n, std::max(u, v));
            }
            MPI_Allreduce(MPI_IN_PLACE, &actual_n, 1, KAGEN_MPI_SINT, MPI_MAX, MPI_COMM_WORLD);
            ++n;

            // Create desired vertex distribution
            std::tie(graph.vertex_range.first, graph.vertex_range.second) = ComputeRange(actual_n, size, rank);
            AddReverseEdgesAndRedistribute(graph.edges, graph.vertex_range, false, MPI_COMM_WORLD);
        }
    } catch (const IOError& e) {
        if (!quiet) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        std::exit(1);
    }

    bool has_warned = false;

    SSInt total_node_weight = 0;
    SSInt total_edge_weight = 0;

    const bool has_edge_weights   = !graph.edge_weights.empty();
    const bool has_vertex_weights = !graph.vertex_weights.empty();

    if (!quiet && rank == 0) {
        std::cout << "Reading successful, graph information:" << std::endl;
        std::cout << "Number of vertices: " << n << " " << (has_vertex_weights ? "(weighted)" : "(unweighted)")
                  << std::endl;
        std::cout << "Number of edges:    " << m << " " << (has_edge_weights ? "(weighted)" : "(unweighted)")
                  << std::endl;
    }

    if (warn_64bits
        && (n > std::numeric_limits<std::uint32_t>::max() || m > std::numeric_limits<std::uint32_t>::max())) {
        if (!quiet) {
            std::cerr << "Warning: the graph has too many vertices or edges for 32 bit data types\n";
        }
        has_warned = true;
    }

    if (has_vertex_weights) {
        for (SInt node = 0; node < graph.vertex_range.second - graph.vertex_range.first; ++node) {
            if (!no_warn_negative_vertex_weights && graph.vertex_weights[node] < 0) {
                if (!quiet) {
                    std::cerr << "Warning: weight of vertex " << node + 1
                              << " is negative (skipping remaining vertices)\n";
                }
                has_warned = true;
                break;
            }
            total_node_weight += graph.vertex_weights[node];
        }
        if (warn_64bits && total_node_weight > std::numeric_limits<std::int32_t>::max()) {
            if (!quiet) {
                std::cerr << "Warning: total weight of all vertices is too large for 32 bit data types\n";
            }
            has_warned = true;
        }
    }

    if (has_edge_weights) {
        for (SInt edge = 0; edge < graph.edges.size(); ++edge) {
            if (!no_warn_negative_edge_weights && graph.edge_weights[edge] < 0) {
                if (!quiet) {
                    std::cerr << "Warning: weight of edge " << graph.edges[edge].first << " -> "
                              << graph.edges[edge].second << " is negative (skipping remaining edges)\n";
                }
                has_warned = true;
                break;
            }
        }
    }

    has_warned |= !ValidateGraph(graph, no_warn_self_loops, no_warn_directed, no_warn_multi_edges, MPI_COMM_WORLD);
    MPI_Allreduce(MPI_IN_PLACE, &has_warned, 1, MPI_C_BOOL, MPI_LOR, MPI_COMM_WORLD);

    if (!has_warned && !quiet && rank == 0) {
        std::cout << "Graph OK" << std::endl;
    }

    if (has_warned) {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    MPI_Finalize();
    return has_warned;
}

