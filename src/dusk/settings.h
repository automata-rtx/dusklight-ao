#pragma once

#include <array>

#include "dusk/config_var.hpp"
#include "dusk/ui/controls.hpp"

namespace dusk {

using config::ConfigVar;
using config::ActionBindConfigVar;

enum class BloomMode : int {
    Off = 0,
    Classic = 1,
    Dusk = 2,
};

enum class DepthOfFieldMode : int {
    Off = 0,
    Classic = 1,
    Dusk = 2,
};

enum class Resampler : int {
    Bilinear = 0,
    Area = 1,
};

enum class GameLanguage : u8 {
    English = OS_LANGUAGE_ENGLISH,
    German = OS_LANGUAGE_GERMAN,
    French = OS_LANGUAGE_FRENCH,
    Spanish = OS_LANGUAGE_SPANISH,
    Italian = OS_LANGUAGE_ITALIAN,
};

enum class DiscVerificationState : u8 {
    Unknown = 0,
    Success,
    HashMismatch,
};

enum class FrameInterpMode : u8 {
    Off = 0,
    Capped = 1,
    Unlimited = 2,
};

enum class TouchTargeting : u8 {
    Hybrid = 0,
    Hold = 1,
    Switch = 2,
};

enum class MenuScaling : u8 {
    GameCube = 0,
    Wii = 1,
    Dusklight = 2,
};

enum class MagicArmorMode : u8 {
    NORMAL = 0,
    ON_DAMAGE = 1,
    DOUBLE_DEFENSE = 2,
    INVINCIBLE = 3,
    COSMETIC = 4,
};

namespace config {
template <>
struct ConfigEnumRange<BloomMode> {
    static constexpr auto min = BloomMode::Off;
    static constexpr auto max = BloomMode::Dusk;
};

template <>
struct ConfigEnumRange<DepthOfFieldMode> {
    static constexpr auto min = DepthOfFieldMode::Off;
    static constexpr auto max = DepthOfFieldMode::Dusk;
};

template <>
struct ConfigEnumRange<Resampler> {
    static constexpr auto min = Resampler::Bilinear;
    static constexpr auto max = Resampler::Area;
};

template <>
struct ConfigEnumRange<GameLanguage> {
    static constexpr auto min = GameLanguage::English;
    static constexpr auto max = GameLanguage::Italian;
};

template <>
struct ConfigEnumRange<DiscVerificationState> {
    static constexpr auto min = DiscVerificationState::Unknown;
    static constexpr auto max = DiscVerificationState::HashMismatch;
};

template <>
struct ConfigEnumRange<FrameInterpMode> {
    static constexpr auto min = FrameInterpMode::Off;
    static constexpr auto max = FrameInterpMode::Unlimited;
};

template <>
struct ConfigEnumRange<TouchTargeting> {
    static constexpr auto min = TouchTargeting::Hybrid;
    static constexpr auto max = TouchTargeting::Switch;
};

template <>
struct ConfigEnumRange<MenuScaling> {
    static constexpr auto min = MenuScaling::GameCube;
    static constexpr auto max = MenuScaling::Dusklight;
};

template <>
struct ConfigEnumRange<MagicArmorMode> {
    static constexpr auto min = MagicArmorMode::NORMAL;
    static constexpr auto max = MagicArmorMode::COSMETIC;
};

template <>
struct ConfigValueTraits<ui::ControlLayout> {
    static constexpr bool enabled = true;
};
}  // namespace config

// Persistent user settings

struct UserSettings {
    // Program settings

    struct {
        // Video
        ConfigVar<bool> enableFullscreen;
        ConfigVar<bool> enableVsync;
        ConfigVar<bool> lockAspectRatio;
        ConfigVar<bool> enableFpsOverlay;
        ConfigVar<int> fpsOverlayCorner;
        ConfigVar<int> maxFrameRate;
        ConfigVar<bool> rememberWindowSize;
        ConfigVar<int> lastWindowWidth;
        ConfigVar<int> lastWindowHeight;
    } video;

    struct {
        // Audio
        ConfigVar<int> masterVolume;
        ConfigVar<int> mainMusicVolume;
        ConfigVar<int> subMusicVolume;
        ConfigVar<int> soundEffectsVolume;
        ConfigVar<int> fanfareVolume;
        ConfigVar<bool> enableReverb;
        ConfigVar<bool> enableHrtf;
        ConfigVar<bool> menuSounds;
    } audio;

    // Game settings

    struct {
        ConfigVar<GameLanguage> language;

        // QoL
        ConfigVar<bool> enableQuickTransform;
        ConfigVar<bool> hideTvSettingsScreen;
        ConfigVar<bool> biggerWallets;
        ConfigVar<bool> noReturnRupees;
        ConfigVar<bool> disableRupeeCutscenes;
        ConfigVar<bool> noSwordRecoil;
        ConfigVar<int> damageMultiplier;
        ConfigVar<bool> noHeartDrops;
        ConfigVar<bool> instantDeath;
        ConfigVar<bool> fastClimbing;
        ConfigVar<bool> noMissClimbing;
        ConfigVar<bool> fastTears;
        ConfigVar<bool> no2ndFishForCat;
        ConfigVar<bool> buttonFishing;
        ConfigVar<bool> instantSaves;
        ConfigVar<bool> instantText;
        ConfigVar<bool> sunsSong;
        ConfigVar<bool> autoSave;
        ConfigVar<bool> enhancedMapMenus;

        // Preferences
        ConfigVar<bool> enableMirrorMode;
        ConfigVar<bool> minimalHUD;
        ConfigVar<float> hudScale;
        ConfigVar<bool> pauseOnFocusLost;
        ConfigVar<bool> enableLinkDollRotation;
        ConfigVar<bool> enableAchievementToasts;
        ConfigVar<bool> enableControllerToasts;
        ConfigVar<bool> enableDiscordPresence;
        ConfigVar<MenuScaling> menuScalingMode;

        // Graphics
        ConfigVar<BloomMode> bloomMode;
        ConfigVar<float> bloomMultiplier;
        ConfigVar<bool> remixKankyoBridge;
        ConfigVar<bool> remixSunMoonLight;
        ConfigVar<float> remixSunIntensity;
        ConfigVar<float> remixMoonIntensity;
        ConfigVar<float> remixCelestialAngle;
        ConfigVar<bool> remixLocalLights;
        ConfigVar<float> remixLocalLightIntensity;
        ConfigVar<float> remixLocalLightRadius;
        // Effect lights: sphere lights at the origin of the game's own fire and glow effects,
        // rather than at the positions of the game's registered lights. docs/effect-lights.md.
        ConfigVar<bool> effectLights;
        ConfigVar<float> effectLightIntensity;
        ConfigVar<float> effectLightMassExponent;
        ConfigVar<float> effectLightDerivedIntensity;
        ConfigVar<float> effectLightDerivedReach;
        ConfigVar<float> effectLightDerivedRadius;
        ConfigVar<float> effectLightUndeterminedIntensity;
        ConfigVar<float> effectLightUndeterminedReach;
        ConfigVar<float> effectLightUndeterminedRadius;
        ConfigVar<float> effectLightFireOffset;
        ConfigVar<float> effectLightGlowOffset;
        ConfigVar<float> effectLightMergeRadius;
        ConfigVar<float> effectLightAdoptRadius;
        ConfigVar<int> effectLightMaxLights;
        ConfigVar<float> effectLightMaxDistance;
        ConfigVar<bool> effectLightBursts;
        ConfigVar<float> effectLightMinChroma;
        ConfigVar<float> effectLightMinLuma;
        ConfigVar<float> effectLightVolumetric;
        ConfigVar<bool> disableFrustumCulling;
        ConfigVar<float> celestialNoonElevation;
        ConfigVar<bool> remixHideSkyBillboards;
        // Whether remixHideSkyBillboards also takes the star packet with it. Defaults to
        // true, which is what that switch has always done - the two were one setting until
        // 2026-08-11. It is a sub-switch, not an independent one: with
        // remixHideSkyBillboards off nothing is hidden either way. Turning this off while
        // that one is on is the A/B that isolates which packet is the shadow occluder,
        // because only the sun packet draws the moon quad. docs/remix-test-playbook.md §4b.
        ConfigVar<bool> remixHideStarBillboards;
        ConfigVar<bool> remixHideVrbox;
        // Draw each blade from its display list with its own position matrix instead of
        // batching a room into one dynamic stream, so Remix sees a stable asset hash.
        // Covers dGrass_packet_c ONLY. daGrass_c also spawns flowers - kind 2 and kind 3
        // go to dFlower_packet_c (d_a_grass.cpp:322), whose draw batches identically and
        // has no switch. Driven from Remix's overlay via rtx.dusklight.game.perBladeGrass.
        ConfigVar<bool> remixPerBladeGrass;
        // The same thing for dFlower_packet_c - the hana (flower) half of the same actor,
        // kinds 2 and 3. A separate switch rather than a widening of the one above: a flower
        // is a bigger template than a blade so the cost profile differs, and neither half has
        // been tested in game, so one test session can answer both questions independently.
        // Driven from Remix's overlay via rtx.dusklight.game.perBladeFlowers.
        ConfigVar<bool> remixPerBladeFlowers;
        // Hand the texture_replacements pack to Remix on the D3D9 backend. Separate from
        // enableTextureReplacements so the pack can be registered for the WebGPU backends
        // without also being handed over, and so a pack problem can be isolated without
        // turning replacements off everywhere. Read once at launch; there is no live toggle.
        ConfigVar<bool> remixTextureReplacements;
        // Suppress the speed effect spawned while Epona dashes. It is placed in front of
        // the camera rather than in the world, so under Remix it is captured as a
        // translucent quad travelling with the view instead of composited over the frame.
        // Driven from Remix's overlay via rtx.dusklight.game.hideDashEffect, which defaults
        // to on and pushes that down every frame; this defaults to off so other backends
        // keep the vanilla effect. Suppressing it is a hypothesis about the
        // water-while-dashing report rather than a confirmed cause - see that option.
        ConfigVar<bool> remixHideDashEffect;
        // Suppress the game's SIMPLE ground shadows; Remix traces real ones from the
        // geometry, so the painted disc lands on top of a correct shadow. This is every
        // actor that registers one - not just items - because it is dropped inside
        // dDlst_shadowControl_c::setSimple, which is the single funnel behind all 50
        // dComIfGd_setSimpleShadow call sites: items, pots, insects, enemies, NPCs
        // (daNpcT_c::draw covers 51 derived classes) and cutscene actors. The game's
        // projected shadows (dDlst_shadowReal_c, which the debug labels call "riaru
        // kage") are a separate system and are untouched.
        // Driven from Remix's overlay via rtx.dusklight.game.blobShadows.
        ConfigVar<bool> remixBlobShadows;
        // Keep Link's lantern permanently fuelled. Driven from Remix's overlay via
        // rtx.dusklight.game.lanternInfiniteOil; a gameplay change, off by default.
        ConfigVar<bool> remixLanternInfiniteOil;
        ConfigVar<bool> freezeTime;
        ConfigVar<float> timeOfDay;
        ConfigVar<int> timeCommit;
        // Three of the original team's own environment sliders, driven from Remix's overlay
        // via rtx.dusklight.game.{waterSurfaceShine,grassLightInfluence,clockRate}. Their
        // labels, fields and ranges are extracted in docs/kankyo-tuning-surface.md; the
        // panel they came from is compiled out of every build (one #if DEBUG covering all of
        // d_kankyo.cpp's genMessage functions), so this is the only way to reach them.
        //
        // Each defaults to the value envcolor_init() gives the field, so a config file that
        // has never been touched leaves the game exactly as it was. The bridge applies these
        // to g_env_light rather than the consumers reading them, which is the reverse of the
        // usual convention here - see the note in remix_bridge.cpp's applyKankyoTuning.
        ConfigVar<float> waterSurfaceShine;
        ConfigVar<float> grassLightInfluence;
        ConfigVar<float> clockRate;
        ConfigVar<DepthOfFieldMode> depthOfFieldMode;
        ConfigVar<bool> disableWaterRefraction;
        ConfigVar<bool> skinDebugView;
        ConfigVar<bool> enableTextureReplacements;
        // Write every texture the replacement registry is asked for and cannot satisfy to
        // <cachePath>/texture_dumps/, as a .dds named with the exact key a pack file must
        // carry. Off by default because the directory grows for as long as it is on.
        // NOTE: aurora only reaches that path from its GX texture resolver, so this
        // produces files on the WebGPU backends and none at all under D3D9/Remix - see
        // docs/remix-test-playbook.md "Dumping textures under their pack filenames".
        ConfigVar<bool> allowTextureDumps;
        ConfigVar<FrameInterpMode> enableFrameInterpolation;
        ConfigVar<int> internalResolutionScale;
        ConfigVar<int> shadowResolutionMultiplier;
        ConfigVar<Resampler> resampler;
        ConfigVar<bool> enableMapBackground;
        ConfigVar<bool> disableCutscenePillarboxing;

        // Audio
        ConfigVar<bool> noLowHpSound;
        ConfigVar<bool> midnasLamentNonStop;

        // Input
        ConfigVar<bool> enableGyroAim;
        ConfigVar<bool> enableGyroRollgoal;
        ConfigVar<float> gyroSensitivityX;
        ConfigVar<float> gyroSensitivityY;
        ConfigVar<float> gyroSensitivityRollgoal;
        ConfigVar<float> gyroSmoothing;
        ConfigVar<float> gyroDeadband;
        ConfigVar<bool> gyroInvertPitch;
        ConfigVar<bool> gyroInvertYaw;
        ConfigVar<bool> enableMouseCamera;
        ConfigVar<bool> enableMouseAim;
        ConfigVar<float> mouseAimSensitivity;
        ConfigVar<float> mouseCameraSensitivity;
        ConfigVar<bool> invertMouseY;
        ConfigVar<bool> freeCamera;
        ConfigVar<bool> enableTouchControls;
        ConfigVar<TouchTargeting> touchTargeting;
        ConfigVar<bool> enableMenuPointer;
        ConfigVar<ui::ControlLayout> touchControlsLayout;
        ConfigVar<bool> invertCameraXAxis;
        ConfigVar<bool> invertCameraYAxis;
        ConfigVar<bool> invertFirstPersonXAxis;
        ConfigVar<bool> invertFirstPersonYAxis;
        ConfigVar<bool> invertAirSwimX;
        ConfigVar<bool> invertAirSwimY;
        ConfigVar<float> freeCameraXSensitivity;
        ConfigVar<float> freeCameraYSensitivity;
        ConfigVar<float> touchCameraXSensitivity;
        ConfigVar<float> touchCameraYSensitivity;
        ConfigVar<bool> debugFlyCam;
        ConfigVar<bool> debugFlyCamLockEvents;
        ConfigVar<bool> allowBackgroundInput;
        std::array<ConfigVar<bool>, 4> enableLED;
        ConfigVar<bool> swapDirectSelect;

        // Cheats
        ConfigVar<bool> infiniteHearts;
        ConfigVar<bool> infiniteArrows;
        ConfigVar<bool> infiniteSeeds;
        ConfigVar<bool> infiniteBombs;
        ConfigVar<bool> infiniteOil;
        ConfigVar<bool> infiniteOxygen;
        ConfigVar<bool> infiniteRupees;
        ConfigVar<bool> enableIndefiniteItemDrops;
        ConfigVar<bool> moonJump;
        ConfigVar<bool> superClawshot;
        ConfigVar<bool> alwaysGreatspin;
        ConfigVar<bool> enableFastIronBoots;
        ConfigVar<bool> canTransformAnywhere;
        ConfigVar<bool> fastRoll;
        ConfigVar<bool> fastSpinner;
        ConfigVar<MagicArmorMode> armorRupeeDrain;
        ConfigVar<bool> invincibleEnemies;

        // Technical
        ConfigVar<bool> restoreWiiGlitches;

        // Controls
        ConfigVar<bool> enableTurboKeybind;
        ConfigVar<bool> enableResetKeybind;

        // Tools
        ConfigVar<bool> speedrunMode;
        ConfigVar<bool> liveSplitEnabled;
        ConfigVar<bool> showSpeedrunRTATimer;
        ConfigVar<bool> recordingMode;
        ConfigVar<bool> removeQuestMapMarkers;
        ConfigVar<bool> showInputViewer;
        ConfigVar<bool> showInputViewerGyro;
    } game;

    struct {
        ConfigVar<std::string> isoPath;
        ConfigVar<DiscVerificationState> isoVerification;
        ConfigVar<std::string> graphicsBackend;
        ConfigVar<bool> skipPreLaunchUI;
        ConfigVar<bool> wasPresetChosen;
        ConfigVar<bool> checkForUpdates;
        ConfigVar<int> cardFileType;
        ConfigVar<bool> enableAdvancedSettings;
    } backend;

    // Arrays of size 4 for 4 ports
    struct {
        std::array<ActionBindConfigVar, 4> firstPersonCamera;
        std::array<ActionBindConfigVar, 4> callMidna;
        std::array<ActionBindConfigVar, 4> openMapScreen;
        std::array<ActionBindConfigVar, 4> toggleMinimap;
        std::array<ActionBindConfigVar, 4> openDusklightMenu;
        std::array<ActionBindConfigVar, 4> turboSpeedButton;
    } actionBindings;
};

UserSettings& getSettings();

void registerSettings();

// Transient settings

struct CollisionViewSettings {
    bool enableTerrainView;
    bool enableWireframe;
    bool enableAtView;
    bool enableTgView;
    bool enableCoView;
    float terrainViewOpacity;
    float colliderViewOpacity;
    float drawRange;
};

struct TransientSettings {
    CollisionViewSettings collisionView;
    bool skipFrameRateLimit;
    bool moveLinkActive;
    bool stateShareLoadActive;
};

TransientSettings& getTransientSettings();

}  // namespace dusk
