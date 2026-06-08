# SkyrimCombatLogger

An SKSE plugin for Skyrim Special Edition that logs combat events to a file in real time — hits, combat state changes, deaths, and magic effect applications. Useful for debugging follower AI, mod conflicts, and damage sources.

## What it logs

| Event | Format |
|---|---|
| Hit | `[HIT] Attacker(FormID) → Target(FormID) \| via WeaponOrSpell(FormID)` |
| Combat state | `[COMBAT] Actor(FormID) entered/exited combat with Target(FormID)` |
| Death | `[DEATH] Actor(FormID) killed by Killer(FormID)` |
| Magic effect | `[MGEF] Caster(FormID) → Target(FormID) \| EffectEditorID(FormID)` |

Followers are annotated with `[F]` in all log lines when `LogFactions=true`.

Log file: `Documents\My Games\Skyrim Special Edition\SKSE\SkyrimCombatLogger.log`

## Features

- **Hit logging** — weapon, spell, and MGEF hits with source identification
- **Combat state logging** — entered/exited combat and searching state changes
- **Death logging** — who killed whom
- **Magic effect logging** — hostile/detrimental MGEF applications only (filters out buffs)
- **Follower filter** — optionally log only events involving current followers
- **Follower friendly-fire block** — auto-resets combat state when followers target each other
- **Post-load follower reset** — clears stale combat state on game load
- **MGEF exclusion list** — suppress specific effect FormIDs from the log

## Requirements

- Skyrim Special Edition (AE/SE, 1.5.97 or 1.6.x)
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Installation

1. Build from source (see below) or grab a release DLL
2. Copy `SkyrimCombatLogger.dll` → `Data/SKSE/Plugins/`
3. Copy `SkyrimCombatLogger.ini` → `Data/SKSE/Plugins/`

## Configuration

Edit `SkyrimCombatLogger.ini`:

```ini
[Log]
LogHits=true
LogCombat=true
LogDeaths=true
LogFactions=true       ; annotate followers with [F]
LogMGEFName=true       ; include MGEF editor IDs
OnlyFollowers=false    ; set true to only log follower-involved events

[Combat]
BlockFollowerHits=true          ; prevent follower-vs-follower combat escalation
ResetFollowerCombatOnLoad=true  ; clear stale combat state on load

[Filter]
ExcludeMGEF=            ; comma-separated hex FormIDs to suppress
```

## Building

**Requirements:** Visual Studio 2022, CMake 3.21+, vcpkg

```powershell
git clone https://github.com/lolidroid86/SkyrimCombatLogger.git
cd SkyrimCombatLogger
cmake --preset vs2022-windows
cmake --build build --config Release
```

The DLL is output to `build/Release/SkyrimCombatLogger.dll`.

Dependencies (pulled automatically via vcpkg + FetchContent):
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)
- spdlog, simpleini, fmt, xbyak, robin-hood-hashing

## Contributors

| Contributor | Role |
|---|---|
| [lolidroid86](https://github.com/lolidroid86) | Project lead — design, testing, Skyrim modding context |
| [Claude Sonnet 4.6](https://claude.ai) (Anthropic) | Implementation |
