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
    ImGui::TextWrapped(
        "Note: this window is not drawn at all on the D3D9 backend - the ImGui overlay renders "
        "through WebGPU, which that backend never initializes. Under Remix these settings are "
        "edited from Remix's own Dusklight tab (rtx.dusklight.game.*), and the values here are "
        "only the fallback for a Remix build without the option getter.");
    ImGui::Text("total pushes: %llu",
                static_cast<unsigned long long>(remix::totalPushes()));

    ImGui::SeparatorText("Sun / Moon distant light");
    {
        auto& settings = getSettings().game;

        bool lightEnabled = settings.remixSunMoonLight.getValue();
        if (ImGui::Checkbox("Sun/Moon Light", &lightEnabled)) {
            settings.remixSunMoonLight.setValue(lightEnabled);
        }

        const remix::CelestialLightDebug& cel = remix::celestialDebug();
        ImGui::SameLine();
        ImGui::Text("device: %s | %s", cel.deviceRegistered ? "registered" : "-",
                    cel.active ? (cel.isDay ? "SUN" : "MOON") : "inactive");

        float sunIntensity = settings.remixSunIntensity.getValue();
        if (ImGui::SliderFloat("Sun Intensity", &sunIntensity, 0.0f, 50.0f, "%.2f")) {
            settings.remixSunIntensity.setValue(sunIntensity);
        }

        float moonIntensity = settings.remixMoonIntensity.getValue();
        if (ImGui::SliderFloat("Moon Intensity", &moonIntensity, 0.0f, 10.0f, "%.2f")) {
            settings.remixMoonIntensity.setValue(moonIntensity);
        }

        float angle = settings.remixCelestialAngle.getValue();
        if (ImGui::SliderFloat("Angular Diameter", &angle, 0.1f, 10.0f, "%.2f deg")) {
            settings.remixCelestialAngle.setValue(angle);
        }

        ImGui::Checkbox("Flip Direction (debug)", &remix::celestialFlipDirection());
        ImGui::Checkbox("Lock Direction (debug)", &remix::celestialLockDirection());

        if (cel.active) {
            // Azimuth/elevation first: it is the readout that answers "is the
            // sun following me?" at a glance. Both are computed from time of
            // day alone, so they must not move while the player does.
            ImGui::Text("azimuth: %6.1f deg   elevation: %5.1f deg", cel.azimuth,
                        cel.elevation);
            ImGui::Text("dir: %.3f, %.3f, %.3f  fade: %.2f", cel.direction[0], cel.direction[1],
                        cel.direction[2], cel.fade);
            ImGui::Text("radiance: %.2f, %.2f, %.2f", cel.radiance[0], cel.radiance[1],
                        cel.radiance[2]);
            ImGui::TextWrapped(
                "Azimuth/elevation depend on time of day and nothing else. If they hold still "
                "while you run in a circle but the lighting still swings, the direction is fine "
                "and something downstream is rotating it - lock it and check whether a tree's "
                "shadow stays put on the ground as you circle the tree.");
        }
    }

    ImGui::SeparatorText("Effect lights");
    {
        auto& settings = getSettings().game;

        bool effectEnabled = settings.effectLights.getValue();
        if (ImGui::Checkbox("Effect Lights", &effectEnabled)) {
            settings.effectLights.setValue(effectEnabled);
        }

        const remix::EffectLightsDebug& eff = remix::effectLightsDebug();
        ImGui::SameLine();
        if (effectEnabled && !eff.enabled) {
            ImGui::TextUnformatted("waiting for the D3D9 device");
        } else {
            ImGui::Text("drawn: %d | tracked: %d", eff.drawn, eff.tracked);
        }

        // The whole chain, so a light lost at any step shows which step lost it.
        ImGui::Text("emitters %d -> considered %d -> candidates %d -> sites %d",
                    eff.stats.emitters, eff.stats.considered, eff.stats.candidates,
                    eff.stats.sites);
        ImGui::Text("reach from the game: %d | colour from the game: %d | game lights unused: %d",
                    eff.stats.derived, eff.stats.colorFromGame, eff.stats.orphans);
        ImGui::Text("game lights available (point/spot): %d/%d | culled: %d",
                    eff.stats.vanillaPoint, eff.stats.vanillaSpot, eff.stats.culled);

        float effIntensity = settings.effectLightIntensity.getValue();
        if (ImGui::SliderFloat("Effect Intensity", &effIntensity, 0.0f, 8.0f, "%.2f")) {
            settings.effectLightIntensity.setValue(effIntensity);
        }

        float fireOffset = settings.effectLightFireOffset.getValue();
        if (ImGui::SliderFloat("Fire Height Offset", &fireOffset, -200.0f, 200.0f, "%.1f units")) {
            settings.effectLightFireOffset.setValue(fireOffset);
        }

        if (ImGui::Button("Log classification report")) {
            effect_lights::requestReport();
        }

        ImGui::TextWrapped(
            "Lights at the origin of the game's own fire and glow effects, rather than where the "
            "game registered a light. The full set of knobs lives in Remix's Dusklight tab, which "
            "is the one that is actually reachable while the D3D9 backend is running.");
    }

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
