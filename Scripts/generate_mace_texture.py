"""
generate_mace_texture.py
========================

Nacrta privremeni 16x16 sprite drške buzdovana (Mace) i uveze ga kao
/Game/Items/Textures/T_Item_Mace s istim pixel-art postavkama koje
build_block_materials.py daje rucno nacrtanim item spriteovima
(Docs/PLAN_Mace.md).

Drška ide dijagonalno od donjeg lijevog kuta (jabuka) do gornjeg desnog
(metalna glava s ušicom). Sredina ušice je piksel (13, 2) - tu UMaceComponent
veze lanac (ChainAnchorLocal). Kad stigne prava tekstura, ušica mora ostati na
istom pikselu ili se ChainAnchorLocal mora pomaknuti.

Pokretanje headless:  UnrealEditor-Cmd.exe MinecraftClone.uproject
                      -run=pythonscript -script="Scripts/generate_mace_texture.py"
"""

import os
import struct
import zlib

import unreal

EAL = unreal.EditorAssetLibrary
TEXTURE_DIR = "/Game/Items/Textures"
TEXTURE_NAME = "T_Item_Mace"
SIZE = 16

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
PNG_PATH = os.path.join(PROJECT_DIR, "Saved", "MaceTexture", TEXTURE_NAME + ".png")

OUTLINE = (59, 42, 20, 255)
WOOD = (139, 90, 43, 255)
WOOD_LIGHT = (168, 116, 58, 255)
METAL = (154, 154, 160, 255)
METAL_DARK = (78, 78, 84, 255)
METAL_LIGHT = (214, 214, 220, 255)


def _draw():
    pixels = [[(0, 0, 0, 0)] * SIZE for _ in range(SIZE)]

    def put(col, row, color):
        if 0 <= col < SIZE and 0 <= row < SIZE:
            pixels[row][col] = color

    # Drvena drška: dva piksela siroka dijagonala, obrub s obje strane
    for k in range(10):
        col, row = 3 + k, 12 - k
        put(col - 1, row, OUTLINE)
        put(col, row, WOOD)
        put(col + 1, row, WOOD_LIGHT)
        put(col + 2, row, OUTLINE)

    # Metalna jabuka na dnu
    for col, row in ((1, 13), (2, 13), (1, 14), (2, 14), (3, 13)):
        put(col, row, METAL)
    put(1, 14, METAL_DARK)

    # Metalna glava s ušicom na vrhu
    for row in range(1, 5):
        for col in range(11, 15):
            put(col, row, METAL)
    for col, row in ((11, 1), (14, 4)):
        put(col, row, (0, 0, 0, 0))
    put(12, 2, METAL_LIGHT)
    put(12, 3, METAL_LIGHT)
    put(13, 3, METAL_DARK)
    put(14, 2, METAL_DARK)
    put(13, 2, METAL_DARK)   # ušica: sredina tog piksela je sidro lanca
    return pixels


def _write_png(path, pixels):
    raw = b"".join(
        b"\x00" + b"".join(struct.pack("BBBB", *px) for px in row)
        for row in pixels)

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        handle.write(chunk(b"IEND", b""))


def main():
    _write_png(PNG_PATH, _draw())

    target_path = "{0}/{1}".format(TEXTURE_DIR, TEXTURE_NAME)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", PNG_PATH)
    task.set_editor_property("destination_path", TEXTURE_DIR)
    task.set_editor_property("destination_name", TEXTURE_NAME)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not EAL.does_asset_exist(target_path):
        unreal.log_error("[MACE] import {0} nije uspio".format(PNG_PATH))
        return

    # Iste postavke kao build_block_materials.py za ITEM_TEXTURE_DIR
    texture = EAL.load_asset(target_path)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    texture.set_editor_property("never_stream", True)
    EAL.save_loaded_asset(texture, only_if_is_dirty=False)
    unreal.log("[MACE] {0} uvezen iz {1}".format(target_path, PNG_PATH))


main()
