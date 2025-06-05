# SekiroFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**SekiroFix** is an ASI plugin for **Sekiro: Shadows Die Twice** that can unlock the framerate, add ultrawide/narrower support and more.

## Features

### General
- Unlock framerate.
- Adjust gameplay FOV.
- Borderless windowed mode.

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
- Open **`SekiroFix.ini`** to adjust settings.

## Screenshots
| ![animated-comparison](.github/images/sekiro_comparison.png) |
|:--------------------------:|
| Gameplay

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
