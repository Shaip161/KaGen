#pragma once

#include <mpi.h>

#include "kagen/definitions.h"
#include "kagen/io/graph_format.h"
#include "kagen/io/mmap_toker.h"
#include "kagen/io/seq_graph_writer.h"

namespace kagen {
class EdgelistWriter : public SequentialGraphWriter {
public:
    EdgelistWriter(bool header, bool directed, const OutputGraphConfig& config, Graph& graph, MPI_Comm comm);

protected:
    int Requirements() const final;

    void AppendHeaderTo(const std::string& filename, SInt n, SInt m) final;

    void AppendTo(const std::string& filename) final;

private:
    bool header_;
    bool directed_;
};

class EdgelistFactory : public FileFormatFactory {
public:
    std::string DefaultExtension() const final {
        return "edgelist";
    }

    std::unique_ptr<GraphWriter> CreateWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm) const final;
};

class UndirectedEdgelistFactory : public FileFormatFactory {
public:
    std::string DefaultExtension() const final {
        return "edgelist";
    }

    std::unique_ptr<GraphWriter> CreateWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm) const final;
};

class BinaryEdgelistWriter : public SequentialGraphWriter {
public:
    BinaryEdgelistWriter(
        bool header, bool directed, int datatype_size, const OutputGraphConfig& config, Graph& graph, MPI_Comm comm);

protected:
    int Requirements() const final;

    void AppendHeaderTo(const std::string& filename, SInt n, SInt m) final;

    void AppendTo(const std::string& filename) final;

private:
    bool header_;
    bool directed_;
    int  datatype_size_;
};

class BinaryEdgelistFactory : public FileFormatFactory {
public:
    std::string DefaultExtension() const final {
        return "binary-edgelist";
    }

    std::unique_ptr<GraphWriter> CreateWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm) const final;
};

class UndirectedBinaryEdgelistFactory : public FileFormatFactory {
public:
    std::string DefaultExtension() const final {
        return "undirected-binary-edgelist";
    }

    std::unique_ptr<GraphWriter> CreateWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm) const final;
};

class XtrapulpFactory : public FileFormatFactory {
public:
    std::string DefaultExtension() const final {
        return "xtrapulp";
    }

    std::unique_ptr<GraphWriter> CreateWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm) const final;
};

class PlainEdgelistReader : public GraphReader {
public:
    PlainEdgelistReader(const std::string& filename, PEID rank, PEID size);

    GraphSize ReadSize() final;

    Graph Read(SInt from_vertex, SInt to_vertex, SInt to_edge, GraphRepresentation representation) final;

    SInt FindNodeByEdge(SInt edge) final;

    int Deficits() const final;

private:
    MappedFileToker toker_;
    PEID            rank_;
    PEID            size_;
};

class PlainEdgelistWriter : public SequentialGraphWriter {
public:
    PlainEdgelistWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm);

protected:
    void AppendHeaderTo(const std::string& filename, SInt n, SInt m) final;

    void AppendTo(const std::string& filename) final;
};

class PlainEdgelistFactory : public FileFormatFactory {
public:
    std::string DefaultExtension() const final {
        return "plain-edgelist";
    }

    std::unique_ptr<GraphReader> CreateReader(const InputGraphConfig& config, PEID rank, PEID size) const final;

    std::unique_ptr<GraphWriter> CreateWriter(const OutputGraphConfig& config, Graph& graph, MPI_Comm comm) const final;
};
} // namespace kagen
