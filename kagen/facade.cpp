#include "kagen/facade.h"

#include <cmath>
#include <iomanip>
#include <iostream>

#include <mpi.h>

#include "kagen/context.h"
#include "kagen/generators/generator.h"
#include "kagen/tools/statistics.h"

// Generators
#include "kagen/generators/barabassi/barabassi.h"
#include "kagen/generators/geometric/rgg.h"
#include "kagen/generators/gnm/gnm_directed.h"
#include "kagen/generators/gnm/gnm_undirected.h"
#include "kagen/generators/gnp/gnp_directed.h"
#include "kagen/generators/gnp/gnp_undirected.h"
#include "kagen/generators/grid/grid_2d.h"
#include "kagen/generators/grid/grid_3d.h"
#include "kagen/generators/hyperbolic/hyperbolic.h"
#include "kagen/generators/image/image_mesh.h"
#include "kagen/generators/kronecker/kronecker.h"
#include "kagen/generators/rmat/rmat.h"

#ifdef KAGEN_CGAL_FOUND
    #include "kagen/generators/geometric/delaunay.h"
#endif // KAGEN_CGAL_FOUND

#include "kagen/tools/postprocessor.h"
#include "kagen/tools/validator.h"

namespace kagen {
std::unique_ptr<GeneratorFactory> CreateGeneratorFactory(const GeneratorType type) {
    switch (type) {
        case GeneratorType::GNM_DIRECTED:
            return std::make_unique<GNMDirectedFactory>();

        case GeneratorType::GNM_UNDIRECTED:
            return std::make_unique<GNMUndirectedFactory>();

        case GeneratorType::GNP_DIRECTED:
            return std::make_unique<GNPDirectedFactory>();

        case GeneratorType::GNP_UNDIRECTED:
            return std::make_unique<GNPUndirectedFactory>();

        case GeneratorType::RGG_2D:
            return std::make_unique<RGG2DFactory>();

        case GeneratorType::RGG_3D:
            return std::make_unique<RGG3DFactory>();

#ifdef KAGEN_CGAL_FOUND
        case GeneratorType::RDG_2D:
            return std::make_unique<Delaunay2DFactory>();

        case GeneratorType::RDG_3D:
            return std::make_unique<Delaunay3DFactory>();
#endif // KAGEN_CGAL_FOUND

        case GeneratorType::GRID_2D:
            return std::make_unique<Grid2DFactory>();

        case GeneratorType::GRID_3D:
            return std::make_unique<Grid3DFactory>();

        case GeneratorType::BA:
            return std::make_unique<BarabassiFactory>();

        case GeneratorType::KRONECKER:
            return std::make_unique<KroneckerFactory>();

        case GeneratorType::RHG:
            return std::make_unique<HyperbolicFactory>();

        case GeneratorType::RMAT:
            return std::make_unique<RMATFactory>();

        case GeneratorType::IMAGE_MESH:
            return std::make_unique<ImageMeshFactory>();
    }

    __builtin_unreachable();
}

namespace {
void PrintHeader(const PGeneratorConfig& config) {
    std::cout << "###############################################################################\n";
    std::cout << "#                         _  __      ____                                     #\n";
    std::cout << "#                        | |/ /__ _ / ___| ___ _ __                           #\n";
    std::cout << "#                        | ' // _` | |  _ / _ \\ '_ \\                          #\n";
    std::cout << "#                        | . \\ (_| | |_| |  __/ | | |                         #\n";
    std::cout << "#                        |_|\\_\\__,_|\\____|\\___|_| |_|                         #\n";
    std::cout << "#                         Karlsruhe Graph Generation                          #\n";
    std::cout << "#                                                                             #\n";
    std::cout << "###############################################################################\n";
    std::cout << config;
}
} // namespace

Graph Generate(const PGeneratorConfig& config_template, MPI_Comm comm) {
    PEID rank, size;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    const bool output_error = rank == ROOT;
    const bool output_info  = rank == ROOT && !config_template.quiet;

    if (output_info && config_template.print_header) {
        PrintHeader(config_template);
    }

    auto             factory = CreateGeneratorFactory(config_template.generator);
    PGeneratorConfig config;
    try {
        config = factory->NormalizeParameters(config_template, size, output_info);
    } catch (ConfigurationError& ex) {
        if (output_error) {
            std::cerr << "Error: " << ex.what() << "\n";
        }
        std::exit(1);
    }

    // Generate graph
    if (output_info) {
        std::cout << "Generating graph ..." << std::endl;
    }

    const auto start_graphgen = MPI_Wtime();

    auto generator = factory->Create(config, rank, size);
    generator->Generate();

    const SInt num_edges_before_finalize = generator->GetEdges().size();
    if (!config.skip_postprocessing) {
        if (output_info) {
            std::cout << "Finalizing ..." << std::endl;
        }
        generator->Finalize(comm);
    }
    const SInt num_edges_after_finalize = generator->GetEdges().size();

    const auto end_graphgen = MPI_Wtime();

    if (!config.skip_postprocessing && !config.quiet) {
        SInt num_global_edges_before, num_global_edges_after;
        MPI_Reduce(&num_edges_before_finalize, &num_global_edges_before, 1, KAGEN_MPI_SINT, MPI_SUM, ROOT, comm);
        MPI_Reduce(&num_edges_after_finalize, &num_global_edges_after, 1, KAGEN_MPI_SINT, MPI_SUM, ROOT, comm);

        if (output_info) {
            std::cout << "  Finalizing changed number of global edges changed from " << num_global_edges_before
                      << " to " << num_global_edges_after << " edges: by "
                      << std::abs(
                             static_cast<SSInt>(num_global_edges_after) - static_cast<SSInt>(num_global_edges_before))
                      << std::endl;
        }
    }

    auto graph = generator->TakeResult();

    // Validation
    if (config.validate_simple_graph) {
        if (output_info) {
            std::cout << "Validating simple graph ... " << std::flush;
        }

        bool success = ValidateSimpleGraph(graph.edges, graph.vertex_range, comm);
        MPI_Allreduce(MPI_IN_PLACE, &success, 1, MPI_C_BOOL, MPI_LOR, comm);
        if (!success) {
            if (output_error) {
                std::cerr << "Error: simple graph validation failed\n";
            }
            std::exit(1);
        } else if (output_info) {
            std::cout << "ok" << std::endl;
        }
    }

    // Statistics
    if (!config.quiet) {
        // Running time
        if (output_info) {
            std::cout << "Generation took " << std::fixed << std::setprecision(3) << end_graphgen - start_graphgen
                      << " seconds" << std::endl;
        }

        if (config.statistics_level >= StatisticsLevel::BASIC) {
            // Basic graph statistics
            PrintBasicStatistics(graph.edges, graph.vertex_range, rank == ROOT, comm);
        }
        if (config.statistics_level >= StatisticsLevel::ADVANCED) {
            PrintAdvancedStatistics(graph.edges, graph.vertex_range, rank == ROOT, comm);
        }
    }

    return graph;
}
} // namespace kagen
