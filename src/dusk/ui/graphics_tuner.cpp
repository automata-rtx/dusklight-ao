#include "graphics_tuner.hpp"

#include "Z2AudioLib/Z2SeMgr.h"
#include "m_Do/m_Do_audio.h"

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include <fmt/format.h>

#include "dusk/config.hpp"
#include "dusk/settings.h"
#include "dusk/texture_replacements.hpp"

#include <algorithm>
#include <string>

namespace dusk::ui {
namespace {

const Rml::String kDocumentSource = R"RML(
<rml>
<head>
    <link type="text/rcss" href="res/rml/tuner.rcss" />
</head>
<body>
    <div id="root" class="tuner-root">
        <div class="tuner">
            <div class="header">
                <div id="title"></div>
                <div id="carousel-container" class="carousel-container"></div>
            </div>
            <div id="description" class="description"></div>
            <div class="divider"></div>
            <div id="footer" class="footer"></div>
        </div>
    </div>
</body>
</rml>
)RML";

int get_value(GraphicsOption option) {
    switch (option) {
    case GraphicsOption::InternalResolution:
        return getSettings().game.internalResolutionScale.getValue();
    case GraphicsOption::ShadowResolution:
        return getSettings().game.shadowResolutionMultiplier.getValue();
    case GraphicsOption::Resampler:
        return static_cast<int>(getSettings().game.resampler.getValue());
    case GraphicsOption::Fxaa:
        return getSettings().game.enableFxaa.getValue() ? 1 : 0;
    case GraphicsOption::Fsr1:
        return getSettings().game.enableFsr1.getValue() ? 1 : 0;
    case GraphicsOption::BloomMode:
        return static_cast<int>(getSettings().game.bloomMode.getValue());
    case GraphicsOption::BloomMultiplier:
        return std::clamp(
            static_cast<int>(getSettings().game.bloomMultiplier.getValue() * 100.0f + 0.5f), 0,
            100);
    case GraphicsOption::DepthOfFieldMode:
        return static_cast<int>(getSettings().game.depthOfFieldMode.getValue());
    case GraphicsOption::TextureReplacements:
        return getSettings().game.enableTextureReplacements.getValue();
    case GraphicsOption::AmbientOcclusion:
        return getSettings().game.enableAmbientOcclusion.getValue() ? 1 : 0;
    case GraphicsOption::AmbientOcclusionQuality:
        return getSettings().game.aoQuality.getValue();
    case GraphicsOption::AmbientOcclusionResolution:
        return getSettings().game.aoResolution.getValue();
    case GraphicsOption::AmbientOcclusionRadius:
        return static_cast<int>(getSettings().game.aoRadius.getValue() * 100.0f + 0.5f);
    case GraphicsOption::AmbientOcclusionStrength:
        return static_cast<int>(getSettings().game.aoIntensity.getValue() * 100.0f + 0.5f);
    case GraphicsOption::AmbientOcclusionContrast:
        return static_cast<int>(getSettings().game.aoPower.getValue() * 100.0f + 0.5f);
    case GraphicsOption::AmbientOcclusionNormalSmooth:
        return getSettings().game.aoNormalSmooth.getValue() ? 1 : 0;
    case GraphicsOption::AmbientOcclusionThickness:
        return static_cast<int>(getSettings().game.aoThickness.getValue() * 100.0f + 0.5f);
    case GraphicsOption::AmbientOcclusionTemporal:
        return getSettings().game.aoTemporal.getValue() ? 1 : 0;
    case GraphicsOption::AmbientOcclusionTemporalFrames:
        return getSettings().game.aoTemporalFrames.getValue();
    case GraphicsOption::AmbientOcclusionSharpness:
        return getSettings().game.aoSharpness.getValue();
    case GraphicsOption::AmbientOcclusionPostFilter:
        return getSettings().game.aoPostFilter.getValue() ? 1 : 0;
    case GraphicsOption::AmbientOcclusionMotionResponse:
        return getSettings().game.aoMotionResponse.getValue();
    }
    return 0;
}

void set_value(GraphicsOption option, int value) {
    switch (option) {
    case GraphicsOption::InternalResolution:
        getSettings().game.internalResolutionScale.setValue(value);
        VISetFrameBufferScale(static_cast<float>(value));
        break;
    case GraphicsOption::ShadowResolution:
        getSettings().game.shadowResolutionMultiplier.setValue(value);
        break;
    case GraphicsOption::Resampler: {
        const auto sampler = static_cast<Resampler>(std::clamp(value,
            static_cast<int>(Resampler::Bilinear),
            static_cast<int>(Resampler::Area)));
        getSettings().game.resampler.setValue(sampler);
        switch (sampler) {
        case Resampler::Area:
            aurora_set_resampler(SAMPLER_AREA);
            break;
        case Resampler::Bilinear:
        default:
            aurora_set_resampler(SAMPLER_BILINEAR);
            break;
        }
        break;
    }
    case GraphicsOption::Fxaa:
        getSettings().game.enableFxaa.setValue(static_cast<bool>(std::clamp(value, 0, 1)));
        aurora_set_fxaa_enabled(static_cast<bool>(std::clamp(value, 0, 1)));
        break;
    case GraphicsOption::Fsr1:
        getSettings().game.enableFsr1.setValue(static_cast<bool>(std::clamp(value, 0, 1)));
        aurora_set_fsr1_enabled(static_cast<bool>(std::clamp(value, 0, 1)));
        break;
    case GraphicsOption::BloomMode:
        getSettings().game.bloomMode.setValue(static_cast<BloomMode>(std::clamp(
            value, static_cast<int>(BloomMode::Off), static_cast<int>(BloomMode::Dusk))));
        break;
    case GraphicsOption::DepthOfFieldMode:
        getSettings().game.depthOfFieldMode.setValue(static_cast<DepthOfFieldMode>(std::clamp(
            value, static_cast<int>(DepthOfFieldMode::Off), static_cast<int>(DepthOfFieldMode::Dusk))));
        break;
    case GraphicsOption::BloomMultiplier:
        getSettings().game.bloomMultiplier.setValue(std::clamp(value, 0, 100) / 100.0f);
        break;
    case GraphicsOption::TextureReplacements:
        texture_replacements::set_enabled(static_cast<bool>(value));
        break;
    case GraphicsOption::AmbientOcclusion:
        getSettings().game.enableAmbientOcclusion.setValue(static_cast<bool>(std::clamp(value, 0, 1)));
        aurora_set_ao_enabled(static_cast<bool>(std::clamp(value, 0, 1)));
        break;
    case GraphicsOption::AmbientOcclusionQuality:
        getSettings().game.aoQuality.setValue(std::clamp(value, 0, 3));
        aurora_set_ao_quality(std::clamp(value, 0, 3));
        break;
    case GraphicsOption::AmbientOcclusionResolution:
        getSettings().game.aoResolution.setValue(std::clamp(value, 0, 2));
        break;
    case GraphicsOption::AmbientOcclusionRadius:
        getSettings().game.aoRadius.setValue(std::clamp(value, 25, 800) / 100.0f);
        break;
    case GraphicsOption::AmbientOcclusionStrength:
        getSettings().game.aoIntensity.setValue(std::clamp(value, 0, 300) / 100.0f);
        break;
    case GraphicsOption::AmbientOcclusionContrast:
        getSettings().game.aoPower.setValue(std::clamp(value, 50, 400) / 100.0f);
        break;
    case GraphicsOption::AmbientOcclusionNormalSmooth:
        getSettings().game.aoNormalSmooth.setValue(static_cast<bool>(std::clamp(value, 0, 1)));
        aurora_set_ao_normal_smooth(static_cast<bool>(std::clamp(value, 0, 1)));
        break;
    case GraphicsOption::AmbientOcclusionThickness:
        getSettings().game.aoThickness.setValue(std::clamp(value, 25, 400) / 100.0f);
        break;
    case GraphicsOption::AmbientOcclusionTemporal:
        getSettings().game.aoTemporal.setValue(static_cast<bool>(std::clamp(value, 0, 1)));
        aurora_set_ao_temporal(static_cast<bool>(std::clamp(value, 0, 1)));
        break;
    case GraphicsOption::AmbientOcclusionTemporalFrames:
        getSettings().game.aoTemporalFrames.setValue(std::clamp(value, 1, 12));
        aurora_set_ao_temporal_frames(std::clamp(value, 1, 12));
        break;
    case GraphicsOption::AmbientOcclusionSharpness:
        getSettings().game.aoSharpness.setValue(std::clamp(value, 0, 100));
        aurora_set_ao_denoise_sharpness(std::clamp(value, 0, 100) / 100.0f);
        break;
    case GraphicsOption::AmbientOcclusionPostFilter:
        getSettings().game.aoPostFilter.setValue(static_cast<bool>(std::clamp(value, 0, 1)));
        aurora_set_ao_post_filter(static_cast<bool>(std::clamp(value, 0, 1)));
        break;
    case GraphicsOption::AmbientOcclusionMotionResponse:
        getSettings().game.aoMotionResponse.setValue(std::clamp(value, 0, 100));
        aurora_set_ao_motion_response(std::clamp(value, 0, 100) / 100.0f);
        break;
    }
}

Rml::Element* create_stepped_carousel_root(Rml::Element* parent) {
    auto* doc = parent->GetOwnerDocument();
    auto root = doc->CreateElement("div");
    root->SetClass("stepped-carousel", true);
    root->SetAttribute("tabindex", "0");
    return parent->AppendChild(std::move(root));
}

Rml::Element* create_stepped_carousel_arrow(
    Rml::Element* parent, const Rml::String& className, const Rml::String& label) {
    auto* doc = parent->GetOwnerDocument();
    auto button = doc->CreateElement("button");
    button->SetClass("stepped-carousel-arrow", true);
    button->SetClass(className, true);
    button->SetInnerRML(label);
    return parent->AppendChild(std::move(button));
}

void update_carousel_arrow_color(Rml::Element* arrow, bool dim) {
    const Rml::Colourb& color = Rml::Colourb(255, 255, 255, dim ? 128 : 255);
    arrow->SetProperty(Rml::PropertyId::Color, Rml::Property(color, Rml::Unit::COLOUR));
}

}  // namespace

SteppedCarousel::SteppedCarousel(Rml::Element* parent, Props props)
    : Component(create_stepped_carousel_root(parent)), mProps(std::move(props)) {
    mPrevElem = create_stepped_carousel_arrow(mRoot, "prev", "&#xe5cb;");
    mValueElem = append(mRoot, "div");
    mValueElem->SetClass("stepped-carousel-value", true);
    mNextElem = create_stepped_carousel_arrow(mRoot, "next", "&#xe5cc;");

    listen(mPrevElem, Rml::EventId::Click,
        [this](Rml::Event&) { handle_nav_command(NavCommand::Left); });
    listen(mNextElem, Rml::EventId::Click,
        [this](Rml::Event&) { handle_nav_command(NavCommand::Right); });
    listen(mRoot, Rml::EventId::Keydown, [this](Rml::Event& event) {
        const auto cmd = map_nav_event(event);
        if (cmd != NavCommand::None && handle_nav_command(cmd)) {
            event.StopPropagation();
        }
    });
}

bool SteppedCarousel::focus() {
    return Component::focus();
}

void SteppedCarousel::update() {
    if (mValueElem == nullptr) {
        return;
    }
    const int value = std::clamp(mProps.getValue ? mProps.getValue() : 0, mProps.min, mProps.max);
    if (mProps.formatValue) {
        mValueElem->SetInnerRML(mProps.formatValue(value));
    } else {
        mValueElem->SetInnerRML(std::to_string(value));
    }

    update_carousel_arrow_color(mPrevElem, value == mProps.min);
    update_carousel_arrow_color(mNextElem, value == mProps.max);
}

bool SteppedCarousel::handle_nav_command(NavCommand cmd) {
    if (cmd == NavCommand::Left) {
        const int value = mProps.getValue ? mProps.getValue() : 0;
        apply(std::clamp(value - mProps.step, mProps.min, mProps.max));
        return true;
    }
    if (cmd == NavCommand::Right) {
        const int value = mProps.getValue ? mProps.getValue() : 0;
        apply(std::clamp(value + mProps.step, mProps.min, mProps.max));
        return true;
    }
    return false;
}

void SteppedCarousel::apply(int value) {
    const int nextValue = std::clamp(value, mProps.min, mProps.max);
    const int currentValue =
        std::clamp(mProps.getValue ? mProps.getValue() : 0, mProps.min, mProps.max);
    if (nextValue == currentValue) {
        return;
    }
    mDoAud_seStartMenu(kSoundItemChange);
    if (mProps.onChange) {
        mProps.onChange(nextValue);
    }
}

Rml::String format_graphics_setting_value(GraphicsOption option, int value) {
    switch (option) {
    case GraphicsOption::InternalResolution: {
        u32 width = 0;
        u32 height = 0;
        AuroraGetRenderSize(&width, &height);
        if (value <= 0) {
            return fmt::format("Auto ({}×{})", width, height);
        } else {
            return fmt::format("{}× ({}×{})", value, width, height);
        }
    }
    case GraphicsOption::ShadowResolution:
        return fmt::format("{}×", value);
    case GraphicsOption::Resampler:
        switch (static_cast<Resampler>(value)) {
        case Resampler::Bilinear:
            return "Bilinear";
        case Resampler::Area:
            return "Area";
        }
        break;
    case GraphicsOption::Fxaa:
        return static_cast<bool>(value) ? "On" : "Off";
    case GraphicsOption::Fsr1:
        return static_cast<bool>(value) ? "On" : "Off";
    case GraphicsOption::BloomMode:
        switch (static_cast<BloomMode>(value)) {
        case BloomMode::Off:
            return "Off";
        case BloomMode::Classic:
            return "Classic";
        case BloomMode::Dusk:
            return "Dusklight";
        }
        break;
    case GraphicsOption::DepthOfFieldMode:
        switch (static_cast<DepthOfFieldMode>(value)) {
        case DepthOfFieldMode::Off:
            return "Off";
        case DepthOfFieldMode::Classic:
            return "Classic";
        case DepthOfFieldMode::Dusk:
            return "Dusklight";
        }
        break;
    case GraphicsOption::BloomMultiplier:
        return fmt::format("{}%", value);
    case GraphicsOption::TextureReplacements:
        return static_cast<bool>(value) ? "On" : "Off";
    case GraphicsOption::AmbientOcclusion:
        return static_cast<bool>(value) ? "On" : "Off";
    case GraphicsOption::AmbientOcclusionQuality:
        switch (value) {
        case 0:
            return "Low";
        case 1:
            return "Medium";
        case 2:
            return "High";
        case 3:
            return "Ultra";
        }
        return "Ultra";
    case GraphicsOption::AmbientOcclusionResolution:
        switch (value) {
        case 0:
            return "Full";
        case 1:
            return "Half";
        case 2:
            return "Quarter";
        }
        return "Half";
    case GraphicsOption::AmbientOcclusionRadius:
    case GraphicsOption::AmbientOcclusionStrength:
    case GraphicsOption::AmbientOcclusionContrast:
    case GraphicsOption::AmbientOcclusionThickness:
    case GraphicsOption::AmbientOcclusionSharpness:
    case GraphicsOption::AmbientOcclusionMotionResponse:
        return fmt::format("{}%", value);
    case GraphicsOption::AmbientOcclusionTemporalFrames:
        return value == 1 ? Rml::String("1 frame") : fmt::format("{} frames", value);
    case GraphicsOption::AmbientOcclusionNormalSmooth:
    case GraphicsOption::AmbientOcclusionTemporal:
    case GraphicsOption::AmbientOcclusionPostFilter:
        return static_cast<bool>(value) ? "On" : "Off";
    }
    return "";
}

GraphicsTuner::GraphicsTuner(GraphicsTunerProps props, bool prelaunch)
    : Document(kDocumentSource), mOption(props.option), mValueMin(props.valueMin),
      mValueMax(props.valueMax), mDefaultValue(props.defaultValue), mPrelaunch(prelaunch) {
    if (mDocument == nullptr) {
        return;
    }

    if (auto* title = mDocument->GetElementById("title")) {
        title->SetInnerRML(escape(props.title));
    }
    if (auto* description = mDocument->GetElementById("description")) {
        description->SetInnerRML(escape(props.helpText));
    }
    if (auto* carouselParent = mDocument->GetElementById("carousel-container")) {
        mCarousel = &add_component<SteppedCarousel>(carouselParent,
            SteppedCarousel::Props{
                .min = mValueMin,
                .max = mValueMax,
                .step = props.step,
                .getValue = [this] { return get_value(mOption); },
                .onChange = [this](int value) { set_value(mOption, value); },
                .formatValue =
                    [this](int value) { return format_graphics_setting_value(mOption, value); },
            });
    }

    if (auto* footer = mDocument->GetElementById("footer")) {
        auto& returnButton = add_component<Button>(footer, "\xE2\x86\x90 Return", "footer-button")
                                 .on_pressed([this] { pop(); });
        returnButton.root()->SetClass("return", true);
        auto& resetButton =
            add_component<Button>(footer, "Reset to default", "footer-button").on_pressed([this] {
                mDoAud_seStartMenu(kSoundItemChange);
                reset_default();
            });
        resetButton.root()->SetClass("reset", true);
    }

    // Hide document after transition completion
    mRoot = mDocument->GetElementById("root");
    listen(mRoot, Rml::EventId::Transitionend, [this](Rml::Event& event) {
        if (event.GetTargetElement() == mRoot && !mRoot->HasAttribute("open") &&
            Document::visible())
        {
            Document::hide(mPendingClose);
        }
    });
}

void GraphicsTuner::show() {
    Document::show();
    mRoot->SetAttribute("open", "");
    mDoAud_seStartMenu(kSoundWindowOpen);
}

void GraphicsTuner::hide(bool close) {
    config::Save();
    mRoot->RemoveAttribute("open");
    if (close) {
        mPendingClose = true;
        mDoAud_seStartMenu(kSoundWindowClose);
    }
}

void GraphicsTuner::update() {
    for (const auto& component : mComponents) {
        component->update();
    }
    Document::update();
}

bool GraphicsTuner::focus() {
    for (const auto& component : mComponents) {
        if (component->focus()) {
            return true;
        }
    }
    return false;
}

bool GraphicsTuner::visible() const {
    return mRoot->HasAttribute("open");
}

bool GraphicsTuner::handle_nav_command(Rml::Event& event, NavCommand cmd) {
    if (cmd == NavCommand::Cancel) {
        pop();
        return true;
    }

    if (mCarousel && mCarousel->handle_nav_command(cmd)) {
        return true;
    }

    return mPrelaunch ? false : Document::handle_nav_command(event, cmd);
}

void GraphicsTuner::reset_default() {
    set_value(mOption, mDefaultValue);
}

}  // namespace dusk::ui
