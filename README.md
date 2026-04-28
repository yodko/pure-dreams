# Pure Dreams — `('-')`

A VCV Rack 2 module that replaces the grey rack background with audio-reactive [MilkDrop](https://en.wikipedia.org/wiki/MilkDrop) visuals powered by [projectM](https://github.com/projectM-visualizer/projectm).

Drop it anywhere in your rack. The entire background becomes a live visualiser that reacts to whatever you patch into its audio input.

![Pure Dreams screenshot](res/screenshot.png)

---

## Controls

| Control | Function |
|---|---|
| `+` / `–` | Step forward / backward through presets |
| Binary LEDs | Current preset index in 9-bit binary (hover for n/total tooltip) |
| DIM | Background brightness |
| SMOOTH | IIR low-pass on the audio fed to projectM. LED in the fader shows the audio envelope — erratic at bottom, slow smooth pulses at top |
| IN | Audio input (patch your dry signal here, before reverb/delay) |

Right-click the module for a searchable preset list (413 MilkDrop presets).

---

## Requirements (macOS only)

The visualiser requires **projectM 3.1.x** installed via Homebrew:

```bash
brew install projectm
```

The module loads on Linux and Windows but the background will remain dark — projectM rendering is macOS-only in this release.

---

## Building

```bash
export RACK_DIR=/path/to/Rack-SDK
make
make install
```

On macOS, `PROJECTM_PREFIX` defaults to `/opt/homebrew/Cellar/projectm/3.1.12`. Override if your installation differs:

```bash
make PROJECTM_PREFIX=/usr/local
```

---

## Tips

- Patch your **dry signal** (before reverb/delay) into IN — reverb tails prevent the background from going dark between notes, which reduces the visual rhythm
- Use the SMOOTH slider to control how tightly the background follows transients vs overall dynamics
- Pure Dreams works alongside [Purfenator](https://library.vcvrack.com/Purfenator) — set Purfenator's background to off

---

## License

GPL-3.0-or-later — see [LICENSE](LICENSE)
