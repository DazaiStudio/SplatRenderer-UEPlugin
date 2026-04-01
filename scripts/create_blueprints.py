"""Create SplatRenderer Blueprint assets in UE Editor.
Run via: UE Editor -> Tools -> Execute Python Script, or Console: py "path/to/create_blueprints.py"
Or commandlet: UnrealEditor-Cmd.exe project.uproject -run=pythonscript -script="path/to/create_blueprints.py"
"""
import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

def create_bp(class_path, bp_name, folder="/SplatRenderer/Blueprints"):
    """Create a Blueprint asset based on a C++ class."""
    full_path = f"{folder}/{bp_name}"

    # Delete if already exists (force recreate)
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.EditorAssetLibrary.delete_asset(full_path)
        unreal.log(f"[SplatRenderer] Deleted existing {bp_name}")

    parent_class = unreal.load_class(None, class_path)
    if not parent_class:
        unreal.log_error(f"[SplatRenderer] Failed to load class: {class_path}")
        return

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)

    bp = asset_tools.create_asset(bp_name, folder, unreal.Blueprint, factory)
    if bp:
        unreal.EditorAssetLibrary.save_asset(full_path)
        unreal.log(f"[SplatRenderer] Created {full_path} (parent: {parent_class.get_name()})")
    else:
        unreal.log_error(f"[SplatRenderer] Failed to create {bp_name}")

# Create BP_3DGS (static 3DGS PLY actor)
create_bp("/Script/SplatRenderer.CSGaussianActor", "BP_3DGS")

# Create BP_4DGS (4DGS sequence actor)
create_bp("/Script/SplatRenderer.CSGaussianSequenceActor", "BP_4DGS")

unreal.log("[SplatRenderer] Blueprint creation complete")
