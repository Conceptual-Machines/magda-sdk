// The meter model and painter as a wasm reactor, for the Canvas 2D demo and its node check.

#include <cstdint>
#include <string>
#include <support/MeterScenario.hpp>

#include "magda/sdk/display/DisplayList.hpp"
#include "magda/sdk/meter/MeterModel.hpp"
#include "magda/sdk/meter/MeterPainter.hpp"
#include "magda/sdk/state/detail/Json.hpp"

namespace {

std::string result;
magda::sdk::MeterModel live;

const char* publish(const magda::sdk::display::DisplayList& list) {
    result = magda::sdk::display::toJson(list);
    return result.c_str();
}

}  // namespace

extern "C" {

/// Runs case @p index of a scenarios document; null when there is no such case.
const char* meter_demo_run_case(const char* scenariosJson, int index) {
    std::string error;
    const auto root = magda::sdk::detail::parseJson(scenariosJson, error);
    if (!root)
        return nullptr;
    const auto cases = magda::sdk::meter_scenario::casesOf(*root);
    if (index < 0 || index >= static_cast<int>(cases.size()))
        return nullptr;
    magda::sdk::MeterModel meter;
    return publish(magda::sdk::meter_scenario::run(cases[static_cast<std::size_t>(index)], meter));
}

void meter_demo_live_set_levels(float left, float right) {
    const float gains[] = {left, right};
    live.setTargets(gains);
}

int meter_demo_live_advance(float elapsedMs) {
    return live.advance(elapsedMs) ? 1 : 0;
}

const char* meter_demo_live_paint(float width, float height, int horizontal, float zeroDbY) {
    magda::sdk::MeterLayout layout{width, height,
                                   horizontal != 0
                                       ? magda::sdk::MeterLayout::Orientation::Horizontal
                                       : magda::sdk::MeterLayout::Orientation::Vertical,
                                   zeroDbY};
    magda::sdk::display::DisplayList list;
    magda::sdk::paintMeter(live, layout, list);
    return publish(list);
}

std::uint32_t meter_demo_default_colour(int role) {
    if (role < 0 || role >= magda::sdk::display::kNumColourRoles)
        return 0;
    return magda::sdk::display::defaultColour(static_cast<magda::sdk::display::ColourRole>(role));
}
}
