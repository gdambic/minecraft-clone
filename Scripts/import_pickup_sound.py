"""
import_pickup_sound.py
======================

Uveze vanilla zvuk kupljenja itema (`entity.item.pickup` -> `random/pop.ogg`)
iz `Saved/MinecraftSounds/random/pop.ogg` u `/Game/Audio/Items/Pickup_Pop`.
Izvorna datoteka je u Minecraft asset storeu pod hashom
d6ae1c04d0a7376a33d1df12e1b8057cfbab6bc2 (nije u jaru).

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_pickup_sound.py"
"""

import os

import unreal

SOURCE_FILE = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "Saved", "MinecraftSounds", "random", "pop.ogg")

task = unreal.AssetImportTask()
task.set_editor_property("filename", SOURCE_FILE)
task.set_editor_property("destination_path", "/Game/Audio/Items")
task.set_editor_property("destination_name", "Pickup_Pop")
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", True)
task.set_editor_property("save", True)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

if task.get_editor_property("imported_object_paths"):
    unreal.log("[PickupSound] uvezen /Game/Audio/Items/Pickup_Pop")
else:
    unreal.log_warning("[PickupSound] import nije uspio: {0}".format(SOURCE_FILE))
