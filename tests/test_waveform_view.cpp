#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/sdk/waveview/WaveformView.hpp"

using Catch::Approx;
using magda::sdk::BufferWaveformSource;
using magda::sdk::PeakData;
using magda::sdk::PeakDataWaveformSource;
using magda::sdk::WaveformCursor;
using magda::sdk::WaveformMarkers;
using magda::sdk::WaveformPointer;
using magda::sdk::WaveformView;

namespace {

/// One second at 1 kHz: a ramp from 0 to 1, so a sample's value is its position.
struct Fixture {
    std::vector<float> samples;
    BufferWaveformSource source;
    WaveformView view;

    Fixture() : samples(make()), source(samples.data(), 1000) {
        view.setSize(200, 80);
        view.setSource(&source, 1.0);
        WaveformMarkers markers;
        markers.end = 1.0;
        markers.loop = true;
        markers.loopStart = 0.25;
        markers.loopEnd = 0.5;
        view.setMarkers(markers);
    }

    static std::vector<float> make() {
        std::vector<float> s(1000);
        for (std::size_t i = 0; i < s.size(); ++i)
            s[i] = static_cast<float>(i) / 1000.0f;
        return s;
    }
};

WaveformPointer at(float x, float y = 40.0f) {
    WaveformPointer p;
    p.x = x;
    p.y = y;
    return p;
}

}  // namespace

TEST_CASE("A new source fits the pane and scrolls home", "[waveview]") {
    Fixture f;
    CHECK(f.view.pixelsPerSecond() == 200.0);
    CHECK(f.view.scrollSeconds() == 0.0);
    CHECK(f.view.secondsToPixel(0.5) == 100.0f);
    CHECK(f.view.secondsToPixel(1.0) == 199.0f);
    CHECK(f.view.pixelToSeconds(100.0f) == 0.5);
    CHECK(f.view.pixelToSeconds(500.0f) == 1.0);
}

TEST_CASE("Wheel zoom keeps the time under the cursor and stops at the fit", "[waveview]") {
    Fixture f;
    f.view.zoomBy(4.0, 100.0f);
    CHECK(f.view.pixelsPerSecond() == 800.0);
    CHECK(f.view.pixelToSeconds(100.0f) == Approx(0.5));
    f.view.zoomBy(0.01, 100.0f);
    CHECK(f.view.pixelsPerSecond() == 200.0);
    CHECK(f.view.scrollSeconds() == 0.0);
    f.view.zoomBy(1000.0, 0.0f);
    CHECK(f.view.pixelsPerSecond() == WaveformView::kMaxPixelsPerSecond);
}

TEST_CASE("Alt-drag scrolls within the source", "[waveview]") {
    Fixture f;
    f.view.zoomBy(4.0, 0.0f);
    auto press = at(150.0f);
    press.alt = true;
    f.view.pointerDown(press);
    f.view.pointerDrag(at(50.0f));
    CHECK(f.view.scrollSeconds() == Approx(0.125));
    f.view.pointerDrag(at(-5000.0f));
    CHECK(f.view.scrollSeconds() == Approx(0.75));
    f.view.pointerUp(at(-5000.0f));
}

TEST_CASE("Command-drag up zooms in about the press", "[waveview]") {
    Fixture f;
    auto press = at(50.0f, 60.0f);
    press.command = true;
    f.view.pointerDown(press);
    auto up = at(50.0f, 20.0f);
    up.command = true;
    f.view.pointerDrag(up);
    CHECK(f.view.pixelsPerSecond() > 200.0);
    CHECK(f.view.pixelToSeconds(50.0f) == Approx(0.25).margin(1e-9));
    CHECK(f.view.cursor(up) == WaveformCursor::ZoomIn);
    f.view.pointerUp(up);
    CHECK(f.view.cursor(at(70.0f)) == WaveformCursor::Normal);
}

TEST_CASE("Markers drag where they are grabbed; shift-click places the loop start", "[waveview]") {
    Fixture f;
    CHECK(f.view.cursor(at(197.0f)) == WaveformCursor::ResizeLeftRight);
    CHECK(f.view.pointerDown(at(197.0f)).markersChanged);
    f.view.pointerDrag(at(160.0f));
    f.view.pointerUp(at(160.0f));
    CHECK(f.view.markers().end == 0.8);

    f.view.pointerDown(at(2.0f));
    f.view.pointerDrag(at(20.0f));
    f.view.pointerUp(at(20.0f));
    CHECK(f.view.markers().start == 0.1);

    auto shift = at(140.0f);
    shift.shift = true;
    f.view.pointerDown(shift);
    f.view.pointerUp(shift);
    CHECK(f.view.markers().loopStart == 0.7);
}

TEST_CASE("The loop bar moves the region whole, inside the source", "[waveview]") {
    Fixture f;
    CHECK(f.view.cursor(at(75.0f, 3.0f)) == WaveformCursor::DraggingHand);
    f.view.pointerDown(at(75.0f, 3.0f));
    f.view.pointerDrag(at(115.0f, 3.0f));
    CHECK(f.view.markers().loopStart == Approx(0.45));
    CHECK(f.view.markers().loopEnd == Approx(0.7));
    f.view.pointerDrag(at(500.0f, 3.0f));
    CHECK(f.view.markers().loopEnd == Approx(1.0));
    CHECK(f.view.markers().loopStart == Approx(0.75));
}

TEST_CASE("A pane that grows keeps the whole source in view", "[waveview]") {
    Fixture f;
    f.view.setSize(400, 80);
    CHECK(f.view.pixelsPerSecond() == 400.0);
}

TEST_CASE("Both sources report the loudest absolute sample", "[waveview]") {
    std::vector<float> samples(256, 0.0f);
    samples[10] = -0.75f;
    samples[200] = 0.5f;
    BufferWaveformSource exact(samples.data(), 256);
    CHECK(exact.absolutePeak(0, 64) == 0.75f);
    CHECK(exact.absolutePeak(64, 256) == 0.5f);
    CHECK(exact.absolutePeak(5, 5) == 0.0f);

    PeakData peaks(1, 256);
    const float* channels[] = {samples.data()};
    REQUIRE(peaks.addBlock(magda::ConstBufferView(channels, 1, 256), 0));
    PeakDataWaveformSource stored(peaks, 0);
    CHECK(stored.absolutePeak(0, 64) == Approx(0.75f).margin(1e-4));
    // Buckets are 64 samples, so a range inside one reads the whole bucket.
    CHECK(stored.absolutePeak(20, 30) == Approx(0.75f).margin(1e-4));
}
