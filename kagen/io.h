#pragma once

#include <string>
#include <unordered_map>

#include <mpi.h>

#include "kagen/context.h"
#include "kagen/definitions.h"
#include "kagen/io/graph_format.h"

namespace kagen {
class IOError : public std::exception {
public:
    IOError(std::string what) : _what(std::move(what)) {}

    const char* what() const noexcept override {
        return _what.c_str();
    }

private:
    std::string _what;
};

const std::unordered_map<FileFormat, std::unique_ptr<FileFormatFactory>>& GetGraphFormatFactories();

const std::unique_ptr<FileFormatFactory>& GetGraphFormatFactory(FileFormat format);

std::unique_ptr<GraphReader>
CreateGraphReader(const std::string& filename, const InputGraphConfig& config, PEID rank, PEID size);

std::unique_ptr<GraphReader>
CreateGraphReader(const FileFormat format, const InputGraphConfig& config, PEID rank, PEID size);

void WriteGraph(GraphWriter& writer, const OutputGraphConfig& config, bool output, MPI_Comm comm);
} // namespace kagen
