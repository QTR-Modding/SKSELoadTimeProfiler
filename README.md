# LoadTimeProfiler

SKSE/SKSEVR plugin that profiles Skyrim's startup load time — measuring how long each ESP/ESM and SKSE DLL takes to load, with an in-game overlay and export to CSV, TXT, and Perfetto JSON.

* [Nexus (SE/AE/VR)](https://www.nexusmods.com/skyrimspecialedition/mods/173928)

## Features

* Per-plugin load time broken into three phases: **file open**, **form load (ConstructObjectList)**, and **file close**
* Per-DLL timing across all SKSE messaging callbacks (PostLoad, DataLoaded, etc.)
* In-game overlay via [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
* Export to **CSV**, **plain text**, and **Chrome Trace / Perfetto JSON** (viewable at [ui.perfetto.dev](https://ui.perfetto.dev))
* VR-aware: integrates with [SkyrimVRESL](https://www.nexusmods.com/skyrimspecialedition/mods/106712) to import accurate VR plugin timings when present
* Configurable warn/critical thresholds with colour coding
* Search, filter by type, and sort in the overlay table

## User Requirements

* [SKSE](https://skse.silverlock.org/) or [SKSEVR](https://skse.silverlock.org/)
* [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444) — needed for SE/AE
* [VR Address Library for SKSEVR](https://www.nexusmods.com/skyrimspecialedition/mods/58101) — needed for VR
* [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) — for the in-game overlay
* [SkyrimVRESL](https://www.nexusmods.com/skyrimspecialedition/mods/106712) *(VR, optional)* — enables accurate per-plugin timing on VR

## Developer Requirements

* [CMake](https://cmake.org/) — add to `PATH`
* [Visual Studio Community 2022](https://visualstudio.microsoft.com/) with **Desktop development with C++**
* [Vcpkg](https://github.com/microsoft/vcpkg) — set `VCPKG_ROOT` environment variable to your vcpkg folder
* [CommonLibVR](https://github.com/alandtse/CommonLibVR) (`ng` branch) — bundled as a submodule; see Building

## Building

```
git clone https://github.com/Quantumyilmaz/SKSELoadTimeProfiler.git
cd SKSELoadTimeProfiler
git submodule update --init --recursive extern/CommonLibSSE
```

Open the folder in Visual Studio 2022 and select the **Release** (or Debug) CMake preset, then build.

For command line builds, activate the MSVC environment first:

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --preset release
cmake --build build/release --parallel
```

> If using a user preset (e.g. `user-release`), substitute that preset name and its corresponding build directory.

> To override the compiler path or set environment variables per-machine, copy `CMakeUserPresets.template.json` to `CMakeUserPresets.json`, fill in your paths, and select a **User** preset. This file is gitignored.

### Environment Variables

| Variable | Required | Description |
|---|---|---|
| `VCPKG_ROOT` | **Yes** | Path to your vcpkg clone |
| `COMMONLIB_SSE_FOLDER` | No | Fallback path to CommonLibVR if not using the submodule |
| `SKYRIM_DIRECT_FOLDER` | No | SE/AE `Data/` folder — copies the DLL on build |
| `SKYRIM_VR_DIRECT_FOLDER` | No | VR `Data/` folder — copies the DLL on build |

Copy `CMakeUserPresets.template.json` → `CMakeUserPresets.json` and fill in your paths. This file is gitignored.

### vcpkg Dependencies

Resolved automatically via `vcpkg.json`:
`rapidjson`, `spdlog`, `xbyak`, `directxtk`, `rsm-binary-io`, `rapidcsv`, `fast-cpp-csv-parser`, `skse-mcp`, `skyrimvresl`

## License

[GPL-3.0-or-later](LICENSE) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS.md).

Specifically, the Modded Code is Skyrim (and its variants) and Modding Libraries include [SKSE](https://skse.silverlock.org/), CommonLib (and variants), and Windows.

See `NOTICE` for the full project licensing notice.

## Credits

* [Quantumyilmaz](https://github.com/Quantumyilmaz) — author
* [alandtse](https://github.com/alandtse) — [CommonLibVR](https://github.com/alandtse/CommonLibVR) and [SkyrimVRESL](https://github.com/alandtse/SkyrimVRESL)
* [Thiago099](https://github.com/Thiago099) — [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
* [powerof3](https://github.com/powerof3) — [CLibUtil](https://github.com/powerof3/CLibUtil)
* [Ryan-rsm-McKenzie](https://github.com/Ryan-rsm-McKenzie) — CommonLibSSE
