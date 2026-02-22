# Sims 3 Settings Setter v1.4.0

Performance patcher and setting editor for The Sims 3.

**[Download Latest Release](https://github.com/sims3fiend/Sims3SettingsSetter/releases)**

Contact me on [Tumblr](https://sims3fiend.tumblr.com/) | [Discord](https://discord.com/users/239640351999000578)

## Features

### Live Settings Editor
Edit "Variable" settings in real-time without restarting the game. Change things like:
- Bloom intensity and light colors
- Weather and time of day (snow in summer!)
- Shadow distances and quality
- Sunlight brightness
- And much more - organized by category with search
**Note:** Some settings may have unexpected effects. None are likely/able to corrupt your game, and all are temporary (since I removed the Options heading ones). If you find anything that seems wrong/doesn't work how you think it does, send me a message!

Some package mods sometimes tweak some of these, so this can also be a way of doing things like lighting mods more easily.

### Config Editor
View and edit Config values from `GraphicsRules.sgr`. This shows what's actually loaded in memory (not just what's in the file which can sometimes be wrong/changed after init) and includes hidden settings that don't appear in the original files. Much clearer and easier than editing the .sgr file manually IMO.

### Patches
The main point of the mod now. Collection of ASM patches for performance improvements. **All compatible with Steam, EA App/Origin and Retail versions! Woo!**
There's also a lot of helper functions and easy to use things if you'd like to make your own, see **[patches/README.md](patches/README.md)** for technical details on how to write your own.

**Performance Patches**
- **RefPack Decompressor Optimization ** - Completely rewrote the game's refpack .package file decompressor with AVX2/SSE2 SIMD intrinsics. This is probably the most impactful patch, faster loading screens, less stuttering when streaming assets, optimises one of the heaviest functions in the game.
- **Mimalloc Allocator** - Replaces the Sims 3's old crusty memory allocator with [mimalloc](https://github.com/microsoft/mimalloc) for better memory management and performance. Requires a restart to apply.
- **Optimized Lot Streaming Settings** - Enables lot throttling and tweaks camera speed threshold settings so lots load more smoothly when you stop moving. Major improvement.
- **Timer Optimization** - Increases (reduces?) timer resolution to 1ms, ~~optimizes critical sections with tiered spin counts~~ No longer does this as if 1.2.3.
- **Smooth Patch (Precise Flavour)** - ["Just Harry"](https://github.com/just-harry)'s fully rewritten tick-rate limiter using a hybrid sleep/busy-wait approach. Sleeps via `NtDelayExecution` for sub-millisecond precision and finishes with a busy-wait spin loop for exact timing. Default tick rate raised to 480 TPS, with presets at multiples of 60 (480/960) for smoother frame-pacing. **Now includes a frame-rate limiter** (default 60 FPS) with a separate inactive window limit. Can be safely toggled on/off at any time. **It is recommended you still use the original [Smooth Patch's](https://modthesims.info/d/658759/smooth-patch-2-1.html) .package file**
- **Smooth Patch (Original Flavour)** - LazyDuchess's original Smooth Patch implementation in S3SS.
- **Lot Visibility Camera Override** - Stops lot loading based on camera view, should make it so they only load around you. Might not do anything.
- **CPU Thread Optimization** - Optimizes thread placement for modern CPUs with P/E-cores or multiple CCXs. This also doubles as an Alder Lake patch for people using that series of CPU.
- **CreateFileW Random Access** - Improves file I/O performance by hinting random access pattern.
- **Oversized Thread Stack Fix** - Reduces memory wasted by the game's file-watcher threads. The game creates several dozen of these with oversized 1 MB stacks when they need <64 KB. Saves ~80-170 MB of virtual address space depending on your setup. Requires a restart to apply. By ["Just Harry"](https://github.com/just-harry).
- **GC_stop_world() Optimization** - Early exit for a GC function called ~once per frame, very minor improvement, driveby patch.

**Graphics Patches**
- **Uncompressed Sim Textures** - Forces textures for Sims to be uncompressed during gameplay, like they are in CAS. It is not recommended to use this patch unless you are also using DXVK, as otherwise the game may run out of memory or experience Error 12. This improves the graphical fidelity of Sims by avoiding lossy compression and by preventing compression artefacts.

**Diagnostic Patches**
- **Expanded Crash Logs** - Enhances the game's crash logs (`xcpt...txt`) with much more diagnostic information. Adds detailed access violation info (memory state, protection flags, read/write/DEP), S3SS version, and a full virtual memory statistics breakdown. No performance impact during normal gameplay. Enable this when reporting crashes! By ["Just Harry"](https://github.com/just-harry).

**Experimental Patches**
- **Intersection Optimization** - SIMD optimization for navmesh/pathing calculations, a lot faster but rarely called in normal gameplay, is called very heavily when loading CC/uncached worlds though. May cause issues with sims entering rabitholes, still need to investigate
- **GC Finalizer Throttle** - Prevents (or tries to) the garbage collector finalizer loop from blocking the simulation thread, reducing large stutters. Increases the frame threshold before triggering the blocking loop and caps it to one batch of finalizers per frame instead of an infinite loop.
  - May slightly increase memory usage on very long play sessions
- **Adaptive Thread Waiting** - Hybrid spin-wait that adapts between 50-500μs based on success rate. Trades a bit of CPU usage for lower frame time variance.
  - This should be safe but may need adjusting to make more performant, also not really sure it's worth the hassle but it's done now so might as well include it, may re-add stat tracking also
- **WorldCache Size Uncap** - Removes the 512MB limit on WorldCache files. May help with large CC worlds as it prevents cache-churning.
  - Might have issues once the cache exceeds 2GB. If you run into a crash and think it's this, let me know
- **Resolution Spoofer** - Injects fake resolutions (1440p, 4K, 5K, 6K) for downsampling. Makes the game look real good! You'll want a [UI scale](https://github.com/just-harry/tiny-ui-fix-for-ts3) mod as well.
  - Still working on this, may replace with a more targeted patch that shouldn't require a UI mod (using the pseudoresolution setting). This can also crash your game when set too high for your setup. It **may also crash when using other Borderless Fullscreen implementations**. I do some special handling in the mod for this.
- **Chunky Patch - Disable GC_try_to_collect()** - Removes the explicit garbage collection call from the simulation loop. GC profiling shows this function dominates simulation thread time. Should improve Simulate calls/s quite dramatically. Relies on `GC_malloc` to trigger collection naturally when memory pressure requires it.
  - May increase memory usage
- **Map View Lot Streaming Blocker** - Prevents lot streaming while in map view, makes going in and out of map a lot less stuttery.
  - Experimental as it has a known issue where the toggle gets stuck on the "don't load" path. If your lots aren't loading in, try disabling this first

### Quality of Life / Settings
- **Memory Monitor**: Get warned when approaching the 4GB limit (Error 12) so you can save before you crash and lose it all. Now uses `NtQueryInformationProcess` for more accurate virtual address space tracking. Choose between an auto-dismiss overlay or a modal dialog that pauses gameplay. Includes detailed live memory statistics (page counts, protection flags, free span histogram) in a collapsible section.
- **Borderless Window**: Run the game in Borderless Fullscreen. (also known as Windowed Fullscreen, Borderless Windowed etc.) This can also fix some issues with screen recording software, game brightness etc, compared to regular Fullscreen.
- **Custom UI keybind**: Change the toggle key (default: Insert)
- **Change UI Font Size**: Make the ImGui font bigger/smaller

## Installation

1. Install an ASI loader to your `The Sims 3\Game\Bin` directory:
   - [dxwrapper](https://github.com/elishacloud/dxwrapper) or
   - [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) (`wininet.dll` for Win32, included in [Smooth Patch](https://modthesims.info/d/658759/smooth-patch-2-1.html))

2. Drop `Sims3SettingsSetter.asi` into `The Sims 3\Game\Bin`

3. Launch the game

## Usage

**Press Insert** to open the UI (change this in Other/QoL tab)
**File → Save Settings** to make changes persistent. Some settings (QoL, patches) auto-save when changed. Some patches require a restart to apply. (CPU Thread, CreateFileW)
Green settings - Modified from the default and saved.
Yellow text - Modified but not saved for future restarts.

Settings are stored in `Documents\Electronic Arts\The Sims 3\S3SS\S3SS.toml` (or the localized equivalent, e.g. `Die Sims 3`, die for German). Non-ASCII/Unicode usernames are supported. If you're upgrading from an older version that used an INI file, your settings *should* be automatically migrated on first launch.

### Settings Tab
- Only becomes available after loading into a world
- Right-click any setting to:
  - Edit beyond min/max bounds
  - Reset to default
  - Clear override (remove from config)
  - Copy address for if you're REing

### Patches Tab
- Toggle patches on/off
- Hover for descriptions and technical details
- Experimental patches marked with [EXPERIMENTAL] may be unsafe, more of a pre-release thing

### Config Values Tab
- Edit any Config heading value
- Right-click to clear override

## Troubleshooting

**UI doesn't open?**
- Check for `S3SS_LOG.txt` in the game's execution directory (`Documents\Electronic Arts\The Sims 3\S3SS\`) - send it to me if it exists
- You can also try changing the keybind in `Documents\Electronic Arts\The Sims 3\S3SS\S3SS.toml` by modifying the `UIToggleKey` value (See for IDs: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)
- If there is no log file, your ASI loader isn't working.

**Settings tab stuck on 'Initializing'?**
- Wait until you're fully in-game (past loading screen)
- If it doesn't auto-initialize, use the manual initialize button.
- Send me your log.

**I get crashes when I use specific patches?**
- Send me your `S3SS_LOG.txt` and the latest `xcpt...txt` crash log from `Documents\Electronic Arts\The Sims 3`.

**My settings keep resetting every time I restart the game?**
- Settings are now saved to `Documents\Electronic Arts\The Sims 3\S3SS\S3SS.toml` which should hopefuly resolve this. Check that this file exists and that S3SS has write permissions to your Documents folder. If it's not appearing, you may need to run the sims 3 executable as admin.

## Building from Source

You'll need [vcpkg](https://github.com/microsoft/vcpkg), and to run `vcpkg install --triplet=x86-windows-static`

## For Developers

This tool features a modular patch system that makes adding custom patches very easy. All patches auto-register and appear in the GUI, the system handles memory protection, change tracking, and restoration automatically, which makes reverse engineering and patching much simpler.
See **[patches/README.md](patches/README.md)** for the full guide and breakdown.

## Known Issues & Coming Soon
**Working on:**
- Folder-ization of the codebase... desparately needed but I've muscle memories where everything is...
- UI improvements/features
- D3D9 fun things, I have some stuff using it but it's very janky so maybe next revision
- More config values/settings outside of the debugUI ones, things like AA, etc.. There's no convenient hook point for those however so I'll have to manually do it... which kind of stinks.
- More patches, more. more. more. Main one is object throttling. A way to slow down the object loading would be very very very good.
- Mono....
- More.
