"""
import_underground_assets.py
============================

Uveze teksture blokova i predmeta podzemlja (Docs/PLAN_Underground.md) iz
klijentskog jara lokalne Minecraft Java instalacije
(`%APPDATA%/.minecraft/versions/<verzija>/<verzija>.jar`, najnovija verzija
koja ima jar):

  1. PNG-ove izvuce u `Saved/MinecraftTextures/Underground/`
  2. teksture blokova uveze u /Game/Blocks/Textures kao T_<Blok>_Top/_Side/_Bottom
     (build_block_materials.py trazi sve tri; vecina blokova je sa svih strana
     ista, Deepslate ima zasebnu gornju i donju)
  3. sprite predmete (Coal, Raw Iron, Diamond...) uveze u /Game/Items/Textures
     kao T_Item_<Predmet>
  4. pokrene build_block_materials.py (MI_* po bloku iz Blocks.json) i
     generate_item_sprites.py (ikone blok-predmeta)

Pokretanje iz editora:   Tools > MinecraftClone > Import Underground Blocks

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_underground_assets.py"

Projekt se ne distribuira (ucenje), pa se originalne teksture koriste
direktno; `Saved/` je u .gitignore. Skripta je idempotentna.
"""

import os
import re
import zipfile

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MINECRAFT_VERSIONS_DIR = os.path.join(os.environ.get("APPDATA", ""), ".minecraft", "versions")
OUTPUT_DIR = os.path.join(PROJECT_DIR, "Saved", "MinecraftTextures", "Underground")

BLOCK_TEXTURE_DIR = "/Game/Blocks/Textures"
ITEM_TEXTURE_DIR = "/Game/Items/Textures"

# Nas blok -> (bocna tekstura, gornja/donja tekstura) u assets/minecraft/textures/block
BLOCKS = {
    "Cobblestone": ("cobblestone", "cobblestone"),
    "Deepslate": ("deepslate", "deepslate_top"),
    "CobbledDeepslate": ("cobbled_deepslate", "cobbled_deepslate"),
    "Bedrock": ("bedrock", "bedrock"),
    "Granite": ("granite", "granite"),
    "Diorite": ("diorite", "diorite"),
    "Tuff": ("tuff", "tuff"),
}
for _ore in ("coal", "iron", "copper", "gold", "redstone", "lapis", "diamond", "emerald"):
    _name = _ore.capitalize() + "Ore"
    BLOCKS[_name] = (_ore + "_ore", _ore + "_ore")
    BLOCKS["Deepslate" + _name] = ("deepslate_" + _ore + "_ore", "deepslate_" + _ore + "_ore")

# Nas predmet -> tekstura u assets/minecraft/textures/item
ITEMS = {
    "Flint": "flint",
    "Coal": "coal",
    "RawIron": "raw_iron",
    "RawCopper": "raw_copper",
    "RawGold": "raw_gold",
    "Redstone": "redstone",
    "LapisLazuli": "lapis_lazuli",
    "Diamond": "diamond",
    "Emerald": "emerald",
    # Krampe i poluge od kojih se rade (drvena krampa ima vlastitu sliku)
    "StonePickaxe": "stone_pickaxe",
    "CopperPickaxe": "copper_pickaxe",
    "IronPickaxe": "iron_pickaxe",
    "GoldenPickaxe": "golden_pickaxe",
    "DiamondPickaxe": "diamond_pickaxe",
    "CopperIngot": "copper_ingot",
    "IronIngot": "iron_ingot",
    "GoldIngot": "gold_ingot",
}

EAL = unreal.EditorAssetLibrary

_failures = []


def _fail(message):
    _failures.append(message)
    unreal.log_error("[Underground] " + message)


def _info(message):
    unreal.log("[Underground] " + message)


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


def extract(jar, entry):
    """Izvuci jedan PNG iz jara u OUTPUT_DIR; vrati putanju ili None."""
    target = os.path.join(OUTPUT_DIR, entry.replace("/", "_"))
    try:
        data = jar.read("assets/minecraft/textures/" + entry)
    except KeyError:
        _fail("u jaru nema assets/minecraft/textures/" + entry)
        return None
    with open(target, "wb") as handle:
        handle.write(data)
    return target


def import_task(source, destination_path, destination_name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", destination_path)
    task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    return task


def main():
    jar_path = find_client_jar()
    if jar_path is None:
        _fail("nema Minecraft jara u {0}".format(MINECRAFT_VERSIONS_DIR))
        return
    _info("jar: {0}".format(jar_path))
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    tasks = []
    expected = []
    with zipfile.ZipFile(jar_path) as jar:
        for block, (side, top) in sorted(BLOCKS.items()):
            side_png = extract(jar, "block/{0}.png".format(side))
            top_png = extract(jar, "block/{0}.png".format(top))
            if side_png is None or top_png is None:
                continue
            for face, png in (("Top", top_png), ("Side", side_png), ("Bottom", top_png)):
                name = "T_{0}_{1}".format(block, face)
                tasks.append(import_task(png, BLOCK_TEXTURE_DIR, name))
                expected.append("{0}/{1}".format(BLOCK_TEXTURE_DIR, name))
        for item, texture in sorted(ITEMS.items()):
            png = extract(jar, "item/{0}.png".format(texture))
            if png is None:
                continue
            name = "T_Item_{0}".format(item)
            tasks.append(import_task(png, ITEM_TEXTURE_DIR, name))
            expected.append("{0}/{1}".format(ITEM_TEXTURE_DIR, name))

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    imported = [path for path in expected if EAL.does_asset_exist(path)]
    for path in expected:
        if path not in imported:
            _fail("uvoz nije uspio: {0}".format(path))
    _info("uvezeno {0} od {1} tekstura".format(len(imported), len(expected)))

    # Pixel-art postavke tekstura i MI_* po bloku, pa ikone blok-predmeta
    scripts_dir = os.path.join(PROJECT_DIR, "Scripts")
    for script in ("build_block_materials.py", "generate_item_sprites.py"):
        with open(os.path.join(scripts_dir, script), "r", encoding="utf-8") as handle:
            exec(compile(handle.read(), script, "exec"), {"__name__": "__main__"})

    if _failures:
        for message in _failures:
            unreal.log_error("[Underground] NEUSPJEH: " + message)
    else:
        _info("=== gotovo bez gresaka ===")


main()
