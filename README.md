# FourthTube  

A work-in-progress homebrew YouTube client for the 3DS  

[GBAtemp Thread](https://gbatemp.net/threads/fourthtube-for-now-a-fork-of-thirdtube-that-works.660775/)

[Discord Server](https://discord.gg/UPK3wDTTE2)

## Instability Warning

As this app is still in the beta stage, you may and will encounter crashes and other bugs.  
If you find one of those, it would be helpful to open an issue on this GitHub repository.  

**Please don't make duplicate issues (such as O3DS crashing). This will only make it harder to keep track of bugs.**

## Credits

  [WindowsServer2003](https://github.com/windows-server-2003) - The creator of ThirdTube.

  [Smu1zel](https://github.com/Smu1zel) - Figured out the line of code that needed to be changed and tested the change out, fixing the app after it stopped working in 2024.

  [NCP 3.0](https://github.com/erievs) - Fixing the app after it stopped working in 2024.

  [5GBurrito](https://github.com/5GBurrito) - Minor changes (project manager?).

  [2b-zipper](https://github.com/2b-zipper) - New banner, watch history fix, new icon, 480p support, some other fixes, and a fair bit more (thank you so much!).

  [ItsFrocat](https://github.com/ItsFrocat) & [Dragontwo14](https://github.com/Dragontwo14) - For the strings used in the German translation.

  [cooolgamer](https://github.com/cooolgamer) - For the strings used in the French translation, as well as a custom boot screen.

  [Dxni](https://github.com/Icee666) - For the strings used in the Italian translation.
  
  [returndislike](https://returnyoutubedislike.com/install) - Used for dislikes.

## Description
It utilizes some undocumented YouTube APIs to get the raw video URL and plays the stream using the decoder taken from [Video player for 3DS by Core-2-Extreme](https://github.com/Core-2-Extreme/Video_player_for_3DS).  

It does not run any JavaScript code or render HTML/CSS, so it's significantly faster than YouTube on the browser.  

The name is derived from the fact that it is the fourth YouTube client on 3DS, following the official YouTube app (discontinued), then the New 3DS browser, and ThirdTube.  

## QR code
You can use the QR code below to download & install the .cia directly from your system. 

<img src="https://github.com/erievs/FourthTube/blob/main/images/qr-code.png" width="200" height="200">

## Screenshots
<img src="https://github.com/windows-server-2003/ThirdTube/blob/main/images/0.jpg" width="400" height="480"> ![](https://github.com/windows-server-2003/ThirdTube/blob/main/images/1.bmp)  
![](https://github.com/windows-server-2003/ThirdTube/blob/main/images/3.bmp) ![](https://github.com/windows-server-2003/ThirdTube/blob/main/images/4.bmp)  

## Features

 - Video Playback up to 480p  
 - Livestreams and premiere videos support  
 - Searching  
 - Video suggestions  
 - Comments  
 - Captions  
 - Local watch history and channel subscriptions  
 - No ads  
   It's more like "Ads are not implemented" rather than "We have ad-blocking functionality".  
   Of course, we will never "implement" it :)  

## Controls

 - B button : go back to the previous screen  
 - C-pad up/down : scroll
 - L/R : switch between tabs
 - Select + Start : blackout the bottom screen
 - In video player
    - Arrow left/right : 10 s seek
    - ZL/ZR : 5 s seek

Below are for debug purposes

 - Select + X : toggle debug log
 - Select + Y : toggle memory usage monitor
 - Select + R + A : toggle FPS monitor



## Requirements
A 3DS (including 2DS) with [Luma3DS](https://github.com/LumaTeam/Luma3DS) installed and with the DSP firmware dumped. On modern Luma3DS versions, you can [do this using a built-in feature](https://3ds.hacks.guide/finalizing-setup.html#section-iii-rtc-and-dsp-setup:~:text=dump%20the%20sound%20firmware). Otherwise, you can use [DSP1](https://github.com/zoogie/DSP1).  
We haven't tested the minimum system version, but at least 8.1.0-0 is needed.  

## FAQs

 - Does it make sense?  
   The **worst** question in the console homebrew scene. Isn't it just exciting to see your favorite videos playing on a 3DS?
 - How can I migrate my data from ThirdTube to FourthTube?  
   Rename the /3ds/ThirdTube folder to FourthTube. If you were using an unofficial fork of ThirdTube (such as a translation), this folder may be named differently.
 - What are these app data options in Advanced?  
   This selects what client FourthTube will "spoof" to trick YouTube into giving us video URLs.
   The default "iOS" setting works for most users. If you can't play videos for some reason, try switching to visionOS (recommended) or Android VR (not recommended except as a last resort).

## Building
[Click Here for instructions](/Documentation/Build%20Instructions.md)

## License
You can use the code under the terms of the GNU General Public License GPL v3 or under the terms of any later revisions of the GPL. Refer to the provided LICENSE file for further information.

## Third-party licenses

### [FFmpeg](https://ffmpeg.org/)
by the FFmpeg developers under GNU Lesser General Public License (LGPL) version 2.1  
The modified source code can be found at https://github.com/windows-server-2003/FFmpeg/tree/3ds.  
### [rapidjson](https://github.com/Tencent/rapidjson)
by Tencent and Milo Yip under MIT License  
### [libctru](https://github.com/devkitPro/libctru)
by devkitPro under zlib License  
### [libcurl](https://curl.se/)
by Daniel Stenberg and many contributors under the curl License  
### [libbrotli](https://github.com/google/brotli)  
by the Brotli Authors under MIT license
### [stb](https://github.com/nothings/stb/)
by Sean Barrett under MIT License and Public Domain  

## Archived ThirdTube credits
* Core 2 Extreme  
  For [Video player for 3DS](https://github.com/Core-2-Extreme/Video_player_for_3DS) which this app is based on.  
  Needless to say, the video playback functionality is essential for this app, and it would not have been possible to develop this software without him spending his time optimizing the code sometimes even with assembly and looking into HW decoding on the new 3DS.
* dixy52-beep  
  For in-app textures
* [PokéTube](https://github.com/Poketubepoggu)  
  For the icon and the banner
* The contributors of [youtube-dl](https://github.com/ytdl-org/youtube-dl)  
  As a reference about YouTube webpage parsing. It was especially helpful for the deobfuscation of ciphered signatures.  
* The contributors of [pytube](https://github.com/pytube/pytube)  
  As a reference about YouTube webpage parsing. Thanks to its strict dependency-free policy, I was able to port some of the code without difficulty.  

