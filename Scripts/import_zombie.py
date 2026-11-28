"""
import_zombie.py
================

Napravi Minecraft zombija kao assete u editoru (Docs/PLAN_Zombie.md):

1. izvuce entity/zombie/zombie.png (64x64) iz klijentskog jara lokalne
   Minecraft Java instalacije u Saved/MinecraftTextures/,
2. pokrene Blender headless sa Scripts/build_zombie_blender.py, koji izgradi
   mesh, kostur i animacije u Saved/MinecraftModels/Zombie/ (tamo je i
   Zombie.blend za pregled),
3. uveze T_Zombie, izgradi M_Zombie, uveze SK_Zombie (+ kostur) i animacije
   A_Zombie_Idle / IdleAggressive / Walk / Attack / Death u /Game/Blueprints/MOBS/Zombie.

Pokretanje iz editora:   Tools > MinecraftClone > Import Zombie

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_zombie.py"

M_Zombie odgovara Minecraftovom entityCutoutNoCull: maskiran, dvostran.
Parametar HurtAmount (0..1) postavlja kod dok je stvorenje ozlijedeno.
Minecraft tada mijesa boju s crvenom: mix(crvena, boja, 0.698) iz
OverlayTexture (0xB2FF0000) u prikaznom prostoru, pa materijal boju prebaci
u gamma prostor, pomijesa je i vrati u linearni.

Projekt se ne distribuira (ucenje), pa se originalna tekstura koristi
direktno; `Saved/` je u .gitignore.
"""

import glob
import os
import re
import subprocess
import zipfile

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
MINECRAFT_VERSIONS_DIR = os.path.join(os.environ.get("APPDATA", ""), ".minecraft", "versions")
TEXTURE_OUTPUT_DIR = os.path.join(PROJECT_DIR, "Saved", "MinecraftTextures")
MODEL_OUTPUT_DIR = os.path.join(PROJECT_DIR, "Saved", "MinecraftModels", "Zombie")
BLENDER_SCRIPT = os.path.join(PROJECT_DIR, "Scripts", "build_zombie_blender.py")
BLENDER_GLOB = r"C:\Program Files\Blender Foundation\*\blender.exe"

TEXTURE_ENTRY = "assets/minecraft/textures/entity/zombie/zombie.png"

ZOMBIE_DIR = "/Game/Blueprints/MOBS/Zombie"
TEXTURE_NAME = "T_Zombie"
MATERIAL_NAME = "M_Zombie"
MESH_NAME = "SK_Zombie"
ANIMATIONS = ["Idle", "IdleAggressive", "Walk", "Attack", "Death"]
ANIM_FPS = 60

TEXTURE_PARAM = "Texture"
HURT_PARAM = "HurtAmount"
HURT_MIX = 1.0 - 0xB2 / 255.0   # udio crvene: 1 - alfa iz OverlayTexture
DISPLAY_GAMMA = 2.2
DEFAULT_ROUGHNESS = 0.9         # isto kao blokovi (build_block_materials.py)

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

_failures = []


def _fail(message):
    _failures.append(message)
    unreal.log_error("[ZOMBIE] " + message)


def _info(message):
    unreal.log("[ZOMBIE] " + message)


def _expr(material, expression_class, x, y):
    return MEL.create_material_expression(material, expression_class, x, y)


def _connect(src, src_output, dst, dst_input):
    ok = MEL.connect_material_expressions(src, src_output, dst, dst_input)
    if not ok:
        _fail("spoj odbijen: {0}.'{1}' -> {2}.'{3}'".format(
            src.get_class().get_name(), src_output,
            dst.get_class().get_name(), dst_input))
    return ok


def _connect_property(src, src_output, prop):
    ok = MEL.connect_material_property(src, src_output, prop)
    if not ok:
        _fail("spoj na {0} odbijen".format(prop))
    return ok


def _asset_path(name):
    return "{0}/{1}.{1}".format(ZOMBIE_DIR, name)


def _load(name):
    path = _asset_path(name)
    return EAL.load_asset(path) if EAL.does_asset_exist(path) else None


# ---------------------------------------------------------------------------
# 1. Tekstura iz jara
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


def extract_texture():
    jar_path = find_client_jar()
    if jar_path is None:
        _fail("nema Minecraft jara u {0}".format(MINECRAFT_VERSIONS_DIR))
        return None
    _info("jar: {0}".format(jar_path))
    os.makedirs(TEXTURE_OUTPUT_DIR, exist_ok=True)
    target = os.path.join(TEXTURE_OUTPUT_DIR, "zombie.png")
    with zipfile.ZipFile(jar_path) as jar:
        try:
            data = jar.read(TEXTURE_ENTRY)
        except KeyError:
            _fail("u jaru nema {0}".format(TEXTURE_ENTRY))
            return None
    with open(target, "wb") as handle:
        handle.write(data)
    return target


# ---------------------------------------------------------------------------
# 2. Blender
# ---------------------------------------------------------------------------

def find_blender():
    candidates = sorted(glob.glob(BLENDER_GLOB), key=_version_key, reverse=True)
    return candidates[0] if candidates else None


def run_blender(texture_path):
    blender = find_blender()
    if blender is None:
        _fail("Blender nije pronaden ({0})".format(BLENDER_GLOB))
        return False
    _info("blender: {0}".format(blender))
    command = [blender, "-b", "--factory-startup", "-P", BLENDER_SCRIPT,
               "--", texture_path, MODEL_OUTPUT_DIR]
    result = subprocess.run(command, capture_output=True, text=True)
    for line in result.stdout.splitlines():
        if line.startswith("[ZOMBIE]"):
            _info("blender: " + line[len("[ZOMBIE] "):])
    if result.returncode != 0 or "[ZOMBIE] GOTOVO" not in result.stdout:
        _fail("Blender je pao (izlaz {0}): {1}".format(
            result.returncode, (result.stderr or result.stdout)[-2000:]))
        return False
    return True


# ---------------------------------------------------------------------------
# 3. Uvoz u Unreal
# ---------------------------------------------------------------------------

def _import_task(filename, destination_name, options):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", ZOMBIE_DIR)
    if destination_name:
        task.set_editor_property("destination_name", destination_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return task


def import_texture(source):
    _import_task(source, TEXTURE_NAME, None)
    texture = _load(TEXTURE_NAME)
    if not isinstance(texture, unreal.Texture2D):
        _fail("uvoz teksture nije uspio: " + _asset_path(TEXTURE_NAME))
        return None
    # Pixel-art: Nearest, bez kompresije i mipmapa (DXT unisti 64x64).
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    texture.set_editor_property("never_stream", True)
    EAL.save_loaded_asset(texture, only_if_is_dirty=False)
    _info("tekstura {0}: {1}x{2}".format(
        _asset_path(TEXTURE_NAME), texture.blueprint_get_size_x(), texture.blueprint_get_size_y()))
    return texture


def build_material(texture):
    """
    Graf:

        Texture.RGB ^ (1/2.2)                       = boja u prikaznom prostoru
        Lerp(boja, (1,0,0), HurtAmount x 0.302) ^ 2.2 -> Base Color
        Texture.A                                   -> Opacity Mask
        Roughness 0.9
    """
    material = _load(MATERIAL_NAME)
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MATERIAL_NAME, ZOMBIE_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati " + _asset_path(MATERIAL_NAME))
            return None
        _info("kreiran " + _asset_path(MATERIAL_NAME))
    else:
        MEL.delete_all_material_expressions(material)
        _info("postojeci {0} - graf ociscen i gradi se ponovno".format(_asset_path(MATERIAL_NAME)))

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_skeletal_mesh", True)

    sampler = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -1100, 0)
    sampler.set_editor_property("parameter_name", TEXTURE_PARAM)
    sampler.set_editor_property("texture", texture)

    to_display = _expr(material, unreal.MaterialExpressionPower, -800, -100)
    to_display.set_editor_property("const_exponent", 1.0 / DISPLAY_GAMMA)
    _connect(sampler, "RGB", to_display, "Base")

    red = _expr(material, unreal.MaterialExpressionConstant3Vector, -800, 50)
    red.set_editor_property("constant", unreal.LinearColor(1.0, 0.0, 0.0, 1.0))

    hurt = _expr(material, unreal.MaterialExpressionScalarParameter, -1100, 250)
    hurt.set_editor_property("parameter_name", HURT_PARAM)
    hurt.set_editor_property("default_value", 0.0)

    hurt_mix = _expr(material, unreal.MaterialExpressionMultiply, -800, 250)
    hurt_mix.set_editor_property("const_b", HURT_MIX)
    _connect(hurt, "", hurt_mix, "A")

    lerp = _expr(material, unreal.MaterialExpressionLinearInterpolate, -550, 0)
    _connect(to_display, "", lerp, "A")
    _connect(red, "", lerp, "B")
    _connect(hurt_mix, "", lerp, "Alpha")

    to_linear = _expr(material, unreal.MaterialExpressionPower, -300, 0)
    to_linear.set_editor_property("const_exponent", DISPLAY_GAMMA)
    _connect(lerp, "", to_linear, "Base")
    _connect_property(to_linear, "", unreal.MaterialProperty.MP_BASE_COLOR)

    _connect_property(sampler, "A", unreal.MaterialProperty.MP_OPACITY_MASK)

    roughness = _expr(material, unreal.MaterialExpressionConstant, -300, 200)
    roughness.set_editor_property("r", DEFAULT_ROUGHNESS)
    _connect_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def import_mesh(material):
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("create_physics_asset", False)
    mesh_data = options.get_editor_property("skeletal_mesh_import_data")
    mesh_data.set_editor_property("import_uniform_scale", 1.0)
    mesh_data.set_editor_property("use_t0_as_ref_pose", False)

    _import_task(os.path.join(MODEL_OUTPUT_DIR, MESH_NAME + ".fbx"), None, options)
    mesh = _load(MESH_NAME)
    if not isinstance(mesh, unreal.SkeletalMesh):
        _fail("uvoz mesha nije uspio: " + _asset_path(MESH_NAME))
        return None

    materials = mesh.get_editor_property("materials")
    if len(materials) != 1:
        _fail("{0} ima {1} material slotova, ocekivan 1".format(MESH_NAME, len(materials)))
    for slot in materials:
        slot.set_editor_property("material_interface", material)
    mesh.set_editor_property("materials", materials)
    EAL.save_loaded_asset(mesh, only_if_is_dirty=False)

    bounds = mesh.get_bounds()
    skeleton = mesh.get_editor_property("skeleton")
    _info("mesh {0}: skeleton {1}, bounds origin {2}, extent {3}".format(
        _asset_path(MESH_NAME), skeleton.get_path_name() if skeleton else None,
        bounds.origin, bounds.box_extent))
    return mesh


def import_animation(name, skeleton):
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", False)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    options.set_editor_property("import_animations", True)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("skeleton", skeleton)
    anim_data = options.get_editor_property("anim_sequence_import_data")
    anim_data.set_editor_property(
        "animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    anim_data.set_editor_property("use_default_sample_rate", False)
    anim_data.set_editor_property("custom_sample_rate", ANIM_FPS)
    anim_data.set_editor_property("import_bone_tracks", True)
    anim_data.set_editor_property("remove_redundant_keys", False)

    asset_name = "A_Zombie_" + name
    _import_task(os.path.join(MODEL_OUTPUT_DIR, asset_name + ".fbx"), asset_name, options)
    anim = _load(asset_name)
    if not isinstance(anim, unreal.AnimSequence):
        _fail("uvoz animacije nije uspio: " + _asset_path(asset_name))
        return None
    _info("animacija {0}: {1:.3f} s".format(_asset_path(asset_name), anim.get_play_length()))
    return anim


def log_bone(anim, bone, frame):
    """Kontrolna tocka za usporedbu s Blender pregledom."""
    try:
        pose = unreal.AnimationLibrary.get_bone_pose_for_frame(anim, bone, frame, False)
        _info("  {0} {1} frame {2}: rot {3} loc {4}".format(
            anim.get_name(), bone, frame, pose.rotation.rotator(), pose.translation))
    except Exception as error:
        _info("  {0} {1}: poza nedostupna ({2})".format(anim.get_name(), bone, error))


def main():
    _failures[:] = []
    _info("=== pocetak ===")

    if not EAL.does_directory_exist(ZOMBIE_DIR):
        EAL.make_directory(ZOMBIE_DIR)

    texture_path = extract_texture()
    built = run_blender(texture_path) if texture_path else False
    texture = import_texture(texture_path) if built else None
    material = build_material(texture) if texture else None
    mesh = import_mesh(material) if material else None
    skeleton = mesh.get_editor_property("skeleton") if mesh else None

    animations = {}
    if skeleton is not None:
        for name in ANIMATIONS:
            anim = import_animation(name, skeleton)
            if anim is not None:
                animations[name] = anim

    if "Walk" in animations:
        log_bone(animations["Walk"], "right_leg", 0)
    if "IdleAggressive" in animations:
        log_bone(animations["IdleAggressive"], "right_arm", 0)
    if "Death" in animations:
        log_bone(animations["Death"], "root", 59)

    _info("=== gotovo: mesh {0}, {1}/{2} animacija, {3} gresaka ===".format(
        "OK" if mesh else "NEUSPJEH", len(animations), len(ANIMATIONS), len(_failures)))
    for message in _failures:
        unreal.log_error("[ZOMBIE] NEUSPJEH: " + message)


main()

# M_Zombie je izgraden iz nule, pa mu svjetlo blokova (baklje) treba dodati ponovno
import os as _os
_entity_light_script = _os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts", "build_entity_light.py")
with open(_entity_light_script, "r", encoding="utf-8") as _handle:
    exec(compile(_handle.read(), "build_entity_light.py", "exec"), {"__name__": "__main__"})
