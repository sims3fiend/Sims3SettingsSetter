# Sims3MonoModder (S3MM) BETA

> **Warning!!! Beta!!!**
> Everything here is still a work in progress. The managed API surface in [`S3MM.Helper/S3MM.cs`](S3MM.Helper/S3MM.cs) should be stable enough to build with (hopefully only additions), but parts of it may be broken in ways I haven't discovered yet. This release is intended at developers, not at users.

Contact me on [Tumblr](https://sims3fiend.tumblr.com/) | [Discord](https://discord.com/users/239640351999000578)

## Features

### Modding
- **Drop-in C# mods.** Place a `.cs` source file under `Documents\Electronic Arts\The Sims 3\S3MM\Source\ModName` or a pre-compiled .dll under `/Mods`. S3MM finds `csc.exe` and compiles the code on the fly against the game's assemblies + `S3MM.Helper`. Compiled DLLs go straight into `S3MM/Mods/`; source files go into `S3MM/Source/<ModName>/`.
- **Hot-reload.** Edit your `.cs`, hit reload in the Mods panel, S3MM recompiles, swaps the assembly, runs `[OnUnload]` -> `[OnLoad]`, and reinstalls dispatch stubs. No game restart required.
- **Non-destructive hooking.** Chain-based hooks installed via dispatch stubs. Multiple mods can hook the same method with priority ordering; `Hook.CallNext()` walks the chain, `Hook.CallOriginal()` skips it. `[Watcher]`s run ahead of all hooks for read-only observation. Nested and recursive chains are handled natively.
- **Fast reflection cache.** Native detours over Mono's class/method/field lookup enable repeated lookups to be O(1) hits game-wide. Mods can access this directly through `S3MM.Cache`, typed field/property accessors, method invocation, and introspection without the boxing overhead of `System.Reflection`.
- **Exception & crash capture.** Detours over Mono's exception raise/handler paths and `g_error` log managed exceptions, attributed to the throwing mod in the S3MM log. A vectored handler also catches native interpreter faults, dumps the managed frame chain, and can recover from a known recurring crash to keep the game alive.
- **Assembly conflict detection.** When a mod replaces a core game assembly (`ScriptCore`, `SimIFace`, `mscorlib`, etc.), S3MM records the source and version, flags same-version/duplicate replacements, and reports which copy was kept vs. discarded, so two mods fighting over the same assembly is visible in the Mods panel instead of silent.
- **Hook audit API.** Mods can introspect the live hook graph at runtime through `S3MM.Audit`: `IsMethodHooked()`, `GetHookChain()` (per-method chain with mod names + priorities), `GetReplacedAssemblies()`, and `GetLoadedMods()`.

### In-game overlay
- **Mods Panel** loaded mods list, per-mod logs, hot-reload buttons, settings, init status.
- **Inspector** assembly -> namespace -> class tree with Method / Field / Property / Singleton detail panes. Object Explorer windows for walking live object graphs (WIP).
- **Assembly Diff** compare a loaded assembly against its vanilla baseline to see exactly which methods a mod added, modified, or removed (useful for core mods)
- **Profiler** per-method call counts, self-time, total-time, plus allocation, boxing, and ICALL tables. CSV + Chrome-trace export.
- **XRefs** callers/callees of any method, either from the inspector panel or profiler
- **Resource Browser** browse loaded packages and extract their contents in-game (WIP, will be expanded)
- **Settings / Themes** cool colors.

<img width="1350" height="902" alt="image" src="https://github.com/user-attachments/assets/64620205-de85-4db9-897c-ab890272540a" />

## Installation

1. Install an ASI loader into `The Sims 3\Game\Bin`:
   - [dxwrapper](https://github.com/elishacloud/dxwrapper), or
   - [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
     <sup>(`wininet.dll` for Win32, included with some ASI mods like [Smooth Patch](https://modthesims.info/d/658759/smooth-patch-2-1.html))</sup>

2. Drop `Sims3MonoModder.asi` into `The Sims 3\Game\Bin`.

3. Launch the game. On first run S3MM should automatically extract `S3MM.Helper.dll` and dump the game's managed assemblies from their respective .package files into `Documents\Electronic Arts\The Sims 3\S3MM\`. These dumped vanilla assemblies double as the compile-time references for `.cs` mods and as the baseline the Assembly Diff compares against.

4. Place mods in `Documents\Electronic Arts\The Sims 3\S3MM\`: compiled `.dll`s go in `S3MM\Mods\`, bare `.cs` sources go in `S3MM\Source\<ModName>\`. They get picked up on starting the game and via the Mods panel's **Scan for New Mods** button.

## Example Mod

See [`examples/`](examples/README.md). This isn't the best of demonstrations, and my eventual goal is to create a wiki with more solid/readable examples, as well as actual mods using S3MM to serve as a better resource. In the meantime [`S3MM.Helper/S3MM.cs`](S3MM.Helper/S3MM.cs) can act as a reference for the helper functions S3MM adds.

## Building from source

Requires **CMake 3.21+**, **Visual Studio 2022**, and **vcpkg**

```bash
cmake --preset x86-release
cmake --build --preset x86-release
```

Alternatively open `Sims3MonoModder.vcxproj` directly in Visual Studio and build `Release|Win32` (run `vcpkg install --triplet x86-windows-static` in the repo root first so `vcpkg_installed\` exists).

## Roadmap
- In-mod ImGui API, let mods register their own overlay panels, notifications, etc. Currently being worked on but a bigger task than I thought...
- Resource Browser expansion, resource loading to replace `.package` as a requirement entirely, as well as XML tweaking in-script/dynamically through MM
- Proper getting-started guide + working examples beyond the demo scripts (and actual mods)
- Add more helper functions, including native-side access stuff
- Rewrite the readme to be less bad

## License

[GPLv3](LICENSE). Statically links [Microsoft Detours](https://github.com/microsoft/Detours), [Dear ImGui](https://github.com/ocornut/imgui), and [toml++](https://github.com/marzer/tomlplusplus)

## Credits
- [S3PI](https://s3pi.sourceforge.net/): the S3SA decryption in the package reader is ported from it
- LazyDuchess for [MonoPatcher](https://github.com/LazyDuchess/MonoPatcher) and for many many many other projects and ideas that I've ~~stolen~~ adopted over the year(s)...
- Everyone on LazyDuchess' discord for support, motivation, ideas, etc. etc. etc.
- [Just-harry](https://github.com/just-harry) in particular for help, ideas, input, etc.
- Probably many other people 
