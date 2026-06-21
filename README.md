# SekiroFix

**SekiroFix** is t3nka's maintained fork of Lyall's ASI plugin for **Sekiro: Shadows Die Twice**. It keeps the framerate, FOV, borderless, and ultrawide/narrower fixes from the original project, then adds optional gameplay and HUD tweaks for a cleaner playthrough.

## Features

### General
- Unlock framerate.
- Adjust gameplay FOV.
- Borderless windowed mode.
- Disable camera reset when locking on with no valid target.
- Automatically pick up enemy loot.
- Hide enemy awareness/directional detection markers.
- Hide low-health/dying and stealth vignette effects.
- Prevent Dragonrot from increasing on death.
- Disable Sen and Skill Experience death penalties.
- Log death and kill counters to text files.
- Increase Spirit Emblem capacity from prosthetic skill-tree upgrades.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Unlocked windowed resolution list.
- Fixed vignettes (low health/stealth).
- Fixed animation culling at wider aspect ratios.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder. (e.g **steamapps\common\Sekiro** for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- **Borderless Windowed** - Makes windowed mode run borderless.
- **Gameplay FOV** - Adjusts the gameplay FOV multiplier.
- **Disable Camera Reset** - Stops camera centering when lock-on is pressed without a valid target.
- **Auto Loot** - Automatically picks up enemy loot.
- **Hide Awareness Markers** - Hides enemy awareness/directional detection markers.
- **Hide Vignettes** - Hides low-health/dying and stealth screen vignette effects without hiding black screens or fade transitions.
- **Prevent Dragonrot** - Stops Dragonrot increasing when you die.
- **Disable Death Penalties** - Stops Sen and Skill Experience loss when you die.
- **Log Stats** - Writes `DeathCounter.txt` and `TotalKillsCounter.txt` to the game folder every 2 seconds.
- **Spirit Emblem Upgrade** - Makes prosthetic skill-tree upgrades increase Spirit Emblem capacity by 1. This permanently affects save progression once the upgrade is applied.
- **Unlock Framerate** - Unlocks the 60 FPS cap.
- **Unlock Resolutions** - Unlocks the windowed mode resolution list.
- **Fix Aspect Ratio** - Stops 16:9 scaling and fixes aspect-ratio-related issues.
- **Fix HUD** - Fixes vignettes and fades at ultrawide/narrower resolutions.

## Screenshots
| ![animated-comparison](.github/images/sekiro_comparison.png) |
|:--------------------------:|
| Gameplay

## Credits
Original SekiroFix by **Lyall**. Additional gameplay options by **t3nka**. <br />
Thanks to **Hotiraripha** for commissioning this fix! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
