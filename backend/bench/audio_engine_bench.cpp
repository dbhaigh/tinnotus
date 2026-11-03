#include "audio_engine.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif
#endif

#if (defined(__i386__) || defined(__x86_64__)) && !defined(_MSC_VER)
#include <cpuid.h>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#include <time.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#ifndef TINNOTUS_BENCH_BUILD_CONFIG
#define TINNOTUS_BENCH_BUILD_CONFIG "unknown"
#endif

#ifndef TINNOTUS_BENCH_BASE_FLAGS
#define TINNOTUS_BENCH_BASE_FLAGS "unknown"
#endif

#ifndef TINNOTUS_BENCH_CONFIG_FLAGS
#define TINNOTUS_BENCH_CONFIG_FLAGS ""
#endif

#ifndef TINNOTUS_BENCH_ENGINE_SIMD
#define TINNOTUS_BENCH_ENGINE_SIMD "compiler-default SIMD"
#endif

#ifndef TINNOTUS_BENCH_SANITIZER_FLAGS
#define TINNOTUS_BENCH_SANITIZER_FLAGS "none"
#endif

namespace {

using BenchmarkClock = std::chrono::steady_clock;

constexpr std::array<int, 3> sample_rates{44100, 48000, 96000};
constexpr std::array<std::size_t, 5> channel_counts{1, 2, 4, 6, 8};
constexpr std::array<std::size_t, 5> notch_counts{0, 1, 2, 4, 8};
constexpr std::array<std::size_t, 6> block_sizes{32, 64, 128, 256, 512, 1024};
constexpr std::size_t case_count =
    sample_rates.size() * channel_counts.size() * notch_counts.size() * block_sizes.size();
constexpr std::chrono::milliseconds minimum_throughput_duration{50};
constexpr std::size_t throughput_batch_iterations = 1024;
constexpr std::size_t latency_sample_pass_count = 3;
constexpr std::size_t throughput_pass_count = 3;
constexpr double performance_budget_fraction = 0.25;

struct Options {
    std::size_t warmup_iterations = 300;
    std::size_t timed_iterations = 2000;
    std::filesystem::path output_directory = "docs/benchmarks";
    bool check_realtime_budget = false;
};

struct PerformanceBudgetCase {
    int sample_rate;
    std::size_t channels;
    std::size_t notches;
    std::size_t block_frames;
};

struct BenchmarkResult {
    int sample_rate = 0;
    std::size_t channels = 0;
    std::size_t notches = 0;
    std::size_t block_frames = 0;
    std::size_t throughput_iterations = 0;
    double frames_per_second = 0.0;
    double nanoseconds_per_frame = 0.0;
    double minimum_block_ns = 0.0;
    double average_block_ns = 0.0;
    double p99_block_ns = 0.0;
    double cpu_time_estimate_milliseconds = 0.0;
    double checksum = 0.0;
};

constexpr std::array<PerformanceBudgetCase, 3> performance_budget_cases{{
    {48000, 2, 1, 128},
    {96000, 8, 8, 32},
    {96000, 8, 8, 1024},
}};

struct ThroughputMeasurement {
    std::size_t iterations = 0;
    double frames_per_second = 0.0;
    double nanoseconds_per_frame = 0.0;
    double cpu_time_estimate_milliseconds = 0.0;
};

std::string trim(std::string value) {
    const auto first = std::find_if_not(
        value.begin(),
        value.end(),
        [](unsigned char character) { return std::isspace(character) != 0; }
    );
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(
        value.rbegin(),
        value.rend(),
        [](unsigned char character) { return std::isspace(character) != 0; }
    ).base();
    return std::string(first, last);
}

bool parse_size(const std::string& value, std::size_t& parsed, bool allow_zero) {
    if (value.empty() || value.front() == '-') {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const unsigned long long number = std::strtoull(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != '\0' ||
        (!allow_zero && number == 0) ||
        number > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    parsed = static_cast<std::size_t>(number);
    return true;
}

void print_usage(const char* executable) {
    std::cout << "Usage: " << executable
              << " [--warmup count] [--iterations count] [--output-dir path] "
                 "[--check-realtime-budget]\n"
              << "Defaults: --warmup 300 --iterations 2000 "
                 "--output-dir docs/benchmarks\n";
}

bool configure_benchmark_thread(
    std::string& affinity_description,
    std::string& error
) {
#if defined(_WIN32)
    DWORD_PTR process_affinity_mask = 0;
    DWORD_PTR system_affinity_mask = 0;
    if (!GetProcessAffinityMask(
            GetCurrentProcess(),
            &process_affinity_mask,
            &system_affinity_mask)) {
        error = "GetProcessAffinityMask failed with Windows error " +
                std::to_string(GetLastError());
        return false;
    }
    if (process_affinity_mask == 0) {
        error = "The process has no available logical processors";
        return false;
    }

    const DWORD_PTR selected_mask =
        process_affinity_mask & (~process_affinity_mask + 1);
    if (SetThreadAffinityMask(GetCurrentThread(), selected_mask) == 0) {
        error = "SetThreadAffinityMask failed with Windows error " +
                std::to_string(GetLastError());
        return false;
    }

    unsigned int logical_processor = 0;
    for (DWORD_PTR mask = selected_mask; (mask & 1) == 0; mask >>= 1) {
        ++logical_processor;
    }
    affinity_description =
        "pinned to logical processor " + std::to_string(logical_processor);
#else
    affinity_description = "managed by the operating system";
#endif
    return true;
}

bool parse_options(int argc, char* argv[], Options& options, bool& show_help) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            show_help = true;
            return true;
        }

        if (argument == "--check-realtime-budget") {
            options.check_realtime_budget = true;
            continue;
        }

        if (argument == "--warmup" || argument == "--iterations" ||
            argument == "--output-dir") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for " << argument << '\n';
                return false;
            }

            const std::string value(argv[++index]);
            if (argument == "--warmup") {
                if (!parse_size(value, options.warmup_iterations, true)) {
                    std::cerr << "--warmup must be a non-negative integer\n";
                    return false;
                }
            } else if (argument == "--iterations") {
                if (!parse_size(value, options.timed_iterations, false)) {
                    std::cerr << "--iterations must be a positive integer\n";
                    return false;
                }
            } else if (value.empty()) {
                std::cerr << "--output-dir must not be empty\n";
                return false;
            } else {
                options.output_directory = value;
            }
            continue;
        }

        std::cerr << "Unknown argument: " << argument << '\n';
        return false;
    }

    return true;
}

std::string architecture_name() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_ARM) || defined(__arm__)
    return "arm";
#else
    return "unknown-arch";
#endif
}

std::string operating_system() {
#if defined(_WIN32)
    return "Windows (" + architecture_name() + ")";
#elif defined(__linux__) || defined(__APPLE__)
    struct utsname information {};
    if (uname(&information) == 0) {
        return std::string(information.sysname) + " " + information.release +
               " (" + information.machine + ")";
    }
    return "Unix-like (" + architecture_name() + ")";
#else
    return "Unknown OS (" + architecture_name() + ")";
#endif
}

std::string cpu_model() {
    char brand[49] = {};

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int registers[4] = {};
    __cpuid(registers, static_cast<int>(0x80000000U));
    if (static_cast<unsigned int>(registers[0]) >= 0x80000004U) {
        for (int leaf = 0; leaf < 3; ++leaf) {
            __cpuid(
                registers,
                static_cast<int>(0x80000002U + static_cast<unsigned int>(leaf))
            );
            std::memcpy(brand + leaf * 16, registers, sizeof(registers));
        }
    }
#elif (defined(__i386__) || defined(__x86_64__)) && \
    (defined(__GNUC__) || defined(__clang__))
    if (__get_cpuid_max(0x80000000U, nullptr) >= 0x80000004U) {
        unsigned int registers[4] = {};
        for (unsigned int leaf = 0; leaf < 3; ++leaf) {
            __get_cpuid(0x80000002U + leaf,
                        &registers[0], &registers[1], &registers[2], &registers[3]);
            std::memcpy(brand + leaf * 16, registers, sizeof(registers));
        }
    }
#elif defined(__APPLE__)
    std::size_t length = 0;
    if (sysctlbyname("machdep.cpu.brand_string", nullptr, &length, nullptr, 0) == 0 &&
        length > 1 && length <= sizeof(brand) &&
        sysctlbyname(
            "machdep.cpu.brand_string",
            brand,
            &length,
            nullptr,
            0
        ) == 0) {
        brand[sizeof(brand) - 1] = '\0';
    }
#endif

    const std::string model = trim(brand);
    return model.empty() ? "Unavailable" : model;
}

std::string compiler_description() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(_MSC_VER)
    return "Microsoft Visual C++ " + std::to_string(_MSC_VER / 100) + "." +
           std::to_string(_MSC_VER % 100);
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "Unknown compiler";
#endif
}

std::string compiler_slug() {
#if defined(__clang__) && defined(_MSC_VER)
    return "clang-cl";
#elif defined(_MSC_VER)
    return "msvc";
#elif defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "compiler";
#endif
}

std::string platform_slug() {
#if defined(_WIN32)
    const std::string os = "windows";
#elif defined(__APPLE__)
    const std::string os = "macos";
#elif defined(__linux__)
    const std::string os = "linux";
#else
    const std::string os = "unknown";
#endif
    return os + "-" + architecture_name() + "-" + compiler_slug();
}

std::tm local_time(std::time_t timestamp) {
    std::tm value{};
#if defined(_WIN32)
    localtime_s(&value, &timestamp);
#else
    localtime_r(&timestamp, &value);
#endif
    return value;
}

std::string format_time(std::time_t timestamp, const char* format) {
    std::ostringstream result;
    const std::tm local = local_time(timestamp);
    result << std::put_time(&local, format);
    return result.str();
}

bool run_case(
    int sample_rate,
    std::size_t channels,
    std::size_t notches,
    std::size_t block_frames,
    const Options& options,
    BenchmarkResult& result,
    std::string& error
) {
    tinnotus::audio::AudioEngine engine(sample_rate, channels);
    if (!engine.isValid()) {
        error = "AudioEngine rejected a valid benchmark configuration";
        return false;
    }

    const std::size_t sample_count = block_frames * channels;
    std::vector<float> input(sample_count);
    std::vector<float> output(sample_count);
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t frame = 0; frame < block_frames; ++frame) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const double frequency = 220.0 + 31.0 * static_cast<double>(channel);
            const double phase =
                2.0 * pi * frequency * static_cast<double>(frame) / sample_rate;
            input[frame * channels + channel] = static_cast<float>(0.5 * std::sin(phase));
        }
    }

    for (std::size_t notch = 0; notch < notches; ++notch) {
        const double center_frequency = 650.0 + 1250.0 * static_cast<double>(notch);
        const double bandwidth = 100.0 + 10.0 * static_cast<double>(notch);
        if (engine.setNotch(notch, center_frequency, bandwidth) !=
            tinnotus::audio::RequestResult::queued) {
            error = "AudioEngine rejected a benchmark notch configuration";
            return false;
        }
    }
    if (!engine.process(nullptr, nullptr, 0)) {
        error = "AudioEngine failed to apply benchmark notch configuration";
        return false;
    }

    for (std::size_t iteration = 0; iteration < options.warmup_iterations; ++iteration) {
        if (!engine.process(input.data(), output.data(), block_frames)) {
            error = "AudioEngine failed during benchmark warm-up";
            return false;
        }
    }

    std::vector<std::uint64_t> block_latencies_ns;
    if (options.timed_iterations >
        std::numeric_limits<std::size_t>::max() / latency_sample_pass_count) {
        error = "Latency sample count overflowed";
        return false;
    }
    const std::size_t total_latency_samples =
        options.timed_iterations * latency_sample_pass_count;
    block_latencies_ns.reserve(total_latency_samples);
    for (std::size_t pass = 0; pass < latency_sample_pass_count; ++pass) {
        for (std::size_t iteration = 0; iteration < options.timed_iterations; ++iteration) {
            const auto start = BenchmarkClock::now();
            if (!engine.process(input.data(), output.data(), block_frames)) {
                error = "AudioEngine failed during latency sampling";
                return false;
                }
                const auto finish = BenchmarkClock::now();
                const std::uint64_t latency_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count()
                );
                block_latencies_ns.push_back(latency_ns);
            }
    }
    std::sort(block_latencies_ns.begin(), block_latencies_ns.end());
    const double latency_sum = std::accumulate(
            block_latencies_ns.begin(),
            block_latencies_ns.end(),
            0.0
    );
    const std::size_t p99_rank =
            (block_latencies_ns.size() * 99 + 99) / 100;

    const auto process_throughput_batch = [&](std::size_t iterations) {
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            if (!engine.process(input.data(), output.data(), block_frames)) {
                return false;
            }
        }
        return true;
    };

    std::array<ThroughputMeasurement, throughput_pass_count> throughput_measurements{};
    const std::size_t extension_iterations =
        std::max(options.timed_iterations, throughput_batch_iterations);

    for (ThroughputMeasurement& measurement : throughput_measurements) {
        std::size_t iterations = options.timed_iterations;
        const auto wall_start = BenchmarkClock::now();
        if (!process_throughput_batch(iterations)) {
            error = "AudioEngine failed during throughput measurement";
            return false;
        }

        auto wall_finish = BenchmarkClock::now();
        while (wall_finish - wall_start < minimum_throughput_duration) {
            if (iterations >
                std::numeric_limits<std::size_t>::max() - extension_iterations) {
                error = "Throughput measurement iteration count overflowed";
                return false;
            }
            if (!process_throughput_batch(extension_iterations)) {
                error = "AudioEngine failed during throughput measurement";
                return false;
            }
            iterations += extension_iterations;
            wall_finish = BenchmarkClock::now();
        }

        const double wall_ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                wall_finish - wall_start
            ).count()
        );
        const double total_frames = static_cast<double>(block_frames) * iterations;
        if (wall_ns <= 0.0) {
            error = "The steady clock returned a non-positive measurement interval";
            return false;
        }

        measurement.iterations = iterations;
        measurement.frames_per_second = total_frames * 1000000000.0 / wall_ns;
        measurement.nanoseconds_per_frame = wall_ns / total_frames;
        measurement.cpu_time_estimate_milliseconds = wall_ns / 1000000.0;
    }

    std::sort(
        throughput_measurements.begin(),
        throughput_measurements.end(),
        [](const ThroughputMeasurement& left, const ThroughputMeasurement& right) {
            return left.frames_per_second < right.frames_per_second;
        }
    );
    const ThroughputMeasurement& median_measurement =
        throughput_measurements[throughput_pass_count / 2];

    result.sample_rate = sample_rate;
    result.channels = channels;
    result.notches = notches;
    result.block_frames = block_frames;
    result.throughput_iterations = median_measurement.iterations;
    result.frames_per_second = median_measurement.frames_per_second;
    result.nanoseconds_per_frame = median_measurement.nanoseconds_per_frame;
    result.minimum_block_ns = static_cast<double>(block_latencies_ns.front());
    result.average_block_ns =
        latency_sum / static_cast<double>(block_latencies_ns.size());
    result.p99_block_ns = static_cast<double>(block_latencies_ns[p99_rank - 1]);
    result.cpu_time_estimate_milliseconds =
        median_measurement.cpu_time_estimate_milliseconds;
    result.checksum = std::accumulate(output.begin(), output.end(), 0.0);
    if (!std::isfinite(result.checksum)) {
        error = "The final benchmark output contains a non-finite checksum";
        return false;
    }

    return true;
}

bool run_realtime_budget_smoke(const Options& options, std::string& error) {
    std::cout << "Checking AudioEngine p99 block time against 25% of each block deadline\n";
    for (const PerformanceBudgetCase& benchmark_case : performance_budget_cases) {
        BenchmarkResult result;
        if (!run_case(
                benchmark_case.sample_rate,
                benchmark_case.channels,
                benchmark_case.notches,
                benchmark_case.block_frames,
                options,
                result,
                error)) {
            return false;
        }

        const double deadline_ns =
            static_cast<double>(benchmark_case.block_frames) * 1000000000.0 /
            static_cast<double>(benchmark_case.sample_rate);
        const double limit_ns = deadline_ns * performance_budget_fraction;
        if (!std::isfinite(result.p99_block_ns) || result.p99_block_ns > limit_ns) {
            std::ostringstream message;
            message << "AudioEngine p99 budget exceeded at "
                    << benchmark_case.sample_rate << " Hz, "
                    << benchmark_case.channels << " channels, "
                    << benchmark_case.notches << " notches, "
                    << benchmark_case.block_frames << " frames: "
                    << result.p99_block_ns / 1000.0 << " us p99 exceeds "
                    << limit_ns / 1000.0 << " us (25% of the "
                    << deadline_ns / 1000.0 << " us block deadline)";
            error = message.str();
            return false;
        }

        std::cout << "  " << benchmark_case.sample_rate << " Hz, "
                  << benchmark_case.channels << " channels, "
                  << benchmark_case.notches << " notches, "
                  << benchmark_case.block_frames << " frames: "
                  << std::fixed << std::setprecision(2)
                  << result.p99_block_ns / 1000.0 << " us p99 / "
                  << deadline_ns / 1000.0 << " us deadline ("
                  << result.p99_block_ns / deadline_ns * 100.0 << "%)\n";
    }
    return true;
}

bool write_report(
    const std::filesystem::path& report_path,
    const Options& options,
    const std::vector<BenchmarkResult>& results,
    const std::string& thread_affinity,
    std::string& error
) {
    std::ofstream report(report_path);
    if (!report) {
        error = "Could not open report file: " + report_path.string();
        return false;
    }

    const std::time_t now = std::time(nullptr);
    const std::string compiler_flags = trim(
        std::string(TINNOTUS_BENCH_BASE_FLAGS) + " " +
        TINNOTUS_BENCH_CONFIG_FLAGS
    );
    report << "# Tinnotus AudioEngine Benchmark\n\n"
           << "- Generated: " << format_time(now, "%Y-%m-%d %H:%M:%S") << "\n"
           << "- Platform: " << operating_system() << "\n"
           << "- CPU: " << cpu_model() << "\n"
           << "- Compiler: " << compiler_description() << "\n"
           << "- Build configuration: `" << TINNOTUS_BENCH_BUILD_CONFIG << "`\n"
           << "- Compiler flags: `" << compiler_flags << "`\n"
           << "- Audio engine SIMD mode: `" << TINNOTUS_BENCH_ENGINE_SIMD << "`\n"
           << "- Sanitizer flags: `" << TINNOTUS_BENCH_SANITIZER_FLAGS << "`\n"
           << "- Benchmark thread affinity: " << thread_affinity << "\n"
           << "- Warm-up: " << options.warmup_iterations << " blocks per case\n"
           << "- Latency samples: " << latency_sample_pass_count
           << " batches of " << options.timed_iterations << " blocks per case ("
           << latency_sample_pass_count * options.timed_iterations
           << " total samples).\n"
           << "- Throughput: " << throughput_pass_count
           << " independent passes, each with at least " << options.timed_iterations
           << " blocks and 50 ms; short passes are extended in batches of at least "
              "1,024 blocks.\n"
           << "- Throughput, ns/frame, and CPU-time estimate report the median-rate pass.\n"
           << "- Block min and average use all latency samples; p99 uses the "
              "nearest-rank percentile across all samples.\n"
           << "- CPU-time estimate is the wall-clock duration of the single-threaded, "
              "non-blocking processing pass; it includes scheduler delays.\n"
           << "- Frames/s counts audio frames, with channel count reported separately.\n"
           << "- Cases: " << results.size() << "\n\n"
           << "| Sample rate (Hz) | Channels | Active notches | Block (frames) "
              "| Throughput blocks | Frames/s | ns/frame | Block min (ns, all samples) "
              "| Block avg (ns, all samples) | Block p99 (ns, all samples) "
              "| CPU-time estimate (ms, throughput pass) "
              "| Output checksum |\n"
           << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";

    for (const BenchmarkResult& result : results) {
        report << "| " << result.sample_rate
               << " | " << result.channels
               << " | " << result.notches
               << " | " << result.block_frames
               << " | " << result.throughput_iterations
               << " | " << std::fixed << std::setprecision(0)
               << result.frames_per_second
               << " | " << std::setprecision(2) << result.nanoseconds_per_frame
               << " | " << std::setprecision(1) << result.minimum_block_ns
               << " | " << result.average_block_ns
               << " | " << result.p99_block_ns
               << " | " << std::setprecision(3)
               << result.cpu_time_estimate_milliseconds
               << " | " << std::setprecision(6) << result.checksum << " |\n";
    }

    if (!report) {
        error = "Failed while writing report file: " + report_path.string();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    Options options;
    bool show_help = false;
    if (!parse_options(argc, argv, options, show_help)) {
        print_usage(argv[0]);
        return 2;
    }
    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }

    std::string error;
    std::string thread_affinity;
    if (!configure_benchmark_thread(thread_affinity, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    if (options.check_realtime_budget) {
        if (!run_realtime_budget_smoke(options, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        return 0;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(options.output_directory, filesystem_error);
    if (filesystem_error) {
        std::cerr << "Could not create report directory: "
                  << filesystem_error.message() << '\n';
        return 1;
    }

    std::vector<BenchmarkResult> results;
    results.reserve(case_count);
    std::size_t completed_cases = 0;
    std::cout << "Benchmarking " << case_count << " configurations";
    std::cout.flush();
    for (int sample_rate : sample_rates) {
        for (std::size_t channels : channel_counts) {
            for (std::size_t notches : notch_counts) {
                for (std::size_t block_frames : block_sizes) {
                    BenchmarkResult result;
                    if (!run_case(
                            sample_rate,
                            channels,
                            notches,
                            block_frames,
                            options,
                            result,
                            error)) {
                        std::cerr << "\n" << error << '\n';
                        return 1;
                    }
                    results.push_back(result);
                    ++completed_cases;
                    if (completed_cases % 25 == 0) {
                        std::cout << '.';
                        std::cout.flush();
                    }
                }
            }
        }
    }
    std::cout << '\n';

    const std::time_t now = std::time(nullptr);
    const std::string report_name =
        format_time(now, "%Y-%m-%d") + "-" + platform_slug() + ".md";
    const std::filesystem::path report_path = options.output_directory / report_name;
    if (!write_report(report_path, options, results, thread_affinity, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout << "Wrote " << results.size() << " benchmark cases to "
              << report_path.string() << '\n';
    return 0;
}
