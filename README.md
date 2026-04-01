<p align="center">
  <img src="docs/logo.png" alt="Splat Renderer" width="200">
  <h1 align="center">Splat Renderer</h1>
  <p align="center">3D/4D Gaussian Splatting renderer plugin for Unreal Engine 5.5+</p>
  <p align="center">
    <img src="https://img.shields.io/badge/Unreal%20Engine-5.5+-blue" alt="UE">
    <img src="https://img.shields.io/badge/Platform-Windows-lightgrey" alt="Platform">
    <img src="https://img.shields.io/badge/License-Apache%202.0-blue" alt="License">
    <a href="https://github.com/DazaiStudio/SplatRenderer-UEPlugin/releases"><img src="https://img.shields.io/badge/Release-v1.0.1-green" alt="Release"></a>
  </p>
</p>

<p align="center">
  <a href="https://youtu.be/eeR5QMQA7co">
    <img src="docs/demo-preview.gif" alt="Demo Video" width="700">
  </a>
  <br>
  <a href="https://youtu.be/eeR5QMQA7co"><b>Watch Full Demo on YouTube</b></a>
</p>

---

## Table of Contents

- [Features](#features)
- [Getting Started](#getting-started)
- [Support](#support)
- [License](#license)

---

## Features

- **3DGS** — Real-time rendering of static Gaussian Splats from `.ply` files
- **4DGS** — Playback of 4D Gaussian Splat sequences (`.gsd` format)
- **Crop Volume** — 3D crop box with draggable editor widget
- **Rendering Controls** — Brightness, splat scale
- **Audio** — WAV playback synced to sequence

---

## Getting Started

### 1. Download

Download the latest release from the [**Releases**](https://github.com/DazaiStudio/SplatRenderer-UEPlugin/releases) page.

Extract `SplatRenderer` into your project's `Plugins/` directory.

<img src="docs/install-folder.png" alt="Install Folder" width="500">

### 2. Open Your Project

Launch your project in Unreal Engine. The plugin will be loaded automatically.

Verify in **Edit > Plugins** by searching for **Splat Renderer**.

<img src="docs/plugin-browser.png" alt="Plugin Browser" width="500">

### 3. Add to Level

Open the **Content Browser** and navigate to **Plugins > Splat Renderer Content > Blueprints**.

<img src="docs/content-browser.png" alt="Content Browser" width="600">

Drag **BP_3DGS** or **BP_4DGS** into your level and configure in the **Details** panel.

See the [latest release notes](https://github.com/DazaiStudio/SplatRenderer-UEPlugin/releases/latest) for detailed usage.

> **4DGS:** Use [**4DGS Converter**](https://github.com/DazaiStudio/4dgs-converter) to convert 4DGS training output into `.gsd` files.

---

## Support

For bug reports and feature requests, please use the [GitHub Issues](https://github.com/DazaiStudio/SplatRenderer-UEPlugin/issues) page.

For general questions and discussions, please use the [GitHub Discussions](https://github.com/DazaiStudio/SplatRenderer-UEPlugin/discussions) page.

<a href="https://buymeacoffee.com/dazaistudio"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" width="200"></a>

[Website](https://dazaistudio.com) | [GitHub](https://github.com/DazaiStudio) | [LinkedIn](https://www.linkedin.com/in/dazai-chen-280186183/)

---

## License

This project is licensed under the [Apache License 2.0](LICENSE).
