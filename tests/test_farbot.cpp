#include <catch2/catch_test_macros.hpp>
// farbot's headers expect these to be included first.
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

#include <farbot/RealtimeObject.hpp>
#include <farbot/fifo.hpp>

TEST_CASE("The vendored farbot fifo carries values across two threads", "[farbot][thread]") {
    using namespace farbot;
    fifo<int, fifo_options::concurrency::single, fifo_options::concurrency::single> queue(64);
    constexpr int kValues = 20000;

    std::thread producer([&] {
        for (auto i = 0; i < kValues; ++i)
            while (!queue.push(int{i})) {
            }
    });

    auto expected = 0;
    while (expected < kValues) {
        int out = -1;
        if (queue.pop(out)) {
            REQUIRE(out == expected);
            ++expected;
        }
    }

    producer.join();
}

TEST_CASE("The vendored farbot RealtimeObject hands the audio thread whole values",
          "[farbot][thread]") {
    using namespace farbot;
    struct Pair {
        int a = 0;
        int b = 0;
    };

    RealtimeObject<Pair, RealtimeObjectOptions::nonRealtimeMutatable> object;
    std::atomic<bool> running{true};

    std::thread writer([&] {
        for (auto i = 1; i <= 5000; ++i) {
            RealtimeObject<Pair, RealtimeObjectOptions::nonRealtimeMutatable>::ScopedAccess<
                ThreadType::nonRealtime>
                access(object);
            access->a = i;
            access->b = i;
        }
        running = false;
    });

    auto torn = 0;
    while (running) {
        RealtimeObject<Pair, RealtimeObjectOptions::nonRealtimeMutatable>::ScopedAccess<
            ThreadType::realtime>
            access(object);
        if (access->a != access->b)
            ++torn;
    }

    writer.join();
    CHECK(torn == 0);
}
