#include "MaiMoeEngine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {

[[nodiscard]] maimoe::Bytes read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::string data((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return maimoe::Bytes(data.begin(), data.end());
}

int run(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: maimoe_check <dataset_dir> <expected_file> <output_dir>\n";
        return 2;
    }
    const maimoe::Manifest manifest = maimoe::engine::load_manifest(argv[1], false);
    const maimoe::Bytes expected_bytes = read_bytes(argv[2]);
    const maimoe::engine::ExpectedFile expected =
        maimoe::engine::read_expected(expected_bytes);
    const maimoe::engine::OutputValidation validation =
        maimoe::engine::validate_output(manifest, expected, argv[3]);
    std::cout << "checked charts=" << manifest.chart_count
              << " valid=" << validation.valid_charts
              << " errors=" << validation.error_charts
              << " output_files=" << validation.output_files
              << " output_bytes=" << validation.output_bytes << '\n';
    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "maimoe_check: " << error.what() << '\n';
        return 1;
    }
}
