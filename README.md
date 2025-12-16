# Sims 3 Settings Setter v1.1

Performance improvements and settings editor for The Sims 3

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
- **Note:** Some settings may have unexpected effects. None are likely/able to corrupt your game, and all are temporary (since I removed the Options heading ones). If you find anything that seems wrong/doesn't work how you think it does, send me a message!

Some package mods sometimes tweak some of these, so this can also be a way of doing things like lighting mods more easily.

### Config Editor
View and edit Config values from GraphicsRules.sgr. This shows what's actually loaded in memory (not just what's in the file which can sometimes be wrong/changed after init) and includes hidden settings that don't appear in the original files. Much clearer and easier than editing the .sgr file manually IMO.

### Performance Patches
The main point of the mod now
Collection of ASM patches for performance improvements. **Most are Steam (ts3w.exe) only** - EA users can still use all other features, but incompatible patches are greyed out.... One day I'll port some over I promise...

⭐'s for ones that I think are particularly worthwhile

**Available Patches:**
- **RefPack Decompressor Optimization ⭐⭐⭐** - Completely rewrote the games refpack .package file decompressor with AVX2/SSE2 SIMD intrinsics. This is probably the most impactful patch, faster loading screens, less stuttering when streaming assets, optimises one of the heaviest functions in the game. (Steam only, sorry)
- **Mimalloc Allocator ⭐** - Replaces the sims 3 old crusty memory allocator with [mimalloc](https://github.com/microsoft/mimalloc) for better memory management and performance. Requires restart to apply. (All versions)
- **Optimized Lot Streaming Settings ⭐** - Enables lot throttling and tweaks camera speed threshold settings so lots load more smoothly when you stop moving. Major improvement. (All versions)
- **Timer & Thread Optimizations ⭐** - Increases (reduces?) timer resolution to 1ms, optimizes critical sections with tiered spin counts. (Steam, EA version sometime maybe)
- **Lot Visibility Camera Override** - Stops lot loading based on camera view, should make it so they only load around you. Might not do anything. (Steam)
- **Smooth Patch Original Flavour** - Exact smooth patch implementation in S3SS, I may change this in the future as I find it very confusing (All versions)
- **Smooth Patch Dupe** - Like smooth patch but just one function, sets a frame limiterm I would suggest just using Smooth Patch Originall™, you can use both but Original Flavour will overwrite this one (All versions)
- **CPU Thread Optimization** - Optimizes thread placement for modern CPUs with P/E-cores or multiple CCXs (All versions)
- **Intersection Optimization** - SIMD optimization for navmesh/pathing calculations, a lot faster but rarely called in normall gameplay, is called very heavily when loading CC/uncached worlds though (Steam)
- **CreateFileW Random Access** - Improves file I/O performance by hinting random access pattern (Steam)
- **GC_stop_world() Optimization** - Early exit for a GC function called ~once per frame, very minor improvement, driveby patch (Steam)

**Experimental Patches:**
- **Adaptive Thread Waiting** - Hybrid spin-wait that adapts between 50-500μs based on success rate. Trades a bit of CPU usage for lower frame time variance. (All versions)
  - This shoud be safe but may need adjusting to make more performant, also not really sure it's worth the hassle but It's done now so might as well include it, may re-add stat tracking also
- **WorldCache Size Uncap** - Removes the 512MB limit on WorldCache files. May help with large CC worlds as it prevents cache churning. (All versions)
  - Might have issues once the cache exceeds 2gb, if you run into a crash and think it's this, let me know
- **Resolution Spoofer** - Injects fake resolutions (1440p, 4K, 5K, 6K) for downsampling. Makes the game look real good! You'll want a [UI scale](github.com/just-harry/tiny-ui-fix-for-ts3) mod as well. (All versions)
  - Still working on this, may replace with a more targetted patch that shouldn't require a UI mod (using the pseudoresolution setting). This can also crash your game when set too high for your setup. It **may also crash when using other borderless fullscreen implementations**, I do some special handling in the mod for this.
- **Map View Lot Streaming Blocker** - Prevents lot streaming while in map view, makes going in and out of map a lot less stuttery (Steam)
  - Experimental as it has a known issue where the toggle gets stuck on the "don't load" path. If your lots aren't loading in, try disabling this first

There's also a lot of helper functions and easy to use things if you'd like to make your own, see **[patches/README.md](patches/README.md)** for technical details on how to write your own.

### Quality of Life Features
- **Memory Monitor**: Get warned when approaching the 4GB limit (Error 12) so you can save before you crash and lose it all
- **Borderless Window Mode (new)**: Run the game in borderless windowed or borderless fullscreen
- **Custom UI keybind**: Change the toggle key (default: Insert)
- **Preset system**: Save/load/share your configurations.

## Installation

1. Install an ASI loader:
   - [dxwrapper](https://github.com/elishacloud/dxwrapper) or
   - [Smooth Patch](https://modthesims.info/d/658759/smooth-patch-2-1.html) (includes one, currently busted for EA I think?)

2. Drop `Sims3SettingsSetter.asi` into `The Sims 3\Game\Bin`

3. Launch the game

## Usage

**Press Insert** to open the UI (change this in Other/QoL tab)
**File → Save** to persist changes. Some patches require a restart (CPU Thread, CreateFileW)
Green text = modified and saved value, yellow text is modified but not saved.

### Settings Tab
- Only becomes available after loading into a game
- Right-click any setting to:
  - Edit beyond min/max bounds
  - Reset to default
  - Clear override (remove from ini)
  - Copy address for if you're REing

### Patches Tab
- Toggle patches on/off
- Incompatible patches are greyed out (EA sorry)
- Hover for descriptions and technical details
- Experimental patches marked with [EXPERIMENTAL] may be unsafe, more of a pre-release thing

### Config Values Tab
- Edit any Config heading value
- Right-click to clear override

### Presets
File → Presets to save or load configurations
- **Merge**: Apply preset on top of current settings
- **Overwrite**: Clear everything, apply only preset values

## Troubleshooting

**UI doesn't open?**
- Check for `S3SS_LOG.txt` in your game directory - send it to me if it exists
- You can also try changing the keybind in the ini by modifying `UIToggleKey=45` (See for IDs: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)
- If no log file, your ASI loader isn't working

**Settings tab stuck on "initializing"?**
- Wait until you're fully in-game (past loading screen)
- If it doesn't auto-initialize, use the manual initialize button.
- Send me your log.

**Crashes while using specific patches**
- Send me your S3SS_LOG and the latest crash log from Documents\Electronic Arts\The Sims 3\

## Building from Source

You'll need [vcpkg](https://github.com/microsoft/vcpkg), and to run `vcpkg install --triplet=x86-windows-static`

## For Developers

This tool features a modular patch system that makes adding custom patches v easy. All patches auto-register and appear in the GUI, the system handles memory protection, change tracking, and restoration automatically, which makes reverse engineering and patching muuuch simpler.
See **[patches/README.md](patches/README.md)** for the full guide and breakdown


## Known Issues & Coming Soon
**Working on:**
- Folder-ization of the codebase... desparately needed but I've muscle memories where everything is...
- UI improvements/features
- D3D9 fun things, I have some stuff using it but it's very janky so maybe next revision
- More config values/settings outside of the debugUI ones, things like AA, etc.. There's no convenient hook point for those however so I'll have to manually do it... which kind of stinks.
- More patches, more. more. more. Main one is object throttling. A way to slow down the object loading would be very very very good.
- Mono....
- More.