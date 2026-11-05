# UE5 (5.3 source, applies to 5.4–5.7) — why runtime DOF writes produce no blur

Source of quotes: public mirror `github.com/chenyong2github/UnrealEngine` branch `5.3`
(Epic's own repo is private; the mirror is byte-identical UE 5.3 source). Line numbers are from that branch.
Everything below marked **[verified]** was read directly in that source; **[unverified]** = could not confirm.

---

## 0. Verdict first

Two independent injection paths are being used (CameraCache POV + spawned unbound volume). Both failing
points at a **global** gate, not at the values:

1. `r.DepthOfFieldQuality == 0` (hard off; `sg.PostProcessQuality@0` sets exactly this) — most common in shipping.
2. `EngineShowFlags.DepthOfField == false` (game replaced engine DOF, e.g. photo mode / custom PP material).
3. `CameraCache.POV` is **rebuilt from scratch every camera update** — your write only survives if it lands
   *after* `APlayerCameraManager::UpdateCamera` and *before* `ULocalPlayer::CalcSceneView`.
4. The spawned volume never entered `UWorld::PostProcessVolumes` (registration happens in
   `APostProcessVolume::PostRegisterAllComponents`).
5. The numbers themselves are weak at wide FOV (see §4 math).

---

## 1. `r.DepthOfFieldQuality`

**Verdict:** 0 = off (hard). 1–4 = same DiaphragmDOF, only "how many samples" differ (and in 5.3 the value
itself is only read as `> 0`; the per-level differences come from `[PostProcessQuality@N]` ini cvars).
There is **no** Gaussian fallback on desktop — Gaussian DOF survives only in the mobile/ES3.1 renderer,
which indeed ignores Fstop/SensorWidth.

Help text (still shipped): `0: Off 1: Low 2: high quality (default) 3: very high (CircleDOF only) 4: extremely high`
— https://ue5consolecommands.com/command/r-depthoffieldquality/ , https://unrealdirective.com/resources/console-variables/r-depthoffieldquality/
(the "GaussianDOF is either 0 for off, otherwise on" sentence is stale 4.x text).

**[verified]** the only gate:
```cpp
// Engine/Source/Runtime/Renderer/Private/PostProcess/DiaphragmDOF.cpp:1329
bool DiaphragmDOF::IsEnabled(const FViewInfo& View)
{
    static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DepthOfFieldQuality"));
    const bool bDepthOfFieldRequestedByCVar = CVar->GetValueOnAnyThread() > 0;
    return
        DiaphragmDOF::IsSupported(View.GetShaderPlatform()) &&
        View.Family->EngineShowFlags.DepthOfField &&
        bDepthOfFieldRequestedByCVar &&
        !(View.Family->EngineShowFlags.PathTracing && View.FinalPostProcessSettings.PathTracingEnableReferenceDOF) &&
        ((View.FinalPostProcessSettings.DepthOfFieldFstop > 0.f && View.FinalPostProcessSettings.DepthOfFieldFocalDistance > 0.f)
          || View.FinalPostProcessSettings.DepthOfFieldDepthBlurRadius > 0.f);
}
```
https://github.com/chenyong2github/UnrealEngine/blob/5.3/Engine/Source/Runtime/Renderer/Private/PostProcess/DiaphragmDOF.cpp#L1329-L1342

**[verified]** `r.DepthOfFieldQuality` has exactly 2 read sites in the paths I checked:
* `DiaphragmDOF::IsEnabled` (`> 0`)
* `FSceneView::EndFinalPostprocessSettings` (`<= 0` → `FinalPostProcessSettings.DepthOfFieldScale = 0`)
  SceneView.cpp:1982 / :1988

**[verified]** Desktop old paths are dead: `PostProcessing.cpp` includes `PostProcessBokehDOF.h` but never calls
`AddBokehDOFPasses`; the deferred path calls `DiaphragmDOF::AddPasses` only (PostProcessing.cpp:564, :683).
`EDepthOfFieldMethod` (DOFM_BokehDOF/GaussianDOF/CircleDOF) exists only as `DepthOfFieldMethod_DEPRECATED`
inside `#if WITH_EDITORONLY_DATA` (Scene.h:28-34, :1329).

**[verified]** The surviving "Gaussian" path is mobile-only:
```cpp
// Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessMobile.h:16
// return Depth of Field Scale if Gaussian DoF mode is active. 0.0f otherwise.
float GetMobileDepthOfFieldScale(const FViewInfo& View);
```
and PostProcessing.cpp:2065 `bool bUseDof = GetMobileDepthOfFieldScale(View) > 0.0f && ...DepthOfField && ...`
with `bUseMobileDof = bUseDof && !View.FinalPostProcessSettings.bMobileHQGaussian;`.
That path uses `DepthOfFieldNearBlurSize/FarBlurSize` + `DepthOfFieldScale` (see `IsGaussianActive`,
PostProcessing.cpp:1940) and never reads Fstop/SensorWidth.

**Test:** run `r.DepthOfFieldQuality 4` in the game. If nothing changes, the gate is not this cvar.
Also print `r.DepthOfFieldQuality` at startup (a DeviceProfile/scalability ini may set it after your mod).

---

## 2. Every known gate that can suppress DOF

**[verified]** unless noted.

| # | Gate | Where | Effect |
|---|------|-------|--------|
| 1 | `r.DepthOfFieldQuality <= 0` | DiaphragmDOF.cpp:1329 | hard off |
| 2 | `sg.PostProcessQuality` level 0 | `BaseScalability.ini [PostProcessQuality@0] r.DepthOfFieldQuality=0` | hard off (shipping "Low") |
| 3 | `EngineShowFlags.DepthOfField` | DiaphragmDOF.cpp:1338 | hard off (also zeroes `DepthOfFieldScale`, SceneView.cpp:1988) |
| 4 | `EngineShowFlags.PostProcessing` | `IsPostProcessingEnabled()`, PostProcessing.cpp:166 | hard off (whole PP chain) |
| 5 | `FDataDrivenShaderPlatformInfo::GetSupportsDiaphragmDOF(platform)` | DiaphragmDOF.cpp `IsSupported` | off on platforms without it (ES3.1/mobile) |
| 6 | `DepthOfFieldFstop <= 0` or `DepthOfFieldFocalDistance <= 0` | same `IsEnabled` | off unless `DepthOfFieldDepthBlurRadius > 0` |
| 7 | `r.DOF.Kernel.MaxForegroundRadius` / `MaxBackgroundRadius` (default 0.025; Low = 0.006 in BaseScalability.ini:412-413) | `FPhysicalCocModel::Compile`, DiaphragmDOFUtils.cpp:108-109 | clamps CoC (0 → no blur) |
| 8 | `bOverride_DepthOfFieldXxx == false` | `LERP_PP` macro, SceneView.cpp:1440 | field silently ignored |
| 9 | `Weight <= 0` in `OverridePostProcessSettings` | SceneView.cpp:1449 | whole struct ignored |
| 10 | PathTracing + `PathTracingEnableReferenceDOF` | `IsEnabled` | engine DOF replaced by reference DOF |
| 11 | mobile only: `bMobileHQGaussian`, `DepthOfFieldScale`, Near/FarBlurSize | PostProcessMobile | Gaussian behaviour |

**Do not exist** (searched 5.3 source, no hits): `r.DOF.Method`, `r.DisableDepthOfField`, `r.PostProcessing`.
`r.DefaultFeature.*` has Bloom/AO/AutoExposure/MotionBlur/LensFlare/AntiAliasing but **no DOF** entry
(`FSceneView::StartFinalPostprocessSettings` only applies those, SceneView.cpp:1808+).

**[verified]** but note: `DepthOfFieldScale` is **not** a desktop gate — the default is already 0
(`Scene.cpp:590 DepthOfFieldScale = 0.0f;`) and DiaphragmDOF never reads it. Writing 0 to it changes nothing.

**Test:** in one frame, log `r.DepthOfFieldQuality`, `ShowFlags.DepthOfField`, `ShowFlags.PostProcessing`, and
whether `FinalPostProcessSettings.DepthOfFieldFstop` (render thread view) is what you wrote.

---

## 3. Is `CameraCache.POV.PostProcessSettings` used? And is it wiped?

**Verdict:** Yes it is used — but it is **rebuilt every camera update**, so a write is only live if it lands
between `UpdateCamera` and `CalcSceneView`. Also: `APlayerCameraManager::GetCameraView` **no longer exists**
in UE5, and `CameraCache` was renamed `CameraCachePrivate` in 5.0.

**[verified]** consumer side:
```cpp
// Engine/Source/Runtime/Engine/Private/LocalPlayer.cpp:687 (ULocalPlayer::GetViewPoint)
OutViewInfo = PlayerController->PlayerCameraManager->GetCameraCacheView();
OutViewInfo.FOV = PlayerController->PlayerCameraManager->GetFOVAngle();

// Engine/Source/Runtime/Engine/Private/LocalPlayer.cpp:895 (ULocalPlayer::CalcSceneView)
View->OverridePostProcessSettings(ViewInfo.PostProcessSettings, ViewInfo.PostProcessBlendWeight);
```
so `CameraCache.POV.PostProcessSettings` **is** the source of the view's PP settings.

**[verified]** producer side — wiped every frame:
```cpp
// PlayerCameraManager.cpp:345
void APlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
    ...
    static const FMinimalViewInfo DefaultViewInfo;
    OutVT.POV = DefaultViewInfo;              // :358  <-- full reset, PP settings included
    ...
    OutVT.POV.PostProcessBlendWeight = 1.0f;   // :364
    ...
    CamActor->GetCameraComponent()->GetCameraView(DeltaTime, OutVT.POV);   // or target->CalcCamera()
```
```cpp
// PlayerCameraManager.cpp:824 DoUpdateCamera -> :990
void APlayerCameraManager::FillCameraCache(const FMinimalViewInfo& NewInfo) { ... CameraCache.POV = NewInfo; }
```
Call chain: `APlayerController::UpdateCameraManager` (PlayerController.cpp:5901) → `PlayerCameraManager->UpdateCamera`
(PlayerCameraManager.cpp:767) → `DoUpdateCamera` → `UpdateViewTarget` → `FillCameraCache`.

**[verified]** and `UCameraComponent::GetCameraView` overwrites the whole struct too:
```cpp
DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;   // default 1.0
if (PostProcessBlendWeight > 0.0f) { DesiredView.PostProcessSettings = PostProcessSettings; }
```

**[verified]** API rename:
```cpp
// Classes/Camera/PlayerCameraManager.h:273
struct FCameraCacheEntry CameraCachePrivate;        // was `CameraCache` in UE4
ENGINE_API virtual void SetCameraCachePOV(const FMinimalViewInfo& InPOV);
ENGINE_API virtual const FMinimalViewInfo& GetCameraCacheView() const;
UE_DEPRECATED(5.0, ...) ENGINE_API virtual FMinimalViewInfo GetCameraCachePOV() const;
```
`GetCameraView(float, FMinimalViewInfo&)` is gone from the 5.3 header — only `GetCameraViewPoint` /
`GetCameraCacheView` remain.

**Test (decisive):** write, then read back *at the end of the frame* (post `UWorld::Tick`, or hook
`ULocalPlayer::CalcSceneView`). If your values are back to defaults, you are writing into the reset.
Fix: write in a post-hook on `APlayerCameraManager::UpdateCamera`, or pre-hook on `ULocalPlayer::CalcSceneView`,
or call `SetCameraCachePOV()` with your patched struct at that point.

---

## 4. When do DOF fields get ignored in the final blend?

**[verified]** `PostProcessBlendWeight == 0` → everything ignored:
```cpp
// SceneView.cpp:1445
void FSceneView::OverridePostProcessSettings(const FPostProcessSettings& Src, float Weight)
{
    if (Weight <= 0.0f) { return; }          // :1449
    if (Weight > 1.0f)  { Weight = 1.0f; }
#define LERP_PP(NAME) if(Src.bOverride_ ## NAME) Dest.NAME = FMath::Lerp(Dest.NAME, Src.NAME, Weight);
```

**[verified]** `DepthOfFieldFocalDistance == 0` is **not** auto-focus — it disables DOF:
```cpp
// Scene.cpp:579
DepthOfFieldFocalDistance = 0;   // Intentionally invalid to disable DOF by default.
DepthOfFieldFstop = 4.0f; DepthOfFieldMinFstop = 1.2f; DepthOfFieldSensorWidth = 24.576f; // APS-C
DepthOfFieldScale = 0.0f; DepthOfFieldFocalRegion = 0.0f;
DepthOfFieldNearTransitionRegion = 300.0f; DepthOfFieldFarTransitionRegion = 500.0f;
```

**[verified]** `FocalRegion` / `NearTransitionRegion` / `FarTransitionRegion` / `Scale` are **mobile-only**;
`DiaphragmDOF::FPhysicalCocModel::Compile` reads only:
`DepthOfFieldFocalDistance`, `DepthOfFieldFstop`, `DepthOfFieldSensorWidth`, `DepthOfFieldSqueezeFactor`,
`DepthOfFieldDepthBlurRadius`, `DepthOfFieldDepthBlurAmount` (+ cvars) — DiaphragmDOFUtils.cpp:100-160.
Their own header categories say it: `Category="Lens|Mobile Depth of Field"` (Scene.h:2046-2061).
So **a huge FocalRegion does not kill blur on desktop, and setting `DepthOfFieldScale` cannot disable it**
(its default is 0 anyway).

Formula used (DiaphragmDOFUtils.cpp:129-143, and shader `SceneDepthToCocRadius`, DOFCommon.ush):
```
f  = 0.5 * SensorWidth / tan(HFOV/2)                 // mm
CoC_diameter = f*f / (N * (d_mm - f))                // mm, N = fstop, d_mm = DepthOfFieldFocalDistance*10
CoC_radius_px = 0.5 * CoC_diameter / SensorWidth * HorizontalRes
CocRadius(shader) = ((SceneDepth - Focus) / SceneDepth) * CocInfinityRadius
```
Sanity numbers (HFOV 90°, sensor 71 mm, fstop 0.5 → f≈35.5 mm), radius at 1080p:
focus 3 m ≈ 11.5 px · 5 m ≈ 6.9 px · 15 m ≈ 2.3 px. At HFOV 60° (f≈61.5 mm), 5 m ≈ 20.7 px.
So focus 15 m + wide FOV may genuinely look like "no DOF".

**Test:** set focus 200 cm, fstop 1.0, and a narrow FOV (or `r.DepthOfFieldQuality 4`), then compare.

---

## 5. Does a runtime-spawned unbound `APostProcessVolume` participate?

**Verdict:** only if it was registered into `UWorld::PostProcessVolumes`; blending is done per frame from that
array, and camera POV settings are applied *after* volumes (so volumes can never beat the camera).

**[verified]** per-frame blend:
```cpp
// World.cpp:9260
void UWorld::AddPostProcessingSettings(FVector ViewLocation, FSceneView* SceneView)
{ OnBeginPostProcessSettings.Broadcast(...); for (IInterface_PostProcessVolume* PPVolume : PostProcessVolumes) DoPostProcessVolume(PPVolume, ViewLocation, SceneView); }
```
```cpp
// World.cpp:9192
static void DoPostProcessVolume(...)
{
    const FPostProcessVolumeProperties VolumeProperties = Volume->GetProperties();
    if (!VolumeProperties.bIsEnabled) return;
    float LocalWeight = FMath::Clamp(VolumeProperties.BlendWeight, 0.0f, 1.0f);
    if (!VolumeProperties.bIsUnbound) { ...EncompassesPoint... LocalWeight = 0 if outside / blend by radius... }
    if (LocalWeight > 0) SceneView->OverridePostProcessSettings(*VolumeProperties.Settings, LocalWeight);
}
```
```cpp
// Classes/Engine/PostProcessVolume.h:55
virtual FPostProcessVolumeProperties GetProperties() const override
{ Ret.bIsEnabled = IsPPVEnabled(); Ret.bIsUnbound = bUnbound != 0; Ret.BlendRadius = BlendRadius;
  Ret.BlendWeight = BlendWeight; Ret.Priority = Priority; Ret.Settings = &Settings; return Ret; }
```
Registration hooks declared in the same header:
```cpp
ENGINE_API virtual void PostUnregisterAllComponents(void) override;
protected: ENGINE_API virtual void PostRegisterAllComponents() override;
```
(Epic docs for that override: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/APostProcessVolume/PostRegisterAllComponents)
`UWorld::InsertPostProcessVolume` inserts sorted by `Priority` (World.cpp:9146). **[unverified]** the body of
`APostProcessVolume::PostRegisterAllComponents` — the .cpp I fetched does not contain it, so "it calls
InsertPostProcessVolume" is inference (very high confidence, but not a quote).

Also: `UWorld::AddPostProcessingSettings` is called from `FSceneView::StartFinalPostprocessSettings`
(SceneView.cpp:1801, via the view's World), i.e. **before** `CalcSceneView` applies the camera POV — so
priority 10000 buys you nothing against the camera's own PostProcessSettings, which are applied last with
weight 1.

**Test:** log `GetAllActorsOfClass(APostProcessVolume)` count and, for your spawned one, `bEnabled`,
`bUnbound`, `BlendWeight`, `ActorHasTag`, `IsActorInitialized`. If it is missing from the world's array, the
volume is inert no matter what `Settings` contains. Spawn with `SpawnActor` + `FinishSpawning` (registers
components), not `SpawnActorDeferred` without finishing.

---

## 6. Games that bypass engine DOF

**[unverified / partly speculative]** I could not confirm a named shipping title. What is verifiable:
* UE 5.8's `FPostProcessSettings` docs list a `DepthOfFieldEnabled` bool — *"When false, post-process depth of
  field will not be used"* (https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FPostProcessSettings).
  That field does **not** exist in 5.3 `Scene.h`, so it is a newer addition; if your target is 5.7/5.8, a game
  can switch engine DOF off at the settings level. **[unverified]** for 5.4–5.6.
* Games routinely ship `r.DepthOfFieldQuality=0` in `DefaultEngine.ini`/`DefaultDeviceProfiles.ini` and do
  their own DOF; forum guidance for disabling DOF is exactly that cvar (2017):
  https://forums.unrealengine.com/t/how-to-disable-reflections-and-dof/86882 ,
  https://forums.unrealengine.com/t/how-do-u-completely-disable-depth-of-field-for-performance/364591
* Photo modes normally drive a dedicated camera/volume and often force their own PP state; if the game's
  photo mode writes `PostProcessSettings` through a `UCameraComponent`, that write wins over anything you put
  in `CameraCache` for the *gameplay* camera — but not over a mod write later in the frame.

**Test:** set `r.DepthOfFieldQuality 4` and `ShowFlags.SetDepthOfField(true)` (via a debug console/`ShowFlags`
command if the game keeps one). If still nothing, dump `FinalPostProcessSettings.DepthOfFieldFstop` at render
time — if it is 0, something (game or your timing) zeroed it.

---

## 7. `DepthOfFieldMinFstop` and `DepthOfFieldBladeCount`

**Verdict:** neither clamps the CoC. `MinFstop` only selects the bokeh **shape**; a 0.5 fstop is not clamped.

**[verified]** DiaphragmDOFUtils.cpp:167-230:
```cpp
void DiaphragmDOF::FBokehModel::Compile(const FViewInfo& View)
{
    DiaphragmBladeCount = FMath::Clamp(View.FinalPostProcessSettings.DepthOfFieldBladeCount, 4, 16);
    float Fstop    = View.FinalPostProcessSettings.DepthOfFieldFstop;
    float MinFstop = View.FinalPostProcessSettings.DepthOfFieldMinFstop > 0 ? ... : 0;
    if (Fstop <= MinFstop) { BokehShape = EBokehShape::Circle; DiaphragmBladeCount = 0; }   // shape only
    else if (MinFstop == 0.0) { BokehShape = EBokehShape::StraightBlades; }
    else { BokehShape = EBokehShape::RoundedBlades; float BladeRadius = CircumscribedRadius * Fstop / MinFstop; }
```
`FPhysicalCocModel::Compile` does not read either field.
Caveat: the UPROPERTY metadata is `ClampMin=1.0, ClampMax=32.0` on `DepthOfFieldFstop` (Scene.h:1712) — that is
editor slider metadata only; a C++/memory write of 0.5 is used as-is by the renderer.

**Test:** irrelevant to "no blur" — do not chase these two.

---

## Minimal experiment list (in order)

1. `r.DepthOfFieldQuality` value at runtime (and who sets it: `sg.PostProcessQuality`, DeviceProfile, ini).
2. `ShowFlags.DepthOfField` / `ShowFlags.PostProcessing` at runtime.
3. Read back `CameraCache.POV.PostProcessSettings.DepthOfFieldFstop` **at end of frame** → detects the reset.
4. If 3 shows defaults: move the write to a post-hook on `APlayerCameraManager::UpdateCamera` (or pre-hook
   `ULocalPlayer::CalcSceneView`), or use `SetCameraCachePOV()`.
5. If 3 shows your values: the volume path is irrelevant (camera wins) → gate is 1/2 or a custom DOF pipeline.
6. Sanity-check the math at your actual FOV (focus 200 cm, fstop 1.0, narrow FOV) before concluding "broken".
