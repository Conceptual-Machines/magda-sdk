// The SDK's UI cores (meter, curve editor) as a wasm reactor, for the Canvas 2D demos and their
// node check.

#include <cstdint>
#include <string>
#include <support/CurveEditorScenario.hpp>
#include <support/MeterScenario.hpp>

#include "magda/sdk/curve/Curve.hpp"
#include "magda/sdk/curveedit/CurveEditor.hpp"
#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/meter/MeterModel.hpp"
#include "magda/sdk/meter/MeterPainter.hpp"
#include "magda/sdk/state/detail/Json.hpp"

/// The page measures tooltip text with Canvas measureText.
extern "C" __attribute__((import_module("env"), import_name("measure_text"))) float measure_text(
    const char* text, int length, float fontSize);

namespace {

namespace sdk = magda::sdk;

std::string result;
sdk::MeterModel meter;
sdk::CurveEditor editor;
std::vector<sdk::CurvePointData> lastBefore;

const char* publish(const sdk::display::DisplayList& list) {
    result = sdk::display::toJson(list);
    return result.c_str();
}

const char* publishCurve(const std::vector<sdk::CurvePointData>& points) {
    std::string error;
    result = sdk::writeCurve(sdk::Curve{points}, error).value_or(std::string{});
    return result.c_str();
}

std::optional<sdk::detail::JsonValue> parse(const char* json) {
    std::string error;
    return sdk::detail::parseJson(json, error);
}

sdk::CurveEditorModifiers modsOf(int bits) {
    return {(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0};
}

/// Response flags: 1 repaint, 2 preview, 4 commit, 8 loop moved, 16 key handled.
int flagsOf(const sdk::CurveEditorResponse& r) {
    if (r.commit != sdk::CurveEditorResponse::Commit::None)
        lastBefore = r.before;
    return (r.repaint ? 1 : 0) | (r.preview ? 2 : 0) |
           (r.commit != sdk::CurveEditorResponse::Commit::None ? 4 : 0) |
           (r.loopPreview || r.loopCommitted ? 8 : 0) | (r.handled ? 16 : 0);
}

}  // namespace

extern "C" {

// Meter

/// Runs case @p index of a meter scenarios document; null when there is no such case.
const char* meter_demo_run_case(const char* scenariosJson, int index) {
    const auto root = parse(scenariosJson);
    if (!root)
        return nullptr;
    const auto cases = sdk::meter_scenario::casesOf(*root);
    if (index < 0 || index >= static_cast<int>(cases.size()))
        return nullptr;
    sdk::MeterModel model;
    return publish(sdk::meter_scenario::run(cases[static_cast<std::size_t>(index)], model));
}

void meter_demo_live_set_levels(float left, float right) {
    const float gains[] = {left, right};
    meter.setTargets(gains);
}

int meter_demo_live_advance(float elapsedMs) {
    return meter.advance(elapsedMs) ? 1 : 0;
}

const char* meter_demo_live_paint(float width, float height, int horizontal, float zeroDbY) {
    sdk::MeterLayout layout{width, height,
                            horizontal != 0 ? sdk::MeterLayout::Orientation::Horizontal
                                            : sdk::MeterLayout::Orientation::Vertical,
                            zeroDbY};
    sdk::display::DisplayList list;
    sdk::paintMeter(meter, layout, list);
    return publish(list);
}

std::uint32_t ui_demo_default_colour(int role) {
    if (role < 0 || role >= sdk::display::kNumColourRoles)
        return 0;
    return sdk::display::defaultColour(static_cast<sdk::display::ColourRole>(role));
}

// Curve editor

/// Runs case @p index of a curve editor scenarios document; null when there is no such case.
const char* curve_demo_run_case(const char* scenariosJson, int index) {
    const auto root = parse(scenariosJson);
    const auto* cases = root ? root->member("cases") : nullptr;
    if (cases == nullptr || index < 0 || index >= static_cast<int>(cases->array.size()))
        return nullptr;
    sdk::CurveEditor scenarioEditor;
    return publish(sdk::curve_editor_scenario::run(cases->array[static_cast<std::size_t>(index)],
                                                   scenarioEditor));
}

/// Configures the live editor from a scenario case (size, grid, snap, loop, points).
int curve_demo_live_configure(const char* caseJson) {
    const auto c = parse(caseJson);
    if (!c)
        return 0;
    editor.setTextMeasure([](std::string_view text, float fontSize) {
        return measure_text(text.data(), static_cast<int>(text.size()), fontSize);
    });
    sdk::curve_editor_scenario::configure(*c, editor);
    return 1;
}

void curve_demo_live_resize(int width, int height) {
    editor.setSize(width, height);
}

/// @p type: 0 down, 1 drag, 2 up, 3 double-click, 4 move, 5 exit. @p mods: 1 shift, 2 command,
/// 4 alt, 8 popup.
int curve_demo_live_pointer(int type, float x, float y, int mods) {
    sdk::CurveEditorPointer pointer{x, y, modsOf(mods), (mods & 8) == 0};
    switch (type) {
        case 0:
            return flagsOf(editor.pointerDown(pointer));
        case 1:
            return flagsOf(editor.pointerDrag(pointer));
        case 2:
            return flagsOf(editor.pointerUp(pointer));
        case 3:
            return flagsOf(editor.doubleClick(pointer));
        case 4:
            return flagsOf(editor.pointerMove(pointer));
        case 5:
            return flagsOf(editor.pointerExit());
        default:
            return 0;
    }
}

/// @p code: 0 character, 1 delete, 2 backspace.
int curve_demo_live_key(int code, int character, int mods) {
    sdk::CurveEditorKey key;
    key.code = code == 1   ? sdk::CurveEditorKey::Code::Delete
               : code == 2 ? sdk::CurveEditorKey::Code::Backspace
                           : sdk::CurveEditorKey::Code::Character;
    key.character = static_cast<char32_t>(character);
    key.mods = modsOf(mods);
    return flagsOf(editor.keyPressed(key));
}

void curve_demo_live_indicator(float phase, float value, int triggerLit) {
    editor.setIndicator(phase, value, triggerLit != 0);
}

int curve_demo_live_cursor(int mods) {
    return static_cast<int>(editor.cursor(modsOf(mods)));
}

const char* curve_demo_live_render() {
    sdk::display::DisplayList list;
    editor.render(list);
    return publish(list);
}

/// The committed curve, as a curve document.
const char* curve_demo_live_points() {
    return publishCurve(editor.points());
}

/// The curve before the last commit, as a curve document: the shell's undo step.
const char* curve_demo_live_before() {
    return publishCurve(lastBefore);
}

/// Replaces the curve from a curve document, as an undo does.
int curve_demo_live_set_points(const char* curveJson) {
    const auto read = sdk::readCurve(curveJson);
    if (!read.ok() || read.curve->domain() != sdk::CurveDomain::Phase)
        return 0;
    editor.refreshPoints(std::get<0>(read.curve->points));
    return 1;
}
}
