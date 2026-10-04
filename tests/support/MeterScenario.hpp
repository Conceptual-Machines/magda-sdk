#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/meter/MeterModel.hpp"
#include "magda/sdk/meter/MeterPainter.hpp"
#include "magda/sdk/state/detail/Json.hpp"

/// Runs tests/golden/meter/scenarios.json, natively and in the Canvas demo's wasm.
namespace magda::sdk::meter_scenario {

struct Case {
    std::string name;
    MeterLayout layout;
    const detail::JsonValue* steps = nullptr;
};

inline float numberOf(const detail::JsonValue* value, float fallback) {
    if (value == nullptr)
        return fallback;
    if (value->type == detail::JsonValue::Type::Int)
        return static_cast<float>(value->integer);
    if (value->type == detail::JsonValue::Type::Double)
        return static_cast<float>(value->real);
    return fallback;
}

inline std::vector<Case> casesOf(const detail::JsonValue& root) {
    std::vector<Case> cases;
    const auto* list = root.member("cases");
    if (list == nullptr)
        return cases;
    for (const auto& entry : list->array) {
        Case c;
        if (const auto* name = entry.member("name"))
            c.name = name->string;
        if (const auto* layout = entry.member("layout")) {
            c.layout.width = numberOf(layout->member("width"), 0.0f);
            c.layout.height = numberOf(layout->member("height"), 0.0f);
            c.layout.zeroDbY = numberOf(layout->member("zeroDbY"), -1.0f);
            const auto* orientation = layout->member("orientation");
            if (orientation != nullptr && orientation->string == "horizontal")
                c.layout.orientation = MeterLayout::Orientation::Horizontal;
        }
        c.steps = entry.member("steps");
        cases.push_back(std::move(c));
    }
    return cases;
}

/// Plays the case's steps into @p meter, then paints the final frame.
inline display::DisplayList run(const Case& c, MeterModel& meter) {
    if (c.steps != nullptr) {
        for (const auto& step : c.steps->array) {
            if (const auto* levels = step.member("levels")) {
                std::vector<float> gains;
                for (const auto& level : levels->array)
                    gains.push_back(numberOf(&level, 0.0f));
                meter.setTargets(gains);
            }
            if (const auto* advance = step.member("advanceMs")) {
                const auto repeat = static_cast<int>(numberOf(step.member("repeat"), 1.0f));
                for (int i = 0; i < repeat; ++i)
                    meter.advance(numberOf(advance, 0.0f));
            }
        }
    }
    display::DisplayList list;
    paintMeter(meter, c.layout, list);
    return list;
}

}  // namespace magda::sdk::meter_scenario
