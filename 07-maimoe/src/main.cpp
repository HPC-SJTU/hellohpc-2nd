#include "HostRuntime.hpp"
#include "TuneConfig.hpp"

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <string>
#include <stdexcept>

namespace {

[[nodiscard]] std::string environment_value(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* const value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

int run(int argc, char** argv) {
    std::filesystem::path config = "solution/KstroParam.toml";
    int positional = 0;
    std::string dataset;
    std::string output;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--config") {
            if (index + 1 >= argc) {
                std::cerr << "maimoe: --config requires a path argument\n";
                return 2;
            }
            config = argv[++index];
        } else if (positional == 0) {
            dataset = argument;
            ++positional;
        } else if (positional == 1) {
            output = argument;
            ++positional;
        } else {
            std::cerr << "usage: maimoe <dataset_dir> <output_dir> [--config <toml_path>]\n";
            return 2;
        }
    }
    if (positional != 2) {
        std::cerr << "usage: maimoe <dataset_dir> <output_dir> [--config <toml_path>]\n";
        return 2;
    }
    std::filesystem::path executable = std::filesystem::absolute(argv[0]);
    const std::string override_path = environment_value("MAIMOE_KERNEL_EXECUTABLE");
    if (!override_path.empty()) {
        executable = override_path;
    } else {
#if defined(_WIN32)
        executable.replace_filename("maimoe_kernel.exe");
#else
        executable.replace_filename("maimoe_kernel");
#endif
    }
    return maimoe::host::run(dataset, output, executable, maimoe::host::load_tune(config));
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "maimoe: " << error.what() << '\n';
        return 1;
    }
}
