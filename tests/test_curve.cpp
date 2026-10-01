#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <string>
#include <support/CurveCorpus.hpp>

#include "magda/sdk/curve/Curve.hpp"

using namespace magda::sdk;

namespace {

bool sameBits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool sameBits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

bool sameHandle(const BezierHandle& a, const BezierHandle& b) {
    return sameBits(a.beatOffset, b.beatOffset) && sameBits(a.value, b.value) &&
           a.linked == b.linked;
}

bool samePoint(const AutomationPoint& a, const AutomationPoint& b) {
    return a.id == b.id && sameBits(a.beatPosition, b.beatPosition) && sameBits(a.value, b.value) &&
           a.curveType == b.curveType && sameBits(a.tension, b.tension) &&
           sameHandle(a.inHandle, b.inHandle) && sameHandle(a.outHandle, b.outHandle);
}

bool samePoint(const CurvePointData& a, const CurvePointData& b) {
    return sameBits(a.phase, b.phase) && sameBits(a.value, b.value) &&
           sameBits(a.tension, b.tension) && a.curveType == b.curveType &&
           sameBits(a.inHandleX, b.inHandleX) && sameBits(a.inHandleY, b.inHandleY) &&
           sameBits(a.outHandleX, b.outHandleX) && sameBits(a.outHandleY, b.outHandleY);
}

template <typename Point>
bool samePoints(const std::vector<Point>& a, const std::vector<Point>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!samePoint(a[i], b[i]))
            return false;
    return true;
}

Curve phaseCurve(std::vector<CurvePointData> points) {
    Curve curve;
    curve.points = std::move(points);
    return curve;
}

Curve beatCurve(std::vector<AutomationPoint> points) {
    Curve curve;
    curve.points = std::move(points);
    return curve;
}

std::string written(const Curve& curve) {
    std::string error;
    const auto text = writeCurve(curve, error);
    INFO(error);
    REQUIRE(text.has_value());
    return *text;
}

CurveStatus statusOf(std::string_view json) {
    return readCurve(json).status;
}

/// A phase document with @p point as its only point.
std::string phaseDocument(std::string_view point) {
    return "{\"format\":\"magda.curve\",\"version\":1,\"domain\":\"phase\","
           "\"range\":{\"min\":0,\"max\":1},\"points\":[" +
           std::string(point) + "]}";
}

std::string beatDocument(std::string_view point) {
    return "{\"format\":\"magda.curve\",\"version\":1,\"domain\":\"beats\","
           "\"range\":{\"min\":0,\"max\":1},\"points\":[" +
           std::string(point) + "]}";
}

}  // namespace

TEST_CASE("a phase curve writes the canonical document", "[curve]") {
    CurvePointData a;
    a.phase = 0.0f;
    a.value = 1.0f;
    CurvePointData b;
    b.phase = 0.1f;
    b.value = 0.25f;
    b.curveType = 3;
    b.tension = -1.5f;
    b.outHandleX = 0.05f;
    b.outHandleY = 0.5f;

    CHECK(written(phaseCurve({a, b})) ==
          "{\n"
          "  \"format\": \"magda.curve\",\n"
          "  \"version\": 1,\n"
          "  \"domain\": \"phase\",\n"
          "  \"range\": { \"min\": 0.0, \"max\": 1.0 },\n"
          "  \"points\": [\n"
          "    { \"x\": 0.0, \"y\": 1.0, \"interpolation\": \"linear\" },\n"
          "    { \"x\": 0.1, \"y\": 0.25, \"interpolation\": \"hardCorner\", \"tension\": -1.5, "
          "\"outHandle\": { \"x\": 0.05, \"y\": 0.5 } }\n"
          "  ]\n"
          "}\n");
}

TEST_CASE("a beat curve writes the canonical document", "[curve]") {
    AutomationPoint a;
    a.id = 7;
    a.beatPosition = 1.5;
    a.value = 0.5;
    a.curveType = CurveInterpolation::Bezier;
    a.outHandle.beatOffset = 0.5;
    a.outHandle.value = -0.25;
    a.outHandle.linked = false;
    AutomationPoint b;
    b.beatPosition = 8.0;
    b.value = 0.0;
    b.curveType = CurveInterpolation::Step;

    CHECK(written(beatCurve({a, b})) ==
          "{\n"
          "  \"format\": \"magda.curve\",\n"
          "  \"version\": 1,\n"
          "  \"domain\": \"beats\",\n"
          "  \"range\": { \"min\": 0.0, \"max\": 1.0 },\n"
          "  \"points\": [\n"
          "    { \"x\": 1.5, \"y\": 0.5, \"interpolation\": \"bezier\", \"outHandle\": "
          "{ \"x\": 0.5, \"y\": -0.25, \"linked\": false }, \"id\": 7 },\n"
          "    { \"x\": 8.0, \"y\": 0.0, \"interpolation\": \"step\" }\n"
          "  ]\n"
          "}\n");
}

TEST_CASE("an empty curve is a document with no points", "[curve]") {
    for (const auto& curve : {phaseCurve({}), beatCurve({})}) {
        const auto text = written(curve);
        CHECK(text.find("\"points\": []") != std::string::npos);
        const auto read = readCurve(text);
        REQUIRE(read.ok());
        CHECK(read.curve->domain() == curve.domain());
    }
}

TEST_CASE("every corpus curve survives the document bit for bit", "[curve][roundtrip]") {
    for (const auto& c : magda::curvecorpus::phaseCases()) {
        INFO(c.name);
        const auto text = written(phaseCurve(c.points));
        const auto read = readCurve(text);
        REQUIRE(read.ok());
        CHECK(samePoints(std::get<std::vector<CurvePointData>>(read.curve->points), c.points));
        CHECK(written(*read.curve) == text);
    }
    for (const auto& c : magda::curvecorpus::beatCases()) {
        INFO(c.name);
        const auto text = written(beatCurve(c.points));
        const auto read = readCurve(text);
        REQUIRE(read.ok());
        CHECK(samePoints(std::get<std::vector<AutomationPoint>>(read.curve->points), c.points));
        CHECK(written(*read.curve) == text);
    }
}

TEST_CASE("negative zero, extremes and handle flags keep their bits", "[curve][roundtrip]") {
    AutomationPoint p;
    p.id = -42;
    p.beatPosition = -0.0;
    p.value = -0.0;
    p.tension = -0.0;
    p.inHandle.beatOffset = -0.0;
    p.outHandle.linked = false;
    p.outHandle.value = std::numeric_limits<double>::denorm_min();
    AutomationPoint q = p;
    q.id = 9;
    q.beatPosition = 1.0e300;
    q.value = 0.1 + 0.2;
    q.inHandle.linked = false;
    const Curve curve = beatCurve({p, q});
    const auto read = readCurve(written(curve));
    REQUIRE(read.ok());
    CHECK(samePoints(std::get<std::vector<AutomationPoint>>(read.curve->points),
                     std::get<std::vector<AutomationPoint>>(curve.points)));

    CurvePointData c;
    c.phase = std::numeric_limits<float>::denorm_min();
    c.value = 1.0f / 3.0f;
    c.tension = -0.0f;
    c.inHandleX = std::numeric_limits<float>::max();
    c.outHandleY = -std::numeric_limits<float>::max();
    const Curve phase = phaseCurve({c});
    const auto readPhase = readCurve(written(phase));
    REQUIRE(readPhase.ok());
    CHECK(samePoints(std::get<std::vector<CurvePointData>>(readPhase.curve->points),
                     std::get<std::vector<CurvePointData>>(phase.points)));
}

TEST_CASE("the reader takes whole numbers and any member order inside a point", "[curve]") {
    const auto read = readCurve(beatDocument(R"({"interpolation":"step","y":1,"x":2,"id":3})"));
    REQUIRE(read.ok());
    const auto& points = std::get<std::vector<AutomationPoint>>(read.curve->points);
    REQUIRE(points.size() == 1);
    CHECK(points[0].beatPosition == 2.0);
    CHECK(points[0].value == 1.0);
    CHECK(points[0].curveType == CurveInterpolation::Step);
    CHECK(points[0].id == 3);
}

TEST_CASE("the reader refuses what the writer would not write", "[curve]") {
    CHECK(statusOf("nope") == CurveStatus::NotJson);
    CHECK(statusOf("{\"format\":\"other\"}") == CurveStatus::NotACurve);
    CHECK(statusOf("[]") == CurveStatus::NotACurve);

    const auto ok = phaseDocument(R"({"x":0.5,"y":0.5,"interpolation":"linear"})");
    REQUIRE(statusOf(ok) == CurveStatus::Ok);

    const auto edit = [&](std::string from, std::string to) {
        auto text = ok;
        const auto at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        return text;
    };

    CHECK(statusOf(edit("\"version\":1", "\"version\":0")) == CurveStatus::UnsupportedVersion);
    CHECK(statusOf(edit("\"version\":1", "\"version\":2")) == CurveStatus::FutureVersion);
    CHECK(statusOf(edit("\"version\":1", "\"version\":1.0")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"version\":1,", "\"version\":1,\"extra\":1,")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"phase\"", "\"seconds\"")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"max\":1", "\"max\":2")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"min\":0", "\"min\":-1")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"range\":{\"min\":0,\"max\":1},", "")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"x\":0.5", "\"x\":1.5")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"x\":0.5", "\"x\":1e60")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"linear\"", "\"smooth\"")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"y\":0.5", "\"y\":\"0.5\"")) == CurveStatus::Invalid);
    CHECK(statusOf(edit("\"x\":0.5,", "\"x\":0.5,\"x\":0.6,")) == CurveStatus::NotJson);
    CHECK(statusOf(edit("\"x\":0.5,", "")) == CurveStatus::Invalid);

    const auto point = [](std::string_view extra) {
        return phaseDocument(R"({"x":0.5,"y":0.5,"interpolation":"linear",)" + std::string(extra) +
                             "}");
    };
    CHECK(statusOf(point(R"("tension":0.0)")) == CurveStatus::Invalid);
    CHECK(statusOf(point(R"("tension":-0.0)")) == CurveStatus::Ok);
    CHECK(statusOf(point(R"("tension":2.0)")) == CurveStatus::Ok);
    CHECK(statusOf(point(R"("inHandle":{"x":0,"y":0})")) == CurveStatus::Invalid);
    CHECK(statusOf(point(R"("inHandle":{"x":0,"y":0.1})")) == CurveStatus::Ok);
    CHECK(statusOf(point(R"("inHandle":{"x":0.1})")) == CurveStatus::Invalid);
    CHECK(statusOf(point(R"("inHandle":{"x":0.1,"y":0.1,"linked":false})")) ==
          CurveStatus::Invalid);
    CHECK(statusOf(point(R"("id":4)")) == CurveStatus::Invalid);

    const auto beat = [](std::string_view extra) {
        return beatDocument(R"({"x":0.5,"y":0.5,"interpolation":"linear",)" + std::string(extra) +
                            "}");
    };
    CHECK(statusOf(beat(R"("id":4)")) == CurveStatus::Ok);
    CHECK(statusOf(beat(R"("id":-1)")) == CurveStatus::Invalid);
    CHECK(statusOf(beat(R"("id":4.0)")) == CurveStatus::Invalid);
    CHECK(statusOf(beat(R"("id":3000000000)")) == CurveStatus::Invalid);
    CHECK(statusOf(beat(R"("inHandle":{"x":0,"y":0,"linked":false})")) == CurveStatus::Ok);
    CHECK(statusOf(beat(R"("inHandle":{"x":0,"y":0,"linked":true})")) == CurveStatus::Invalid);
    CHECK(statusOf(beat(R"("inHandle":{"x":0,"y":0})")) == CurveStatus::Invalid);
}

TEST_CASE("points must be in x order", "[curve]") {
    CHECK(statusOf(phaseDocument(R"({"x":0.5,"y":0,"interpolation":"linear"},)"
                                 R"({"x":0.5,"y":1,"interpolation":"linear"})")) ==
          CurveStatus::Ok);
    CHECK(statusOf(phaseDocument(R"({"x":0.6,"y":0,"interpolation":"linear"},)"
                                 R"({"x":0.5,"y":1,"interpolation":"linear"})")) ==
          CurveStatus::Invalid);
    CHECK(statusOf(beatDocument(R"({"x":4,"y":0,"interpolation":"linear"},)"
                                R"({"x":3,"y":1,"interpolation":"linear"})")) ==
          CurveStatus::Invalid);
}

TEST_CASE("the writer refuses a curve the reader would", "[curve]") {
    std::string error;

    CurvePointData bad;
    bad.curveType = 4;
    CHECK_FALSE(writeCurve(phaseCurve({bad}), error).has_value());
    bad.curveType = -1;
    CHECK_FALSE(writeCurve(phaseCurve({bad}), error).has_value());

    bad.curveType = 0;
    bad.value = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(writeCurve(phaseCurve({bad}), error).has_value());

    CurvePointData late;
    late.phase = 1.5f;
    CHECK_FALSE(writeCurve(phaseCurve({late}), error).has_value());

    AutomationPoint inf;
    inf.value = std::numeric_limits<double>::infinity();
    CHECK_FALSE(writeCurve(beatCurve({inf}), error).has_value());

    AutomationPoint first;
    first.beatPosition = 2.0;
    AutomationPoint second;
    second.beatPosition = 1.0;
    CHECK_FALSE(writeCurve(beatCurve({first, second}), error).has_value());

    AutomationPoint unknown;
    unknown.curveType = static_cast<CurveInterpolation>(9);
    CHECK_FALSE(writeCurve(beatCurve({unknown}), error).has_value());
}

TEST_CASE("a newer curve is recognised and never rewritten", "[curve]") {
    const auto future = "{\"format\":\"magda.curve\",\"version\":2,\"anything\":true}";
    CHECK(isFutureCurve(future));
    CHECK(readCurve(future).status == CurveStatus::FutureVersion);
    CHECK_FALSE(isFutureCurve(phaseDocument("")));
    CHECK_FALSE(isFutureCurve("{\"format\":\"magda.preset\",\"version\":2}"));
    CHECK_FALSE(isFutureCurve("nope"));
}
