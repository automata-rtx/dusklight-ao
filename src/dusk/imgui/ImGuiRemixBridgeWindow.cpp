#include "imgui.h"

#include "ImGuiRemixBridgeWindow.hpp"
#include "ImGuiMenuTools.hpp"
#include "dusk/remix_bridge.hpp"
#include "dusk/settings.h"

namespace dusk {

void DrawRemixBridgeWindow(bool& open) {
    if (!open) {
        return;
    }

    if (!ImGui::Begin("Remix Bridge", &open)) {
        ImGui::End();
        return;
    }

    bool enabled = getSettings().game.remixKankyoBridge.getValue();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        getSettings().game.remixKankyoBridge.setValue(enabled);
    }

    ImGui::SameLine();
    ImGui::Text("status: %s", remix::statusString());
    ImGui::Text("total pushes: %llu",
                static_cast<unsigned long long>(remix::totalPushes()));

    ImGui::SeparatorText("Pushed variables");
    if (ImGui::BeginTable("remix_bridge_vars", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Pushes");
        ImGui::TableHeadersRow();

        for (const remix::PushedVar& var : remix::debugVars()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(var.key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(var.value.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", var.pushes);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

void ImGuiMenuTools::ShowRemixBridgeWindow() {
    DrawRemixBridgeWindow(m_showRemixBridgeWindow);
}

}  // namespace dusk
