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
  4. cestice: `textures/particle/flame.png` uveze kao T_Particle_Flame, a osam
     slicica dima (generic_7 ... generic_0, redom iz particles/smoke.json)
     slozi u jednu traku 64x8 i uveze kao T_Particle_Smoke; izgradi
     M_BlockParticle te MI_ParticleFlame i MI_ParticleSmoke u /Game/Blocks/Particles

Model baklje nije asset: C++ ga gradi iz polja "model" u Blocks.json.

Pokretanje iz editora:   Tools > MinecraftClone > Import Torch

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_torch_assets.py"

Projekt se ne distribuira (ucenje), pa se originalne teksture koriste
direktno; `Saved/` je u .gitignore. Skripta je idempotentna.
"""

import os
import re
import struct
import zipfile
import zlib

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

# Cestice (AVoxelWorld, VoxelWorldParticles.cpp). Izvan /Game/Blocks/Textures
# jer build_block_materials.py tamo svim teksturama ukljucuje mipove, a traka
# slicica s mipovima curi iz jedne slicice u drugu.
PARTICLE_DIR = "/Game/Blocks/Particles"
PARTICLE_MATERIAL = "M_BlockParticle"
SMOKE_FRAMES = ["generic_{0}".format(index) for index in range(7, -1, -1)]

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

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


def decode_png(data):
    """Vrati (sirina, visina, RGBA bajtovi) za 8-bitni PNG bez preplitanja (siva+alfa ili RGBA)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("nije PNG")
    position = 8
    width = height = channels = 0
    compressed = b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            channels = {4: 2, 6: 4}.get(color_type, 0)
            if depth != 8 or channels == 0 or interlace != 0:
                raise ValueError("nepodrzan PNG (dubina {0}, tip {1})".format(depth, color_type))
        elif kind == b"IDAT":
            compressed += body

    raw = zlib.decompress(compressed)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for index in range(stride):
            left = line[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_type == 1:
                line[index] = (line[index] + left) & 255
            elif filter_type == 2:
                line[index] = (line[index] + up) & 255
            elif filter_type == 3:
                line[index] = (line[index] + ((left + up) >> 1)) & 255
            elif filter_type == 4:
                estimate = left + up - up_left
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - up_left))
                nearest = (left, up, up_left)[distances.index(min(distances))]
                line[index] = (line[index] + nearest) & 255
        rows.append(line)
        previous = line

    rgba = bytearray()
    for line in rows:
        for x in range(width):
            pixel = line[x * channels:(x + 1) * channels]
            rgba += bytes((pixel[0], pixel[0], pixel[0], pixel[1])) if channels == 2 else bytes(pixel)
    return width, height, bytes(rgba)


def encode_png(width, height, rgba):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def _expr(material, expression_class, x, y):
    return MEL.create_material_expression(material, expression_class, x, y)


def _connect(src, src_output, dst, dst_input):
    if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
        _fail("spoj odbijen: {0}.'{1}' -> {2}.'{3}'".format(
            src.get_class().get_name(), src_output, dst.get_class().get_name(), dst_input))


def _load(package_path):
    return EAL.load_asset(package_path) if EAL.does_asset_exist(package_path) else None


def build_particle_material():
    """
    Graf (cestica je kvadrat okrenut kameri, bez sjencanja):

        U = (TexCoord.U + custom data 0 [slicica]) / "Frames"
        TextureSampleParameter2D "Texture" (U, TexCoord.V)
            RGB * custom data 1-3 [boja] -> Emissive
            A                            -> Opacity Mask (prag 0.1, kao Minecraft)
    """
    material = _load("{0}/{1}.{1}".format(PARTICLE_DIR, PARTICLE_MATERIAL))
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            PARTICLE_MATERIAL, PARTICLE_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati {0}/{1}".format(PARTICLE_DIR, PARTICLE_MATERIAL))
            return None
    else:
        MEL.delete_all_material_expressions(material)

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.1)

    texcoord = _expr(material, unreal.MaterialExpressionTextureCoordinate, -1300, 0)

    def channel(red, y):
        mask = _expr(material, unreal.MaterialExpressionComponentMask, -1100, y)
        mask.set_editor_property("r", red)
        mask.set_editor_property("g", not red)
        mask.set_editor_property("b", False)
        mask.set_editor_property("a", False)
        _connect(texcoord, "", mask, "")
        return mask

    u = channel(True, -50)
    v = channel(False, 100)

    frame = _expr(material, unreal.MaterialExpressionPerInstanceCustomData, -1100, -250)
    frame.set_editor_property("data_index", 0)
    frame.set_editor_property("const_default_value", 0.0)

    frames = _expr(material, unreal.MaterialExpressionScalarParameter, -1100, -450)
    frames.set_editor_property("parameter_name", "Frames")
    frames.set_editor_property("default_value", 1.0)

    shifted = _expr(material, unreal.MaterialExpressionAdd, -900, -150)
    _connect(u, "", shifted, "A")
    _connect(frame, "", shifted, "B")
    scaled = _expr(material, unreal.MaterialExpressionDivide, -750, -150)
    _connect(shifted, "", scaled, "A")
    _connect(frames, "", scaled, "B")
    uv = _expr(material, unreal.MaterialExpressionAppendVector, -600, 0)
    _connect(scaled, "", uv, "A")
    _connect(v, "", uv, "B")

    sampler = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    sampler.set_editor_property("parameter_name", "Texture")
    fallback = _load("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")
    if fallback is not None:
        sampler.set_editor_property("texture", fallback)
    _connect(uv, "", sampler, "UVs")

    color = _expr(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, -400, 300)
    color.set_editor_property("data_index", 1)
    color.set_editor_property("const_default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    tinted = _expr(material, unreal.MaterialExpressionMultiply, -150, 100)
    _connect(sampler, "", tinted, "A")
    _connect(color, "", tinted, "B")

    if not MEL.connect_material_property(tinted, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        _fail("spoj na Emissive odbijen")
    if not MEL.connect_material_property(sampler, "A", unreal.MaterialProperty.MP_OPACITY_MASK):
        _fail("spoj na Opacity Mask odbijen")

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def import_particles(jar):
    """Teksture i materijali plamena i dima."""
    if not EAL.does_directory_exist(PARTICLE_DIR):
        EAL.make_directory(PARTICLE_DIR)

    sources = {}
    try:
        flame_png = os.path.join(OUTPUT_DIR, "particle_flame.png")
        with open(flame_png, "wb") as handle:
            handle.write(jar.read("assets/minecraft/textures/particle/flame.png"))
        sources["T_Particle_Flame"] = flame_png

        # Traka: slicice jedna do druge, redom kojim ih Minecraft izmjenjuje
        frames = [decode_png(jar.read("assets/minecraft/textures/particle/{0}.png".format(name)))
                  for name in SMOKE_FRAMES]
        width, height = frames[0][0], frames[0][1]
        strip = bytearray()
        for y in range(height):
            for _, _, rgba in frames:
                strip += rgba[y * width * 4:(y + 1) * width * 4]
        smoke_png = os.path.join(OUTPUT_DIR, "particle_smoke.png")
        with open(smoke_png, "wb") as handle:
            handle.write(encode_png(width * len(frames), height, bytes(strip)))
        sources["T_Particle_Smoke"] = smoke_png
    except (KeyError, ValueError) as error:
        _fail("cestice: {0}".format(error))
        return

    tasks = []
    for name, png in sources.items():
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", png)
        task.set_editor_property("destination_path", PARTICLE_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    # Pixel-art: bez filtriranja, bez mipova i bez kompresije (8x8 piksela)
    textures = {}
    for name in sources:
        texture = _load("{0}/{1}.{1}".format(PARTICLE_DIR, name))
        if texture is None:
            _fail("uvoz nije uspio: {0}/{1}".format(PARTICLE_DIR, name))
            continue
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("never_stream", True)
        EAL.save_loaded_asset(texture, only_if_is_dirty=False)
        textures[name] = texture

    material = build_particle_material()
    if material is None:
        return
    for instance_name, texture_name, frame_count in (
            ("MI_ParticleFlame", "T_Particle_Flame", 1.0),
            ("MI_ParticleSmoke", "T_Particle_Smoke", float(len(SMOKE_FRAMES)))):
        if texture_name not in textures:
            continue
        instance = _load("{0}/{1}.{1}".format(PARTICLE_DIR, instance_name))
        if instance is None:
            instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                instance_name, PARTICLE_DIR, unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew())
            if instance is None:
                _fail("ne mogu kreirati {0}/{1}".format(PARTICLE_DIR, instance_name))
                continue
        MEL.set_material_instance_parent(instance, material)
        MEL.set_material_instance_texture_parameter_value(instance, "Texture", textures[texture_name])
        MEL.set_material_instance_scalar_parameter_value(instance, "Frames", frame_count)
        EAL.save_loaded_asset(instance, only_if_is_dirty=False)
        _info("{0}  <-  {1}, slicica {2}".format(instance_name, texture_name, int(frame_count)))


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

    with zipfile.ZipFile(jar_path) as jar:
        import_particles(jar)

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
