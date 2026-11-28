"""
import_torch_assets.py
======================

Uveze teksturu baklje (Docs/PLAN_Torches.md) iz klijentskog jara lokalne
Minecraft Java instalacije (`%APPDATA%/.minecraft/versions/<verzija>/<verzija>.jar`,
najnovija verzija koja ima jar):

  1. `textures/block/torch.png` izvuce u `Saved/MinecraftTextures/Torch/`
  2. uveze je u /Game/Blocks/Textures kao T_Torch (tekstura modela bloka) i u
     /Game/Items/Textures kao T_Item_Torch (ikona predmeta; Minecraft za
     predmet koristi istu sliku)
  3. pokrene build_block_materials.py: MPC_BlockLight, svjetlo blokova u
     M_VoxelBlock i M_VoxelBlock_Masked, M_BlockModel i MI_Torch

Model baklje nije asset: C++ ga gradi iz polja "model" u Blocks.json.

Pokretanje iz editora:   Tools > MinecraftClone > Import Torch

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_torch_assets.py"

Projekt se ne distribuira (ucenje), pa se originalne teksture koriste
direktno; `Saved/` je u .gitignore. Skripta je idempotentna.
"""

import os
import re
import zipfile

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MINECRAFT_VERSIONS_DIR = os.path.join(os.environ.get("APPDATA", ""), ".minecraft", "versions")
OUTPUT_DIR = os.path.join(PROJECT_DIR, "Saved", "MinecraftTextures", "Torch")

JAR_ENTRY = "assets/minecraft/textures/block/torch.png"

# (mapa, ime asseta)
TARGETS = (
    ("/Game/Blocks/Textures", "T_Torch"),
    ("/Game/Items/Textures", "T_Item_Torch"),
)

EAL = unreal.EditorAssetLibrary

_failures = []


def _fail(message):
    _failures.append(message)
    unreal.log_error("[Torch] " + message)


def _info(message):
    unreal.log("[Torch] " + message)


def _version_key(name):
    return [int(part) if part.isdigit() else part for part in re.split(r"(\d+)", name)]


def find_client_jar():
    if not os.path.isdir(MINECRAFT_VERSIONS_DIR):
        return None
    for version in sorted(os.listdir(MINECRAFT_VERSIONS_DIR), key=_version_key, reverse=True):
        jar = os.path.join(MINECRAFT_VERSIONS_DIR, version, version + ".jar")
        if os.path.isfile(jar):
            return jar
    return None


def main():
    jar_path = find_client_jar()
    if jar_path is None:
        _fail("nema Minecraft jara u {0}".format(MINECRAFT_VERSIONS_DIR))
        return
    _info("jar: {0}".format(jar_path))
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    png = os.path.join(OUTPUT_DIR, "torch.png")
    with zipfile.ZipFile(jar_path) as jar:
        try:
            data = jar.read(JAR_ENTRY)
        except KeyError:
            _fail("u jaru nema " + JAR_ENTRY)
            return
    with open(png, "wb") as handle:
        handle.write(data)

    tasks = []
    for directory, name in TARGETS:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", png)
        task.set_editor_property("destination_path", directory)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for directory, name in TARGETS:
        if not EAL.does_asset_exist("{0}/{1}".format(directory, name)):
            _fail("uvoz nije uspio: {0}/{1}".format(directory, name))

    # Pixel-art postavke tekstura, svjetlo blokova u materijalima i MI_Torch
    script = os.path.join(PROJECT_DIR, "Scripts", "build_block_materials.py")
    with open(script, "r", encoding="utf-8") as handle:
        exec(compile(handle.read(), "build_block_materials.py", "exec"), {"__name__": "__main__"})

    if _failures:
        for message in _failures:
            unreal.log_error("[Torch] NEUSPJEH: " + message)
    else:
        _info("=== gotovo bez gresaka ===")


main()
