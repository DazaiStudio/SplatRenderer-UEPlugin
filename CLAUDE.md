# SplatRenderer Plugin Development

## Project Structure
- **Local path:** `D:\Github\splatrenderer\` (renamed from csgs-release)
- **Source code:** `SplatRenderer/Source/CSGaussianRenderer/` — active development here
- **Shaders:** `SplatRenderer/Shaders/Private/`
- **Binaries:** `SplatRenderer/Binaries/Win64/` (precompiled DLL for release)
- **Blueprints:** `SplatRenderer/Content/Blueprints/`
- **Notes/plans:** `notes/`
- **Reference plugins:** `ref/` (PICOSplat, Postshot, Volinga)

## UE Plugin
- UE loads the plugin via symlink from `D:\Unreal Projects\FourDGS - V2\Plugins\SplatRenderer\` → this folder's `SplatRenderer/`
- Edit source here, UE compiles from here via the symlink

## Git History
- **This repo** (`SplatRenderer-UEPlugin`, public): Release only — DLL, shaders, content, docs. Source/ is gitignored.
- **Old dev history** (`csgs.git`): at `D:\Unreal Projects\FourDGS - V2\Plugins\CSGaussianRenderer\.git` — check there for commit history before this consolidation

## Build
- Build script: `cmd.exe //C "C:\Users\tommy\AppData\Local\Temp\build_csg3.bat"`
- UE Editor must be closed for linking; shader-only changes just need editor restart
- NEVER run two builds simultaneously

## Key Rules
- Do NOT push Source/ to this public repo
- Module name is still `CSGaussianRenderer` (rename to SplatRenderer planned)
- User communicates in Traditional Chinese
- Analyze before building — wait for permission before running builds
