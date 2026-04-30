# Pure Dreams

A VCV Rack 2 module that replaces the background with audio-reactive [MilkDrop](https://en.wikipedia.org/wiki/MilkDrop) visuals powered by [projectM](https://github.com/projectM-visualizer/projectm). Works on macOS, Linux and Windows.

![Pure Dreams demo](res/demo3.gif)

<table>
<tr>
<td><img src="res/module.png" width="90"/></td>
<td>

**LED grid** — current preset index<br>
**+** — next preset<br>
**–** — previous preset<br>
**right-click** — searchable preset list<br>
**DIM** — brightness of the visuals<br>
**SMOOTH** — how tightly the visuals follow the audio<br>
**IN** — audio input

</td>
</tr>
</table>

## more presets

Drop `.milk` files into the plugin's preset folder and restart Rack.

- macOS — `~/Library/Application Support/Rack2/plugins-mac-arm64/PureDreams/res/presets/`
- Linux — `~/.local/share/Rack2/plugins-lin-x64/PureDreams/res/presets/`
- Windows — `%LOCALAPPDATA%\Rack2\plugins-win-x64\PureDreams\res\presets\`

## using with Purfenator

Right-click Purfenator → Colour and Background → untick Draw Background.
