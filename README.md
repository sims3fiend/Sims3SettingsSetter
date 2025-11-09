# Sims 3 Settings Setter v1.0

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
Collection of ASM patches for performance improvements. **Most are Steam (ts3w.exe) only** - EA users can still use all other features, but incompatible patches are greyed out.... One day I'll port some over I promise...

⭐'s for ones that I think are particuarly worthwhile

**Available Patches:**
- **Smooth Patch Dupe** - Like smooth patch but more focused, sets the frame limit (seconds per frame) more directly, same result but clearer intent and doesn't touch unrelated functions (All versions)
- **Smooth Patch Original Flavour** - Like smooth patch but exactly the same and in S3SS instead (All versions)
- **CPU Thread Optimization** - Optimizes thread placement for modern CPUs with P/E-cores or multiple CCXs (All versions)
- **Intersection Optimization** - SIMD optimization for navmesh calculations (Steam)
- **Timer & Thread Optimizations ⭐** - Increases timer resolution, optimizes critical sections, patches spin loops. Should be a significant improvement on CPU usage (Steam)
- **CreateFileW Random Access** - Improves file I/O performance by hinting random access patterns (Steam)
- **Map View Lot Streaming Blocker ⭐** - Prevents lot streaming while in map view, makes going in and out of map a lot less stuttery (Steam)
- **Lot Visibility Camera Override**  - Stops lot loading based on camera view, should make it so they only load around you. Might not do anything.
- **Optimized Lot Streaming Settings ⭐⭐** - Enables lot throttling and sets camera speed threshold settings lower so lots load in when you stop moving more smoothly, major improvement (All versions). This just sets the debug/Live settings, nothing else.
- **New! GC_stop_world() Optimization** - Adds an early exit for a garbage collection function that is called once a frame-ish, minor drive-by tweak, should have very little performance boost I just saw an easy win.

There's also a lot of helper functions and easy to use things if you'd like to make your own, see **[patches/README.md](patches/README.md)** for technical details on how to write your own.

### Quality of Life
- **Memory Monitor**: Get warned when approaching the 4GB limit (Error 12) so you can save before you crash and lose it all
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
- Send me your log

## Building from Source

You'll need [vcpkg](https://github.com/microsoft/vcpkg), and to run `vcpkg install --triplet=x86-windows-static`

## For Developers

This tool features a modular patch system that makes adding custom patches v easy. All patches auto-register and appear in the GUI, the system handles memory protection, change tracking, and restoration automatically, which makes reverse engineering and patching muuuch simpler.
See **[patches/README.md](patches/README.md)** for the full guide and breakdown


## Known Issues & Coming Soon
**Working on:**
- Folder-ization of the codebase... desparately needed but I've muscle memories where everything is...
- UI improvements
- More values outside of the debugUI ones, things like AA, etc.. There's no convenient hook point for those however so I'll have to manually do it... which kind of stinks.
- More patches, more. more. more. Main one is object throttling. Lots we can throttle easily (might do a custom throttling thing also), objects are kind of horrific, but a way to slow down loading of them or track/manage them would basically fix the game, no hyperbole
- More.