# Sauerbraten Client #

This is a redevelopment of [QuEd](https://github.com/quality-edition/QuEd "Quality Edition"), but with many bug-fixes and crossing-out of unnecessary stuff.

## Disclaimer ##

Because this is not yet tested fully, there _might_ be bugs that will crash the client sometimes. if you happened to experience one, please contact me or open a new issue.

## Main Features ##

* NO SDL2
* Improved frame drawing and input handling
* Extensive extinfo usage
* GUI Updates
* HUD Updates
* Commandline Helper
* Particle and sound updates (improved versions of whats found in SE and RR)
* Demo: recording, previewing, serverdemos, extended infos
* Easily switchable skins (ingame)

## Releases ##

Releases will be uploaded every time I find it is necessary to do because new features were introduced and are sorta stable.  
They were built on Windows 10 64bit in Codeblocks using the TDM-GCC-64 compiler, without getting warnings, and are hopefully running crash-free.

## Todo ##

Among other stuff:
* GeoIP for 64bit (libraries needed!)
* Lua-Integration

## Build ##

There's a codeblocks project which can be used on Linux, Windows and OSX (thought only libraries and executables for Windows will be provided). If you wish, you can of course create your own Makefile or Visual Studio Project.  
All dependencies needed for Windows building should be included

## Install ##

Once-and-forever:  
> Not Recommended!  
> 1. "Download Zip" and extract it to a new folder  
> 2. Download the executables under "Release" or [compile](#build) them yourself  
> 3. See "Other"  

Keep up-to-date:
> Recommended!  
> Make sure you are signed up on GitHub and have a [GitHub-client](https://windows.github.com "GitHub for Windows") installed.  
> 1. "Clone in Desktop"  
> 2. Download the executables under "Release" or [compile](#build) them yourself  
> 3. See "Other"  
> 4. Make sure to open your Github client regularly to check for updates  

Other:
> Copy the _packages/_ folder of Sauerbraten in the folder this Repo is cloned/installed to. A recent SVN version would be the best..  
> **IF** you installed the files from the SVN folder, make sure you the maps are still the ones from the Collect Edition release. If not, just copy the _packages/base/_ folder of the release in the folder this repo was cloned/extracted to