#pragma once

#include <string>
#include <vector>

#include "magda/sdk/curveedit/CurveEditor.hpp"
#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/state/detail/Json.hpp"

/// Runs tests/golden/curve-editor/scenarios.json, natively and in the Canvas demo's wasm.
namespace magda::sdk::curve_editor_scenario {

inline double numberOf(const detail::JsonValue* value, double fallback) {
    if (value == nullptr)
        return fallback;
    if (value->type == detail::JsonValue::Type::Int)
        return static_cast<double>(value->integer);
    if (value->type == detail::JsonValue::Type::Double)
        return value->real;
    return fallback;
}

inline bool flagOf(const detail::JsonValue* value) {
    return value != nullptr && value->type == detail::JsonValue::Type::Bool && value->boolean;
}

inline CurvePointData pointOf(const detail::JsonValue& v) {
    CurvePointData p;
    p.phase = static_cast<float>(numberOf(v.member("phase"), 0.0));
    p.value = static_cast<float>(numberOf(v.member("value"), 0.5));
    p.tension = static_cast<float>(numberOf(v.member("tension"), 0.0));
    p.curveType = static_cast<int>(numberOf(v.member("curveType"), 0.0));
    p.inHandleX = static_cast<float>(numberOf(v.member("inHandleX"), 0.0));
    p.inHandleY = static_cast<float>(numberOf(v.member("inHandleY"), 0.0));
    p.outHandleX = static_cast<float>(numberOf(v.member("outHandleX"), 0.0));
    p.outHandleY = static_cast<float>(numberOf(v.member("outHandleY"), 0.0));
    return p;
}

inline CurveEditorModifiers modsOf(const detail::JsonValue* list) {
    CurveEditorModifiers mods;
    if (list == nullptr)
        return mods;
    for (const auto& m : list->array) {
        mods.shift |= m.string == "shift";
        mods.command |= m.string == "command";
        mods.alt |= m.string == "alt";
        mods.popup |= m.string == "popup";
    }
    return mods;
}

/// Configures @p editor from a case.
inline void configure(const detail::JsonValue& c, CurveEditor& editor) {
    const auto* size = c.member("size");
    editor.setSize(static_cast<int>(numberOf(size ? &size->array.at(0) : nullptr, 200)),
                   static_cast<int>(numberOf(size ? &size->array.at(1) : nullptr, 120)));
    editor.setPadding(static_cast<int>(numberOf(c.member("padding"), 8)));
    if (const auto* grid = c.member("grid"))
        editor.setGrid(static_cast<int>(numberOf(&grid->array.at(0), 4)),
                       static_cast<int>(numberOf(&grid->array.at(1), 4)));
    if (const auto* snap = c.member("snap"))
        editor.setSnap(flagOf(snap->member("x")), flagOf(snap->member("y")),
                       snap->member("loop") == nullptr || flagOf(snap->member("loop")));
    if (const auto* loop = c.member("loop"))
        editor.setLoopRegion(true, static_cast<float>(numberOf(loop->member("start"), 0.0)),
                             static_cast<float>(numberOf(loop->member("end"), 1.0)));
    editor.setDrawContentBorder(flagOf(c.member("border")));
    if (const auto* indicator = c.member("indicator"))
        editor.setIndicator(static_cast<float>(numberOf(indicator->member("phase"), 0.0)),
                            static_cast<float>(numberOf(indicator->member("value"), 0.0)),
                            flagOf(indicator->member("trigger")));

    std::vector<CurvePointData> points;
    if (const auto* list = c.member("points"))
        for (const auto& p : list->array)
            points.push_back(pointOf(p));
    editor.setPoints(points);
}

/// Plays one event; returns what the editor answered.
inline CurveEditorResponse play(const detail::JsonValue& e, CurveEditor& editor) {
    const auto type = e.member("type") != nullptr ? e.member("type")->string : std::string{};
    CurveEditorPointer pointer;
    pointer.x = static_cast<float>(numberOf(e.member("x"), 0.0));
    pointer.y = static_cast<float>(numberOf(e.member("y"), 0.0));
    pointer.mods = modsOf(e.member("mods"));
    pointer.primary = !pointer.mods.popup;

    if (type == "down")
        return editor.pointerDown(pointer);
    if (type == "drag")
        return editor.pointerDrag(pointer);
    if (type == "up")
        return editor.pointerUp(pointer);
    if (type == "double")
        return editor.doubleClick(pointer);
    if (type == "move")
        return editor.pointerMove(pointer);
    if (type == "key") {
        CurveEditorKey key;
        key.mods = pointer.mods;
        const auto name = e.member("key") != nullptr ? e.member("key")->string : std::string{};
        if (name == "delete")
            key.code = CurveEditorKey::Code::Delete;
        else if (name == "backspace")
            key.code = CurveEditorKey::Code::Backspace;
        else if (name.size() == 1) {
            key.code = CurveEditorKey::Code::Character;
            key.character = static_cast<char32_t>(name[0]);
        }
        return editor.keyPressed(key);
    }
    return {};
}

/// The tooltip width a test can reproduce on any host: 6 units per character.
inline float fixedMeasure(std::string_view text, float) {
    return 6.0f * static_cast<float>(text.size());
}

/// Configures the editor from case @p c, plays its events and renders the final frame.
inline display::DisplayList run(const detail::JsonValue& c, CurveEditor& editor) {
    editor.setTextMeasure(fixedMeasure);
    configure(c, editor);
    if (const auto* events = c.member("events"))
        for (const auto& e : events->array)
            play(e, editor);
    display::DisplayList list;
    editor.render(list);
    return list;
}

}  // namespace magda::sdk::curve_editor_scenario
