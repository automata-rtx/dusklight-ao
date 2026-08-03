# Raising the sun/moon elevation cap

Vanilla caps the sun at **59° elevation**. This describes how that cap works
and how it was lifted, for anyone adapting lighting code (shadow maps, etc.)
that inherits it.

## Where the cap comes from

`dScnKy_env_light_c::setSunpos()` (`src/d/d_kankyo.cpp`) places the body on an
ellipse around the camera eye:

```cpp
pos.x = sinf(DEG_TO_RAD(sun_angle)) * 80000.0f;
pos.y = cosf(DEG_TO_RAD(sun_angle)) * 80000.0f;
pos.z = cosf(DEG_TO_RAD(sun_angle)) * -48000.0f;   // <- the cap

sun_pos  = eye + (pos.x, -pos.y, pos.z);   // absolute
moon_pos =       (pos.x, -pos.y, pos.z);   // offset only, not absolute
```

`y` and `z` are both driven by `cos(a)`, so the path is a **great circle
tilted off vertical**, not an ellipse in the usual sense. With
`z/xy = 48000/80000 = 0.6` the peak elevation is:

```
peak = atan(1 / 0.6) = 59.04°
```

Nothing else limits it — the angle sweep `a` covers a full circle. The tilt
is the whole story.

## The change

Invert the relation (`ratio = cot(peak)`) so the knob is the angle:

```cpp
// src/d/d_kankyo.cpp
f32 dKy_celestial_orbit_z_ratio() {
    const f32 elevation = std::clamp(
        dusk::getSettings().game.celestialNoonElevation.getValue(), 1.0f, 90.0f);
    const f32 radians = elevation * (M_PI / 180.0f);
    const f32 sinE = std::sin(radians);
    return sinE <= 0.0f ? 0.0f : std::cos(radians) / sinE;
}
```

Then in `setSunpos`, for **both** the sun and moon blocks:

```cpp
const f32 orbitZ = 80000.0f * dKy_celestial_orbit_z_ratio();
...
pos.z = cosf(DEG_TO_RAD(sun_angle)) * -orbitZ;    // was -48000.0f
```

Setting: `game.celestialNoonElevation`, default **59.036** — that reproduces
`48000/80000` to six decimal places (0.600006), so the default is vanilla.
Declared in `src/dusk/settings.{h,cpp}`; prototype in `include/d/d_kankyo.h`.

## What moves as a result

Everything downstream of `sun_pos` / `moon_pos`, which is what matters for a
shadow map:

| Consumer | Path |
| :-- | :-- |
| **Day shadow light** | `dKy_setLight()` copies `sun_pos` → `sun_light_pos` every frame; `SetBaseLight` uses it as `base_light.mPosition` while `67.5 < daytime < 292.5` |
| **Night shadow light** | `SetBaseLight` uses `eye + moon_pos` outside that window |
| Weather light dir | `d_kankyo_rain.cpp:214,452` — `dKyr_get_vectle_calc(eye, sun_light_pos, …)` |
| Sun billboard, lens flare | `mpSunPacket` / `mpSunLenzPacket` |
| Moon billboard | `dKyr_drawStar` — `eye + moon_pos` |

So the visible body and the light track together; there is no second place
to patch.

Direction to the body, if you want it analytically instead of differencing
positions (the eye cancels, the radii cancel under normalization):

```
toBody = normalize( sin a, -cos a, -cos a * ratio )
```

with `a` from `setSunpos`'s piecewise remap of `daytime` (0–360):

```
daytime in [90, 270]:  a = ((daytime - 90) / 180) * 150 + 105
otherwise:             a = ((daytime' - 270) / 180) * 210 + 255,  wrapped
                       where daytime' = daytime < 90 ? daytime + 360 : daytime
```

## What does not move

**Time of day and every state transition.** `daytime`, the palette schedule,
and dawn/dusk/night selection run off `dComIfGs_getTime()` and
`l_time_attribute`; none of them reads `sun_pos`, `moon_pos`, or the orbit.
`SetBaseLight` picks sun vs moon from `daytime` alone. There is no path from
this value to any timing.

Sunrise/sunset elevations also barely move, so those transitions look the
same:

| `celestialNoonElevation` | 06:00 | 08:00 | 12:00 | 16:00 | 18:00 |
| :-- | :-: | :-: | :-: | :-: | :-: |
| 59.0 (vanilla) | 14.8 | 36.9 | **59.0** | 36.9 | 14.8 |
| 75.0 | 15.0 | 39.3 | **75.0** | 39.3 | 15.0 |
| 85.0 | 15.0 | 39.9 | **85.0** | 39.9 | 15.0 |
| 90.0 | 15.0 | 40.0 | **90.0** | 40.0 | 15.0 |

## Caveat

At exactly **90** the arc passes through the zenith, so the azimuth flips
instantaneously at noon and shadows can snap around. **80–85 is the safer
ceiling** if anything derives a stable azimuth from the light — which a
shadow map's view matrix does, since its up vector degenerates as the light
direction approaches vertical.

## Settled value

**80.** Tested in game 2026-07-28 and chosen by the owner, deliberately short
of 90 for the reason above. It is in the recommended `rtx.conf` in
`dx9-fixed-function.md` as `rtx.dusklight.game.celestialNoonElevation = 80`.
The code default stays at 59.036 so that a build with no configuration behaves
exactly like vanilla.

This is one of the few things in the Remix work that is **tested and settled**
rather than built-and-unrun — see `kankyo-remix.md` for
which side of that line everything else sits on.
