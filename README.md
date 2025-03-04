# Sims 3 Settings Setter

Code for Sims 3 Settings Setter, feel free to do whatever you want with it.

I'm not going to be using the repo outside of major releases, please forgive me.

To build the project you'll need to [vcpkg](https://github.com/microsoft/vcpkg) install detours x86, imgui should work fine(?)

Feel free to contact me: [Tumblr](https://sims3fiend.tumblr.com/) | [Discord DM](https://discord.gg/akgJACJZ)

The below is just the tumblr post I made for the new version without the pictures:

## S3SettingsSetter New new version New

I have returned from the grave to do another re-rewrite of Sims 3 Settings Setter! Hopefully the last!
The main new features are a pattern detection approach, meaning it should work on any version of the game, and a new platform for some function rewrite optimizations I've done (steam only). Also most importantly includes the ability to set the Streaming settings to throttle lots, reducing stutter dramatically and a patch to make lots load on radius instead of view.

A full write-up below, but here's the main features:

### Change "variable" settings ingame:
This lets you change things that normally require mods but live ingame, letting you tweak things how you want. This includes things like changing bloom levels, light colors, sunlight brightness, weather (so you can have snow in summer), sunrise times, tweaking shadow distances, etc etc. Highly recommend just playing around to see what can be done. These are now correctly mapped so they should all work correctly.

### Change "Config" settings (not ingame):
This lets you set any config (graphicsrules.sgr) thing you want, including some that aren't in the original files (idr the name). It also lets you have set presets, and means you can (hopefully) more clearly see what settings you've changed. I will slowly (I've said this before lol) manually add Config and Options settings to the live edit, I haven't found an easy automatic way of doing this unfortunately.

### Performance tweaks and notifications:
Set notifications for hitting memory thresholds, letting you know before an E12 happens. Improve game performance with tweaks to game code. Uhh, maybe other stuff soon idk

Please note, as this is a new rewrite, this is still a beta. There will probably be bugs, the menu may glitch out, etc. Please also note that some settings may effect things in unexpected ways, if you're not sure what a setting does, maybe try it out in a fresh save first. Generally though, everything is correctly mapped and shouldn't have any lasting negative effects on your game.

[BaselinePerformance preset](https://simfileshare.net/download/5333998/) (put into Presets folder and load ingame with File -> Presets). This should reduce stuttering while still being light enough that it wont tank laptop players. Use this instead of my GraphicsRules file please!


## How to use

### Installing:

Just like before you'll need an ASI loader. I recommend [dxwrapper](https://github.com/elishacloud/dxwrapper) but if you're using [Smooth Patch](https://modthesims.info/d/658759/smooth-patch-2-1.html) that comes with one too. You then just plonk it into your The Sims 3\Game\Bin folder like any other .asi mod and run the game.

### Using:

Preset insert to open the UI

The menu should open up, if it doesn't, look in your game directory for hook_log.txt and send it to me x. If you don't have one, chances are your asi loader isn't working correctly.

The settings tab only becomes editable once a game has been loaded (might change this later). These are values that can be edited live ingame and should change something even if that thing isn't immediately obvious. 

Most have sliders, the min/max/step are determined by the game itself, but you can set them above/below these values by right clicking.

This also lets you "Clear Override" AKA remove it from the ini, or "Reset to Default" which resets it it to default :)

When you've got things how you want, go to file -> save to save them. If you want to make a preset out of them, that's in file too x, presets save things like patches, Config and QoL too, so just be careful. When loading a preset you can chose to Overwrite which basically clears your ini, or merge, which adds that preset on top of your current settings.

Everything else should be pretty easy to figure out idk

## What's new/blog

### New function approach for live edit

I actually properly looked at the code flow and found a neat vtable that had, essentially, what I was dreaming of when I made the version version of this. The function gives the name, min, max, step and address of incoming variable manager controlled settings, which is rly rly good! A much more sane and easy to develop for approach than the manually mapping out address spaces approach I was taking before!

### Patterns

I've tried to use patterns for the most part, this means finding the function and trying to write a "pattern" of bytes that matches both main versions of the game (Steam and EA). This can be pretty tricky as you have to be specific enough that you don't get false positives while being loose enough to get both versions. It seems to work on both EA/Steam version (with the exception of the patches), but please let me know if it doesn't.

### Patches

NERD ZONE NERD ZONE SKIP THIS PART

These will primarily be direct naked ASM patches to functions, and I'll probably be keeping them exclusive to the steam version of the game unless there's something that's a substantial improvement, as the EA one has been compiled differently so different instructions are used, meaning I'd have to write two patches. You can probably skip using them for now if I'm being honest, I have a bunch in the works but these mostly suck.

#### Point intersection patch

Essentially this is a hand written naked ASM patch for the point intersection code TS3 uses, this is used for nav meshing IIRC, and was the first proper one of these I did after seeing it in vtuner. It's not really going to be noticeable at all, maybe a second faster on custom maps, as they bulk call it during load. IIRC non-custom ones also call it during sims moving but I could be wrong as I wrote this aaages ago and didn't keep great notes. It has a lot of stuff in it and does achieve a very substantial boost vs the default function as a result, it's just that the function isn't really a cause of lag (though is on the render thread IIRC so..).

It achieves the performance boost by I guess nearly fully rewriting it, I added an early exit, use prefetchnta, switched to SIMD, lddqu, shufps, fast paths… idk these are all words that mean nothing to basically everyone reading this including me but I'm quite happy with how it turned out.

#### Target Framerate

~~Changes what I can only assume is the games framerate target, it seems to interact w/ the frame buffer. I haven't noticed any noticeable difference but give it a go! This one may desync things in theory but I haven't noticed so~~🤷 lol actually this makes animations slow down for some reason so I've removed it oops, need to look into this more.

#### Lot Visibility

Disables the games check for if a lot is in view, instead it should just load based on the radius around the camera. This is a the patch in Stutter Reducerer so if you're using that you don't need this (just enable Lot Throttling under streaming)

#### LZ Optimization (not released)

This is basically the point intersection but for the games main package decompression (RefPack) implementation. I… am/was a bit of a dunce with this, for whatever reason my original implementation was focused on large reads using AVX,SSE, etc… Now, the issue is… LZ does small (1-4) byte reads… so… I'm in the process of rewriting it once more. Plan to uroll the functions and do idk like MOVZX I think would be huge, simplifying the flow, etc etc.

The performance gain might actually be somewhat noticeable for this as this function is used in gameplay quite a bit. How you might ask? Well, every single lot that is loaded reads and extracts files for every object in that lot every single time it is loaded, in excess of 40mb/s with that being several hundreds of thousands of calls (some files are very small, and refpack also is byte-by-byte). This is all also done on the render thread, so the game has to wait for this to do its thing (and several other functions) before it renders the next frame. Very good very fun.

#### Multithreading (maybe eventually)

I've tried but no real luck, I've had some "success" but mostly it's been failures. I do have some interesting things I want to look at, especially related to lot loading so we'll see what the future holds. I think some degree of multithreading for the render thread would be incredibly huge.

#### Other patches

I have like 10 other ones that are not quite ready yet but should help, hopefully I'll just trickle them out but I think waiting until I can get them working properly is probably dumb.

## Coming soon:

Stuff that I'm working on I swear I promise!!! It's coming!

- UI QoL - There's a lot wrong with it.
- "Options" category support. This is tricky as changing these settings directly writes them to the .ini file in documents for some reason
- Several patches
- A system for automatic performance tweaks like reducing active lots to 0 when loading, then throttle-loading them back once game has loaded, or automatically reducing settings related to object caps, etc.
- More live-edit settings. There's a bunch I want to add it's just annoying. Most are static values though but having to find patterns that find them for both versions is rly annoying. Things like RenderSimLODDistances, FogDistances, the Script category…etc. If you have any requests lmk!
- The ini file is kinda ugly garbage horrible to look at
- Need to figure out why these have min/max/step like, is there some sort of debug ui I'm missing out on? It seems like there is but idk how to trigger it, gunna be pissed if it's something obvious