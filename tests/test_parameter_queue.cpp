#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <utility>
#include <vector>

#include "magda/sdk/lockfree/ParameterQueue.hpp"

using magda::BatchedParameterQueue;
using magda::PackedPath;
using magda::ParameterChange;
using magda::ParameterQueue;

TEST_CASE("A parameter queue hands changes back in order", "[parameter-queue]") {
    ParameterQueue queue;
    CHECK_FALSE(queue.hasPending());

    for (auto i = 0; i < 3; ++i) {
        ParameterChange change;
        change.paramIndex = i;
        change.value = static_cast<float>(i) * 0.5f;
        REQUIRE(queue.push(change));
    }
    CHECK(queue.pendingCount() == 3);

    ParameterChange out;
    for (auto i = 0; i < 3; ++i) {
        REQUIRE(queue.pop(out));
        CHECK(out.paramIndex == i);
        CHECK(out.value == Catch::Approx(static_cast<float>(i) * 0.5f));
    }
    CHECK_FALSE(queue.pop(out));
}

TEST_CASE("A parameter queue refuses a push when full", "[parameter-queue]") {
    ParameterQueue queue;
    const ParameterChange change;

    for (auto i = 0; i < ParameterQueue::kQueueSize - 1; ++i)
        REQUIRE(queue.push(change));
    CHECK_FALSE(queue.push(change));

    queue.clear();
    CHECK(queue.push(change));
}

TEST_CASE("A packed path holds at most its fixed number of steps", "[parameter-queue]") {
    PackedPath path;
    const PackedPath::Step steps[PackedPath::kMaxSteps + 1] = {};

    CHECK(path.setSteps(steps, PackedPath::kMaxSteps));
    CHECK(path.stepCount == PackedPath::kMaxSteps);
    CHECK_FALSE(path.setSteps(steps, PackedPath::kMaxSteps + 1));
    CHECK(path.stepCount == PackedPath::kMaxSteps);
}

TEST_CASE("A batched queue carries one path across every change", "[parameter-queue]") {
    BatchedParameterQueue batched;

    PackedPath path;
    path.trackId = 7;
    path.topLevelDeviceId = 11;
    REQUIRE(batched.pushBatch(path, {{0, 0.1f}, {1, 0.2f}}));

    std::vector<ParameterChange> changes;
    batched.popAll(changes);
    REQUIRE(changes.size() == 2);
    CHECK(changes[1].paramIndex == 1);
    CHECK(changes[1].devicePath.trackId == 7);
    CHECK(changes[1].devicePath.topLevelDeviceId == 11);
}

TEST_CASE("A parameter queue delivers every change across two threads",
          "[parameter-queue][thread]") {
    ParameterQueue queue;
    constexpr int kChanges = 20000;

    std::thread producer([&] {
        for (auto i = 0; i < kChanges; ++i) {
            ParameterChange change;
            change.paramIndex = i;
            while (!queue.push(change)) {
            }
        }
    });

    auto expected = 0;
    while (expected < kChanges) {
        ParameterChange out;
        if (queue.pop(out)) {
            REQUIRE(out.paramIndex == expected);
            ++expected;
        }
    }

    producer.join();
}
