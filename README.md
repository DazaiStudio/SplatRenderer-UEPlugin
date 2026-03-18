# Splat Renderer

3D/4D Gaussian Splatting renderer plugin for Unreal Engine 5.6.

## Features

- **BP_3DGS** — Load and render static 3D Gaussian Splat (.ply) files
- **BP_4DGS** — Play 4D Gaussian Splat sequences (.bin folders or .gsd compressed format)
- OBB Crop Volume with visual editor widget
- Brightness and splat scale controls
- WAV audio playback synced to sequence
- Editor preview (no PIE required)

## Installation

Copy the `SplatRenderer` folder into your UE project's `Plugins/` directory. No compilation needed — precompiled for UE 5.6.

## Usage

1. Open your project in UE 5.6
2. In Content Browser, search for `BP_3DGS` or `BP_4DGS`
3. Drag into your level
4. Set the file path in the Details panel:
   - **BP_3DGS**: Set `PLY File Path` to your `.ply` file
   - **BP_4DGS**: Set `Sequence Path` to your sequence `.json` or `.gsd` file

## 4DGS Sequence Format

For 4DGS playback, sequences can be in raw `.bin` folder format or compressed `.gsd` format.

Use [4DGS Converter](https://github.com/DazaiStudio/4dgs-converter) to convert 4DGS training output (e.g. from [MLSharp](https://github.com/ml-sharp)) into the `.gsd` compressed format for faster loading and smaller file sizes.

## Requirements

- Unreal Engine 5.6
- Windows (DirectX 12)
