# ExpressLRS Backpack: race-day build for HDZero goggles

This fork of the [ExpressLRS Backpack](https://github.com/ExpressLRS/Backpack)
adds race-day features to the backpack inside **HDZero goggles** (the ESP32 the
goggles use to talk ExpressLRS). Everything the stock backpack does still
works: it's current ExpressLRS Backpack master with these additions on top.

**[Download the latest race build](https://github.com/bob9/Backpack/releases/tag/v2026.07.17-aufpv-race)**
· pairs with [HDZero goggle firmware 9.5.45](https://github.com/bob9/hdzero-goggle/releases/tag/v9.5.45)
· built for race timing with [**dd-pits**](https://dd-pits.threedigitalllamas.com.au)

## What it adds to the stock backpack

### Change channel on the goggles, and the quad follows

HDZero goggles have always sent their channel to the backpack, from the
**Send VTX** button and a dial long-press, but the stock backpack drops that
message. This build passes it on over ESP-NOW as `MSP_SET_VTX_CONFIG`, the
same message ExpressLRS's VTX Administrator uses, so everything in your bind
group follows with stock firmware:

- your **TX backpack** hands it to the radio, which updates its VTX
  Administrator setting and retunes **your drone's VTX** over the ELRS link
  (the same set-up you need for VTX Administrator from the Lua script today);
- other **goggles** bound to the same phrase retune to match;
- a **race timer** listening on your bind group stays in sync.

It works with stock HDZero goggle firmware (Send VTX, or a dial long-press on
a new channel). With [HDZero goggle firmware 9.5.45](https://github.com/bob9/hdzero-goggle/releases/tag/v9.5.45)
you also get **Auto Send VTX** (sends on every channel change), a long-press
**re-send** in case the drone missed it, and a green `VTX SENT` confirmation
on screen.

> **Race-day tip:** leave Auto Send VTX off when a race timer assigns your
> channel. Otherwise flicking through channels to watch someone else retunes
> your own powered-up quad.

### DVR recordings named after the race

Stock goggles name every recording `hdz_0042.mp4`, so after a race day you're
left guessing which file is which heat. With this backpack and
[HDZero goggle firmware 9.5.45](https://github.com/bob9/hdzero-goggle/releases/tag/v9.5.45),
the race timer names each pilot's recording after the race they're about to
fly:

```
hdz_0042-WinterCup-Mains-A-R5-H1.mp4
hdz_0043-WinterCup-R6-H2.mp4
hdz_0044-WinterCup-CTA-R8-H3.ts
```

**How it works.** When a heat is cued, the timer sends each pilot's goggles a
label (`MSP_ELRS_BACKPACK_SET_DVR_NAME`, `0x030F`). This backpack passes it
straight through to the goggles (the stock backpack drops it). The next
recording the goggles start takes that label, keeping the goggles' own file
counter in front so files still sort in order.

**Turn it on (once, on the goggles):** **Menu → Record Option → Naming Scheme →
ELRS**. The other schemes (Digits, Date) ignore race labels. The Naming Scheme
row stays greyed out until the goggles know the time; the backpack's clock sync
(below) sets it as soon as the timer is running.

**The name format.**

```
hdz_<counter>-<race label>.<mp4|ts>
```

- `hdz_<counter>` is the goggles' normal four-digit recording number.
- `<race label>` is whatever the timer sends. The goggles keep letters, digits,
  `-` and `_`, turn spaces into `-`, drop anything else, and cap it at
  **43 characters** (so the whole name stays inside the goggles' playback list).
- The extension is your recording format (MP4 or TS).

[dd-pits](https://github.com/bob9/dd-pits) builds the label like this:

```
<Meeting>-<Mains-A … Mains-F | CTA>-<R|Q><round>-H<heat>
```

- `<Meeting>` is your event name, letters and digits only, shortened to fit.
- `Mains-A` … `Mains-F` or `CTA` appears only when the round is a lettered main
  or a chase-the-ace final.
- `R` is a race, `Q` a qualifying (time trial) round, then the round number;
  `H` is the heat number in that round.

**When it applies.** A label is used for the recording that starts after it
arrives. The timer clears it when the heat ends, and the goggles drop it after
5 minutes anyway, so a freestyle flight later never inherits a race's name.
Without a label (no timer, or another naming scheme), recordings are named
exactly as on stock firmware.

### The goggle clock, set over the air

The TX backpack sends the time (`MSP_ELRS_BACKPACK_SET_RTC`) to the goggle
backpack over ESP-NOW, and the goggle backpack sets the goggles' clock, so
recordings carry the right date and time. The TX backpack gets the time from
WiFi (NTP) or from whatever sends it `SET_RTC` on its serial port, such as a
race timer.

### Built for dd-pits

[**dd-pits**](https://dd-pits.threedigitalllamas.com.au) is the free race-day
companion for RotorHazard and FPVTrackside: a live dashboard on every phone,
brackets, OBS overlays, race calls, AI commentary and lap times in pilots'
goggles. With this backpack it also:

- names each pilot's DVR recording after their heat,
- keeps the goggle clock right,
- puts every pilot on their race channel as each race is cued, Lowband too
  with the [race-day goggle firmware](https://github.com/bob9/hdzero-goggle/releases/tag/v9.5.45).

[Website and download](https://dd-pits.threedigitalllamas.com.au) ·
[Watch it in action](https://dd-pits.threedigitalllamas.com.au/#watch) ·
[HDZero firmware on dd-pits](https://dd-pits.threedigitalllamas.com.au/#hdzero) ·
[Source on GitHub](https://github.com/bob9/dd-pits)

## Install

Setting up a whole race day (timer, dd-pits, goggles)? The
[best race-day setup guide](https://dd-pits.threedigitalllamas.com.au/best-setup)
covers every step. For just this backpack:

For **HDZero Goggle** and **Goggle 2**. (ExpressLRS uses the same backpack
firmware for the BoxPro.) You need the goggles, their SD card and a computer.

1. Download **`AUFPV_HDZero_Goggle_Backpack.zip`** from the
   [latest race build](https://github.com/bob9/Backpack/releases/tag/v2026.07.17-aufpv-race).
2. Extract it. You get four files: `bootloader.bin`, `partitions.bin`,
   `boot_app0.bin` and `firmware.bin`.
   - **macOS:** extract from Terminal (`unzip AUFPV_HDZero_Goggle_Backpack.zip`),
     not by double-clicking: Safari and Finder can damage the `.bin` files.
   - **Windows:** extracting in Explorer is fine.
3. On the goggles' SD card, create a folder called **`ELRS`** at the top level
   (if it isn't there already) and copy the four files into it, so they sit in
   `SD:/ELRS/`.
4. Put the card in the goggles, power them on and go to
   **Menu → Firmware → Update ESP32**. Confirm, and wait for the done message.
5. Delete the four files from the card afterwards.

### Binding

The build has **no bind phrase built in**, and updating doesn't touch your
existing binding: a bind phrase flashed earlier with the ExpressLRS
Configurator, or a traditional binding, is kept. If your radio or the race
timer can't reach the goggles afterwards, re-bind: put the goggle backpack in
bind mode, then choose **Backpack → Bind** in your radio's ELRS Lua script.

### Going back to stock

Flash the stock ExpressLRS backpack for **HDZero Goggles** with the
[ExpressLRS Configurator](https://github.com/ExpressLRS/ExpressLRS-Configurator)
(it's in the Backpack targets, over UART or WiFi). The goggle firmware's DVR
naming and channel features simply go quiet without this backpack.

## Upstream

The DVR-name and clock changes are open pull requests to ExpressLRS Backpack
([#232](https://github.com/ExpressLRS/Backpack/pull/232),
[#231](https://github.com/ExpressLRS/Backpack/pull/231)). The goggle channel
broadcast is ready for upstream on the
[`goggle-channel-broadcast`](https://github.com/bob9/Backpack/tree/goggle-channel-broadcast)
branch. This fork isn't affiliated with ExpressLRS LLC or HDZero.

| Branch | What's on it |
| --- | --- |
| `race-day-with-auto-vtx` | The race build: all three features (released as `v2026.07.17-aufpv-race`) |
| `goggle-channel-broadcast` | Goggle channel changes over ESP-NOW only |
| `dvr-recording-name` | DVR name pass-through only |
| `rtc-sync` | Clock sync only |
| `master` | ExpressLRS Backpack master, plus this README |

---

## About the ExpressLRS Backpack

[![Release](https://img.shields.io/github/v/release/ExpressLRS/Backpack?include_prereleases)](https://github.com/ExpressLRS/Backpack/releases)
[![License](https://img.shields.io/github/license/ExpressLRS/Backpack)](https://github.com/ExpressLRS/Backpack/blob/master/LICENSE)
[![Chat](https://img.shields.io/discord/596350022191415318)](http://discord.gg/dS6ReFY)
[![Open Collective backers](https://img.shields.io/opencollective/backers/expresslrs?label=Open%20Collective%20backers)](https://opencollective.com/expresslrs)

The ExpressLRS Backpack adds ESP-NOW–based wireless communication between ExpressLRS TX modules and compatible FPV hardware, allowing for remote configuration, control, and telemetry exchange. Developed and maintained by **ExpressLRS LLC** and its passionate open source community, working together to advance reliable, high-performance radio control technology.

ExpressLRS Backpack is developed and maintained by **ExpressLRS LLC** and its passionate open source community, working together to advance reliable, high-performance radio control technology.

### What is a "TX Backpack"?

Some of the ExpressLRS TX modules include an additional ESP8285 chip, which lets us communicate wirelessly with other ESP8285 enabled devices using a protocol called espnow. We call this chip the "TX-Backpack". The aim of the TX-Backpack is to allow wireless communication between ExpressLRS, and other FPV related devices for command and control, or for querying config.

### Sounds interesting... What type of FPV devices can it talk to?
 A prime use case is your video receiver module (or VRX). Currently there aren't many VRX modules that have an ESP8285 built in to allow them to communicate with ExpressLRS, so in most cases you need to add your own. A small ESP based receiver can be "piggybacked" onto your VRX module, which allows ExpressLRS to control the band and channel that your goggles are set to. We call this device the "VRX-Backpack". 

### Wow cool, so I'll be able to control the module via ELRS!? Which VRX modules does it work with?
The list of supported modules can be found on the wiki:
https://github.com/ExpressLRS/Backpack/wiki

### Great! I use one of the supported modules. How do I get a VRX-Backpack?
There are a few different options for both DIY or compatible off the shelf backpacks... check the wiki for the current list:
https://github.com/ExpressLRS/Backpack/wiki
