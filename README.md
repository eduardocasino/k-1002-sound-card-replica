# K-1002 8 bit audio Digital to Analog Converter replica

## About

This is a faithful replica of the K‑1002 8‑bit audio Digital‑to‑Analog Converter card for the KIM‑1, SYM‑1, and AIM‑65 computers, originally designed by MTU in the late ’70s.

I'm using the original K‑1002 documentation and high‑resolution photos from [Hans Otten's Retro Computing website](http://retro.hansotten.nl/).

![finished card](https://github.com/eduardocasino/k-1002-sound-card-replica/blob/main/card/images/k1002.jpg?raw=true)
![front](https://github.com/eduardocasino/k-1002-sound-card-replica/blob/main/card/images/K-1002-sound-card-replica-front.png?raw=true)
![back](https://github.com/eduardocasino/k-1002-sound-card-replica/blob/main/card/images/K-1002-sound-card-replica-back.png?raw=true)

### Important!

Although the card uses the familiar 44‑pin edge connector, it’s not a 1‑to‑1 match for the KIM‑1 application bus, so an adapter is required, as explained in the manual. I’ve also designed such an adapter, which you can find in the `adapter` folder.

![adapter](https://github.com/eduardocasino/k-1002-sound-card-replica/blob/main/adapter/images/app-mtu-adapter.png?raw=true)

## Building

There’s nothing particularly special about assembling the board. As with any other PCB, just start soldering components in order of height. All the ICs are socketed.

There’s a BOM spreadsheet in the `card/docs` folder, but there are a few things to keep in mind regarding some of the components:

* R1 to R13 are 51K resistors that need to be matched within 1%. You can either use modern metal‑film resistors with that tolerance or manually match carbon ones. I went with the latter to keep the look closer to the original.
Also, the documentation isn’t very clear about whether &plusmn;% is actually enough or if it really means &plusmn;0.5% (so that all resistors fall within a 1% spread). I chose the latter interpretation as well.

* The 92PU01 and 92PU51 transistors are no longer available. The BD135‑10 and BD136‑10 are perfectly good functional replacements, but they use a different package and footprint, so you’ll need to bend the leads to fit them on the board.
 **NOTE**: They *must* be the "-10" variants!!

* The original potentiometer is no longer manufactured. A drop‑in replacement is the Piher PT15NH06 with the adjustment wheel. **NOTE**: Make sure you get the NH06 variant.


## KIM-1 Software and PC Utilities

I've only built it on a Debian Linux and on WSL with Ubuntu. I can't give support for other systems, sorry.

### Prerequisites

Any modern Linux distribution, tested on `Debian 12.6` and `Ubuntu 24.04`

Install the required packages:

```console
$ sudo apt update
$ sudo apt install build-essential cc65
```

### Build and usage

Just go to the `software` folder and run`make`:

```console
$ cd software
$ make
```

This will build the PC utilities and the KIM‑1 demo programs. The latter are generated in PAP format and are named with a numeric prefix that matches the file numbers in the K‑1002‑1L 8‑BIT Digital Music Software manual. Use them as instructed there.

Additionally, pcmplay.pap is a simple PCM player that plays a short sound snippet. Load it into your KIM-1 and run it at `$0200`.

The PC utilities are built into `software/utils/bin`:

* `wavegen` is a modern C version of the `kimfs` program. It generates a waveform table suitable for use with the MTU utilities from a very simle YAML description file.

* `notcmp` is the C version of the NOTRAN compiler. It accepts the same input files as its MTU counterpart and generates compatible object code for the NOTRAN interpreter.

* `notint` is a NOTRAN interpreter simulator that can either play a NOTRAN bytecode file through an ALSA device or generates WAV files to be played with any WAV player.

Run any of them without arguments to see the usage instructions.

## Licensing

This is a personal project that I am sharing in case it is of interest to any retrocomputing enthusiast and all the information in this repository is provided "as is", without warranty of any kind. I am not liable for any damages that may occur, whether it be to individuals, objects, KIM-1 computers, kittens or any other pets. **It should also be noted that everything in this repository is a work in progress and, although the board has been tested, may contain errors. Therefore, anyone who chooses to use it does so at their own risk**.

### Card

[![license](https://i.creativecommons.org/l/by-nc/4.0/88x31.png)](http://creativecommons.org/licenses/by-nc/4.0/)

Th K-1002 replica is licensed under a [Creative Commons Attribution-NonCommercial 4.0 International License](http://creativecommons.org/licenses/by-nc/4.0/).

### Adapter

[![license](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-sa/4.0/)

The adapter card is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/).

### MTU Software

Although it probably doesn’t matter much nowadays, since this software was written for a very obsolete and rare piece of hardware, the material is still copyrighted by MICRO TECHNOLOGY UNLIMITED, a company that is [still in business](http://www.mtu.com/catalog/index.php). I’m sharing it here solely to help anyone who owns a K‑1002 card.

### PC Utilities

All my code is licensed under the GNU General Public License, Version 3.

## Acknowledgements

* Hans Otten and his [Retro Computing blog](http://retro.hansotten.nl/). A lot of information and high resolution pictures of a K-1002
