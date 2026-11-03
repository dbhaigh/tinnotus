#include "audio_engine.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool get_stress_duration(std::chrono::seconds& duration) {
    constexpr long default_seconds = 30;
    constexpr long max_seconds = 120;
#if defined(_MSC_VER)
    char* configured_buffer = nullptr;
    std::size_t configured_buffer_size = 0;
    if (_dupenv_s(
            &configured_buffer,
            &configured_buffer_size,
            "TINNOTUS_AUDIO_STRESS_SECONDS") != 0) {
        std::cerr << "Could not read TINNOTUS_AUDIO_STRESS_SECONDS\n";
        return false;
    }
    const char* configured_seconds = configured_buffer;
#else
    const char* configured_seconds = std::getenv("TINNOTUS_AUDIO_STRESS_SECONDS");
#endif
    if (configured_seconds == nullptr) {
        duration = std::chrono::seconds(default_seconds);
#if defined(_MSC_VER)
        std::free(configured_buffer);
#endif
        return true;
    }

    errno = 0;
    char* end = nullptr;
    const long seconds = std::strtol(configured_seconds, &end, 10);
    const bool valid = errno != ERANGE && end != configured_seconds &&
                       *end == '\0' && seconds >= 1 && seconds <= max_seconds;
#if defined(_MSC_VER)
    std::free(configured_buffer);
#endif
    if (!valid) {
        std::cerr << "TINNOTUS_AUDIO_STRESS_SECONDS must be an integer from 1 to "
                  << max_seconds << '\n';
        return false;
    }

    duration = std::chrono::seconds(seconds);
    return true;
}

void test_spsc_buffer_long_duration(std::chrono::seconds duration) {
    constexpr std::size_t capacity = 257;
    constexpr std::size_t max_block_size = 127;
    constexpr std::uint64_t sequence_mask = (1u << 20) - 1;
    tinnotus::audio::SpscAudioBuffer buffer(capacity);
    std::atomic<bool> start{false};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> invalid_availability{false};
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t produced_samples = 0;
    std::uint64_t consumed_samples = 0;

    std::thread producer([&] {
        std::array<float, max_block_size> block{};
        std::mt19937 random(0x51A7u);
        std::uniform_int_distribution<std::size_t> block_size(1, max_block_size);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!failed.load(std::memory_order_relaxed) &&
               std::chrono::steady_clock::now() < deadline) {
            const std::size_t count = block_size(random);
            for (std::size_t i = 0; i < count; ++i) {
                block[i] = static_cast<float>((produced_samples + i) & sequence_mask);
            }

            if (buffer.tryWrite(block.data(), count)) {
                produced_samples += count;
            } else {
                std::this_thread::yield();
            }

            if (buffer.available() > buffer.capacity()) {
                invalid_availability.store(true, std::memory_order_relaxed);
                failed.store(true, std::memory_order_relaxed);
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::array<float, max_block_size> block{};
        std::mt19937 random(0xC0FFEEu);
        std::uniform_int_distribution<std::size_t> read_size(1, max_block_size);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!failed.load(std::memory_order_relaxed)) {
            const std::size_t count = buffer.tryRead(block.data(), read_size(random));
            for (std::size_t i = 0; i < count; ++i) {
                const float expected =
                    static_cast<float>((consumed_samples + i) & sequence_mask);
                if (block[i] != expected) {
                    failed.store(true, std::memory_order_relaxed);
                    break;
                }
            }
            consumed_samples += count;

            if (buffer.available() > buffer.capacity()) {
                invalid_availability.store(true, std::memory_order_relaxed);
                failed.store(true, std::memory_order_relaxed);
            }

            if (failed.load(std::memory_order_relaxed)) {
                break;
            }
            if (count == 0) {
                if (producer_done.load(std::memory_order_acquire) &&
                    buffer.available() == 0) {
                    break;
                }
                std::this_thread::yield();
            }
        }
    });

    deadline = std::chrono::steady_clock::now() + duration;
    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    check(!failed.load(std::memory_order_relaxed),
          "SPSC stress transfer contains no corrupted samples");
    check(!invalid_availability.load(std::memory_order_relaxed),
          "SPSC available() stays within its capacity during concurrent access");
    check(produced_samples > capacity,
          "SPSC stress test transfers enough samples to wrap repeatedly");
    check(consumed_samples == produced_samples,
          "SPSC stress test consumes every successfully written sample");
    check(buffer.available() == 0, "SPSC stress test drains the buffer before exit");
}

void test_command_queue_long_duration(std::chrono::seconds duration) {
    constexpr std::size_t channels = 2;
    constexpr std::size_t frame_count = 64;
    tinnotus::audio::AudioEngine engine(48000, channels);
    std::atomic<bool> start{false};
    std::atomic<bool> producer_done{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> invalid_result{false};
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t attempts = 0;
    std::uint64_t queued_commands = 0;
    std::uint64_t full_queue_results = 0;
    std::uint64_t processed_blocks = 0;
    std::array<float, frame_count * channels> input{};
    std::array<float, frame_count * channels> output{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>((static_cast<int>(i % 17) - 8) / 16.0);
    }

    std::thread control_thread([&] {
        std::mt19937 random(0xA0D10u);
        std::uniform_int_distribution<int> operation(0, 1);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!failed.load(std::memory_order_relaxed) &&
               std::chrono::steady_clock::now() < deadline) {
            const std::size_t index =
                static_cast<std::size_t>(attempts % tinnotus::audio::AudioEngine::max_notches);
            const tinnotus::audio::RequestResult result = operation(random) == 0
                ? engine.setNotch(
                      index,
                      500.0 + static_cast<double>(attempts % 10000),
                      25.0 + static_cast<double>(attempts % 400))
                : engine.removeNotch(index);

            ++attempts;
            if (result == tinnotus::audio::RequestResult::queued) {
                ++queued_commands;
            } else if (result == tinnotus::audio::RequestResult::queue_full) {
                ++full_queue_results;
                std::this_thread::yield();
            } else {
                invalid_result.store(true, std::memory_order_relaxed);
                failed.store(true, std::memory_order_relaxed);
            }
        }

        producer_done.store(true, std::memory_order_release);
    });

    std::thread audio_thread([&] {
        const auto process_block = [&] {
            if (!engine.process(input.data(), output.data(), frame_count)) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            ++processed_blocks;
            for (float sample : output) {
                if (!std::isfinite(sample)) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        };

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!producer_done.load(std::memory_order_acquire) &&
               !failed.load(std::memory_order_relaxed)) {
            process_block();
            if ((processed_blocks & 0x7Fu) == 0) {
                std::this_thread::yield();
            }
        }

        if (!failed.load(std::memory_order_relaxed)) {
            process_block();
        }
    });

    deadline = std::chrono::steady_clock::now() + duration;
    start.store(true, std::memory_order_release);
    control_thread.join();
    audio_thread.join();

    check(!failed.load(std::memory_order_relaxed),
          "command queue stress processing succeeds with finite output");
    check(!invalid_result.load(std::memory_order_relaxed),
          "command queue stress requests are valid or explicitly report queue_full");
    check(attempts > 0 && queued_commands > 0,
          "command queue stress test submits configuration commands");
    check(queued_commands + full_queue_results == attempts,
          "command queue reports every attempted request as queued or full");
    check(processed_blocks > 0,
          "audio thread processes blocks while control commands are submitted");

    check(engine.process(nullptr, nullptr, 0),
          "command queue drains pending commands after stress threads stop");
    for (std::size_t index = 0;
         index < tinnotus::audio::AudioEngine::max_notches;
         ++index) {
        check(engine.removeNotch(index) == tinnotus::audio::RequestResult::queued,
              "command queue accepts commands after concurrent draining");
    }
    check(engine.process(nullptr, nullptr, 0),
          "audio engine applies final command queue drain");
    constexpr std::size_t transition_frames = 48000 * 20 / 1000;
    std::vector<float> transition_input(transition_frames * channels, 0.0f);
    std::vector<float> transition_output(transition_input.size(), 0.0f);
    check(engine.process(
              transition_input.data(),
              transition_output.data(),
              transition_frames
          ),
          "audio engine completes the final removal transitions");

    const std::array<float, 8> probe_input{
        0.25f, -0.5f, 0.75f, -1.0f, 0.125f, -0.25f, 0.5f, -0.75f
    };
    std::array<float, probe_input.size()> probe_output{};
    check(engine.process(probe_input.data(), probe_output.data(), 4),
          "audio engine processes after the concurrent queue stress");
    bool passthrough = true;
    for (std::size_t i = 0; i < probe_input.size(); ++i) {
        passthrough &= probe_output[i] == probe_input[i];
    }
    check(passthrough,
          "final remove commands are applied in order and restore passthrough");
}

} // namespace

int main() {
    std::chrono::seconds duration;
    if (!get_stress_duration(duration)) {
        return 2;
    }

    test_spsc_buffer_long_duration(duration);
    test_command_queue_long_duration(duration);

    if (failures != 0) {
        std::cerr << failures << " stress test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All Tinnotus audio-engine stress tests passed ("
              << duration.count() << " seconds per concurrent test)\n";
    return 0;
}
