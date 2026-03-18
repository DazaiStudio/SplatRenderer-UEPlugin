# Splat Renderer

3D/4D Gaussian Splatting renderer plugin for Unreal Engine 5.6. Precompiled — no C++ compilation needed.

![Plugin Browser](docs/plugin-browser.png)

## Features

- **BP_3DGS** — Render static 3D Gaussian Splats from `.ply` files
- **BP_4DGS** — Play 4D Gaussian Splat sequences (`.bin` folders or `.gsd` compressed format)
- OBB Crop Volume with draggable editor widget
- Brightness and splat scale controls
- WAV audio playback synced to sequence
- Editor preview without PIE

## Installation

### Option 1: Git Clone

```bash
cd "YourProject/Plugins"
git clone https://github.com/DazaiStudio/splat-renderer.git
```

### Option 2: Download ZIP

1. Click **Code** > **Download ZIP** on this page
2. Extract the `SplatRenderer` folder
3. Copy it into your UE project's `Plugins/` directory

![Install Folder](docs/install-folder.png)

4. Open your project in UE 5.6

## Quick Start

1. Open the **Content Browser**
2. Navigate to **Plugins > Splat Renderer Content > Blueprints**

![Content Browser](docs/content-browser.png)

3. Drag **BP_3DGS** or **BP_4DGS** into your level
4. Select the actor and set the file path in the **Details** panel

### BP_3DGS — Static Gaussian Splats

Set `PLY File Path` to any standard `.ply` file (COLMAP, Luma, PostShot, etc.)

![BP_3DGS Details](docs/bp-3dgs-details.png)

### BP_4DGS — 4D Gaussian Splat Sequences

Set `Sequence Path` to a `.json` (raw bin folders) or `.gsd` (compressed) file.

![BP_4DGS Details](docs/bp-4dgs-details.png)

| Property | Description |
|----------|-------------|
| Sequence Path | Path to `.json` or `.gsd` sequence file |
| Audio Path | Optional WAV file for synced audio |
| Playback Speed | 1.0 = normal speed |
| Playback Mode | Loop or Once |
| Start/End Frame | -1 = auto (use all frames) |
| Scrub Frame | Manually seek to a specific frame |
| Brightness | Adjust splat brightness (0.0 - 5.0) |
| Splat Scale | Adjust splat size (0.01 - 5.0) |
| Crop Volume | Enable OBB crop box to mask regions |

## 4DGS Sequence Format

For 4DGS playback, use the `.gsd` compressed format for best performance.

Use [**4DGS Converter**](https://github.com/DazaiStudio/4dgs-converter) to convert 4DGS training output into `.gsd` files with LZ4 compression and random frame access.

## Requirements

- Unreal Engine 5.6
- Windows (DirectX 12)
- Blueprint project (no C++ required)

## License

All rights reserved. For evaluation and testing purposes only.
