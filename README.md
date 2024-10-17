Hi! Appologies for the really bad code, I will maybe hopeufuly rewrite/refactor it eventually, but I'm still working on reversing some stuff and it feels kinda pointless to do both at the same time.

I'm not going to be using the repo outside of major releases, please forgive me.

To build the project you'll need to [vcpkg](https://github.com/microsoft/vcpkg) install detours x86, imgui should work fine(?)

Feel free to contact me: [Tumblr](https://sims3fiend.tumblr.com/) | [Discord DM](https://discord.gg/akgJACJZ)


## Installation and use
Please note, some settings wont appear until you load into a world.

### Installation and basic usage:
- Download the ASI file and wack it in to your Sims 3 base directory, where the ts3w.exe is located. If you're using one of the presets, make a folder called s3ss_presets and pop them in there (you'll need to activate them in the presets menu ingame).
- Make sure you have an ASI loader, these are either (links). I recommend dxwrapper, just make sure you set the `LoadPlugins` value to 1 (should be default)
- Start the game, you might experience a little more of an initial "freeze" when starting the game than usual, this is from the script logging a bunch of config calls during initialization, there's like 800 or something nuts. It should not have any negative impact on regular loading or gameplay, and I plan to turn the logging off... eventually. 
- Press Insert to open up the menu. Go crazy and change everything, make the sun huge, crank bloom up, live.

### Help I crashed/the game doesn't start with the mod!

Please send me your hooks_log.txt if you're experiencing any crashing issues. If the crash is because you set some value to like 7 billion, that's on you, you can just delete the line out of script_settings.ini or go to Settings -> Clear all settings

If you can't get the game to run with the mod, lmk also, please tell me if you're using a launcher, if you're using any other .asi mods, using dxvk, etc. as well as what operating system you're on.

---

## Features
### Live Edit
This is the new™ and now main part of the mod. I've mapped out several/most of the exes main "settings" (anything that interacts w/ 0x005a00a0 and some that don't) areas, which allows you to now, in game, change these values whereas before it was a whole arduous process of making .package mods.  I mapped these all statically so some of the offsets/addresses might be wrong.

I was gunna list the settings but there's 260~ of them so maybe not?

I plan to add missing specific individual settings from Config eventually. If you think a setting is missing, or if you think I've mapped a value wrong (i.e. you know it has an effect but it's not working with my mod or is crashing you, or one value is changing multiple things), please let me know. Render/er is definitely missing some, that's because the function is scary and I don't like it.

Values (sometimes) have sliders with the min and max value I found in the exe set, if you want to go higher, you can double click to type in your own number.

Some interesting things you can do with the settings:

- Set max lots higher than 8 AND increase the radius so it actually shows (will crash if set too high ~35+, need to investigate) by changing values in Streaming  
- Play in a game where the sun never sets or rises by editing Sky Common -> Sunset/Sunrise Time
- Change shadow settings (same thing as LD's shadow asi thing), extending shadows (they will still look hideous, writing a post about why currently) in Shadow Settings
- Change various light settings to get the perfect look for your game. Some popular mods edit these values for their looks (presets soon?)


### Game Config
The function we're hooking (0x0058c380) only seems to effect Config (GraphicsRules.sgr in the .exe directory) and Options (Options.ini in the documents/Sims3 directory), but logs a whole bunch of other thing. Feel free to toggle the option in the settings tab and try changing a bunch, it should in theory work because the function is reading and writing but somewhere it gets overridden or something idk 🤷

It lets you set any that fall under those two categories/headings, which means there's some like `ForceHighLODObjects` that aren't in the actual file and are settable. You might notice some show different values than what they're set as in your config, this could either be that I'm hooking it too early (I don't think I am), or the value is getting overwritten or changed somewhere in the exe. If there's a setting that's in the file but not in the list that you think does something, lmk, but it should capture everything.

I haven't mapped all of the Config/Option settings to Live Edit as they're all split up in the exe, if there's one you want in particular, lmk.

### Presets
I've prepared a preset with just the essentials from my [GraphicsRules file post](https://www.tumblr.com/sims3fiend/747099242410147840/new-master-post?source=share) with the idea that you can then use this with a stock GraphicsRules file instead of having to manage different versions, giving you the ability to toggle certain things back to default. I might make some visual "enhancement" presets or something later, either based off popular mods or my own insanity, we'll see.

Presets go into the s3ss_presets folder, and currently they stack (not intentional but I might keep it)
### Known issues:
- Rendering toggles need to be re-toggled each load - Easy fix I'm just lazy
- Options settings overwrite the actual Options.ini file (idk why??)
- Occasionally D3D9 wont hook, I can't replicate this reliably to test so lmk if you can lmao
- I mapped all the settings pretty hastily, so some are bound to be wrong
- Was flagged as a virus briefly??? Praying this never happens again because I have no idea what to do to fix that dshjakfhhsdaj
- Presets stack, if you apply a preset and you have existing values, they stack together... I kind of like that though as a concept so I just added a clear all option to settings, I might rework it later.
- Some Live Edit value locations might change during gameplay, resulting in the menu displaying them incorrectly and crashing the game if edited in a broken state. I've checked most off them and they don't seem to, but Render ones did. Let me know if you experience it as I can probably find a static pointer like I did for Render.

### Planned things:
- Searching. God that'd be good...
- Go over existing maps again, some I did early on before I supported static values, 4 float arrays, etc. so I've probably messed some up
- Adding every single GraphicsRule.sgr setting to Live
- Maybe adding some of my performance mods to it? Or should I keep them as their own individual thing? Mmmm I dunno
- I still haven't looked at the way everyone else has been editing the "live" settings, so I should probably do that, there's probably a lot of info out there but at this point I'm too invested in my weird approach djsakfsksaffsa
