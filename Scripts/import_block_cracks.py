"""
import_block_cracks.py
======================

Uveze Minecraft pukotine (destroy_stage_0..9) i izgradi materijal kojim ih
UBlockCrackComponent crta preko bloka koji se kopa (Docs/PLAN_BlockDamage.md).

Skripta sama izvuce deset PNG-ova iz klijentskog jara lokalne Minecraft Java
instalacije (`%APPDATA%/.minecraft/versions/<verzija>/<verzija>.jar`, uzima
najnoviju verziju koja ima jar) u `Saved/MinecraftTextures/`, uveze ih u
/Game/Blocks/Crack kao T_DestroyStage_0..9 i izgradi M_BlockCrack.

Pokretanje iz editora:   Tools > MinecraftClone > Import Block Cracks

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_block_cracks.py"

Kako Minecraft mijesa pukotine s blokom (procitano iz 1.21.11 jara):
pipeline "crumbling" ima blend DST_COLOR / SRC_COLOR, sto daje
rezultat = 2 x pukotina x blok. Siva 0.5 ne mijenja blok, tamniji pikseli
ga zatamne, svjetliji posvijetle. Pikseli s alfom ispod 0.1 se odbacuju.
Minecraft to radi u prikaznom (gamma) prostoru, a UE mnozi u linearnom, pa
materijal faktor 2 x pukotina digne na 2.2 da izgled ostane isti.

Projekt se ne distribuira (ucenje), pa se originalne teksture koriste
direktno; `Saved/` je u .gitignore.
"""

import os
import re
import zipfile

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MINECRAFT_VERSIONS_DIR = os.path.join(os.environ.get("APPDATA", ""), ".minecraft", "versions")
OUTPUT_DIR = os.path.join(PROJECT_DIR, "Saved", "MinecraftTextures")

# Namjerno izvan /Game/Blocks/Textures: build_block_materials.py tamo svim
# teksturama postavlja sRGB, a pukotine se citaju kao linearne vrijednosti.
CRACK_DIR = "/Game/Blocks/Crack"
MATERIAL_NAME = "M_BlockCrack"
TEXTURE_FORMAT = "T_DestroyStage_{0}"
NUM_STAGES = 10

TEXTURE_PARAM = "CrackTexture"
OPACITY_PARAM = "CrackOpacity"

# Prikazni prostor -> linearni (vidi docstring)
DISPLAY_GAMMA = 2.2

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

_failures = []


def _fail(message):
    _failures.append(message)
    unreal.log_error("[BlockCracks] " + message)


def _info(message):
    unreal.log("[BlockCracks] " + message)


def _expr(material, expression_class, x, y):
    return MEL.create_material_expression(material, expression_class, x, y)


def _connect(src, src_output, dst, dst_input):
    ok = MEL.connect_material_expressions(src, src_output, dst, dst_input)
    if not ok:
        _fail("spoj odbijen: {0}.'{1}' -> {2}.'{3}'".format(
            src.get_class().get_name(), src_output,
            dst.get_class().get_name(), dst_input))
    return ok


# ---------------------------------------------------------------------------
# 1. Izvlacenje iz jara
# ---------------------------------------------------------------------------

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


def extract_textures():
    jar_path = find_client_jar()
    if jar_path is None:
        _fail("nema Minecraft jara u {0}".format(MINECRAFT_VERSIONS_DIR))
        return []

    _info("jar: {0}".format(jar_path))
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    files = []
    with zipfile.ZipFile(jar_path) as jar:
        for stage in range(NUM_STAGES):
            entry = "assets/minecraft/textures/block/destroy_stage_{0}.png".format(stage)
            target = os.path.join(OUTPUT_DIR, "destroy_stage_{0}.png".format(stage))
            try:
                data = jar.read(entry)
            except KeyError:
                _fail("u jaru nema {0}".format(entry))
                return []
            with open(target, "wb") as handle:
                handle.write(data)
            files.append(target)
    return files


# ---------------------------------------------------------------------------
# 2. Uvoz tekstura
# ---------------------------------------------------------------------------

def import_textures(files):
    tasks = []
    for stage, source in enumerate(files):
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", source)
        task.set_editor_property("destination_path", CRACK_DIR)
        task.set_editor_property("destination_name", TEXTURE_FORMAT.format(stage))
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    textures = []
    for stage in range(NUM_STAGES):
        name = TEXTURE_FORMAT.format(stage)
        path = "{0}/{1}.{1}".format(CRACK_DIR, name)
        texture = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if not isinstance(texture, unreal.Texture2D):
            _fail("uvoz nije uspio: {0}".format(path))
            return []

        # Pixel-art: Nearest i bez kompresije (DXT unisti 16x16). Linearno
        # jer materijal vrijednosti piksela koristi kao faktor mnozenja.
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        texture.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("srgb", False)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        texture.set_editor_property("never_stream", True)
        EAL.save_loaded_asset(texture, only_if_is_dirty=False)
        textures.append(texture)

    _info("uvezeno {0} tekstura u {1}".format(len(textures), CRACK_DIR))
    return textures


# ---------------------------------------------------------------------------
# 3. Materijal
# ---------------------------------------------------------------------------

def build_material(default_texture):
    """
    Graf:

        CrackTexture.R x 2 -> Power(2.2) = Faktor
        CrackTexture.A x CrackOpacity  = Alpha
        Lerp(1, Faktor, Alpha)         -> Emissive

    Modulate blend mnozi scenu s Emissive. Gdje je tekstura prozirna, Lerp
    daje 1 i blok ostaje netaknut.
    """
    package_path = "{0}/{1}".format(CRACK_DIR, MATERIAL_NAME)
    material = EAL.load_asset(package_path) if EAL.does_asset_exist(package_path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MATERIAL_NAME, CRACK_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        MEL.delete_all_material_expressions(material)
        _info("postojeci {0} - graf ociscen i gradi se ponovno".format(package_path))

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", False)

    sampler = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, 0)
    sampler.set_editor_property("parameter_name", TEXTURE_PARAM)
    sampler.set_editor_property("texture", default_texture)
    sampler.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

    doubled = _expr(material, unreal.MaterialExpressionMultiply, -650, -100)
    doubled.set_editor_property("const_b", 2.0)
    _connect(sampler, "R", doubled, "A")

    power = _expr(material, unreal.MaterialExpressionPower, -450, -100)
    power.set_editor_property("const_exponent", DISPLAY_GAMMA)
    _connect(doubled, "", power, "Base")

    opacity = _expr(material, unreal.MaterialExpressionScalarParameter, -900, 300)
    opacity.set_editor_property("parameter_name", OPACITY_PARAM)
    opacity.set_editor_property("default_value", 1.0)

    alpha = _expr(material, unreal.MaterialExpressionMultiply, -650, 200)
    _connect(sampler, "A", alpha, "A")
    _connect(opacity, "", alpha, "B")

    lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, -250, 0)
    lerp.set_editor_property("const_a", 1.0)
    _connect(power, "", lerp, "B")
    _connect(alpha, "", lerp, "Alpha")

    if not MEL.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        _fail("spoj na Emissive odbijen")

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def main():
    _failures[:] = []
    _info("=== pocetak ===")

    if not EAL.does_directory_exist(CRACK_DIR):
        EAL.make_directory(CRACK_DIR)

    files = extract_textures()
    textures = import_textures(files) if files else []
    material = build_material(textures[0]) if textures else None

    _info("=== gotovo: {0} tekstura, materijal {1}, {2} gresaka ===".format(
        len(textures), "OK" if material is not None else "NEUSPJEH", len(_failures)))
    for message in _failures:
        unreal.log_error("[BlockCracks] NEUSPJEH: " + message)


main()
