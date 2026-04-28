# Pure Dreams — `('-')`

A VCV Rack 2 module that replaces the background with audio-reactive [MilkDrop](https://en.wikipedia.org/wiki/MilkDrop) visuals powered by [projectM](https://github.com/projectM-visualizer/projectm).

![Pure Dreams demo](res/demo3.gif)

`+` / `–` cycle through 413 presets. Right-click for a searchable list. **DIM** controls brightness. **SMOOTH** controls how tightly the visuals follow the audio — patch your dry signal (before reverb) into **IN**.

---

## Requirements

macOS only. Requires **projectM 3.1.x**:

```bash
brew install projectm
```

The module loads on Linux and Windows but the background remains dark.

---

## Building

```bash
export RACK_DIR=/path/to/Rack-SDK
make
make install
```

Override the projectM path if needed: `make PROJECTM_PREFIX=/usr/local`

---

## License

GPL-3.0-or-later — see [LICENSE](LICENSE)
