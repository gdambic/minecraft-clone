"""
build_block_materials.py
========================

Generira materijalni sustav za voxel blokove:

  1. popravi import postavke svih tekstura u /Game/Blocks/Textures
  2. izgradi master materijale M_VoxelBlock i M_VoxelBlock_Masked
  3. izgradi MI_<Blok> instancu po tipu bloka iz pronadenih tekstura

Pokretanje iz editora:   Window > Developer Tools > Output Log > Python
                         exec(open(r"C:/RVS/C++GameDev/MinecraftClone/Scripts/build_block_materials.py").read())

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/build_block_materials.py"

Skripta je idempotentna: postojeci se materijali NE brisu nego im se graf
ocisti i ponovno izgradi, pa reference iz koda i iz MI instanci prezive.
Slobodno je pokreni ponovno svaki put kad dodas ili promijenis teksturu.
"""

import unreal

# ---------------------------------------------------------------------------
# Konfiguracija
# ---------------------------------------------------------------------------

TEXTURE_DIR = "/Game/Blocks/Textures"
MATERIAL_DIR = "/Game/Blocks/Materials"

MASTER_OPAQUE = "M_VoxelBlock"
MASTER_MASKED = "M_VoxelBlock_Masked"

# Mipovi: 16x16 bez mipova daje ostre piksele izbliza, ali u daljini titra
# (aliasing kroz TSR). S mipovima je mirno, a Filter=Nearest i dalje cuva
# pixel-art izgled na bliskim blokovima. Prebaci na False za cisti MC izgled.
GENERATE_MIPS = True

# Konstantna hrapavost - pixel-art blokovi ne trebaju ORM mapu.
DEFAULT_ROUGHNESS = 0.9

# Blokovi za koje se generira MI. Drugi clan: koristi li masked master
# (alpha-cutout, za lisce).
BLOCKS = [
    ("Dirt",          False),
    ("Stone",         False),
    ("Grass",         False),
    ("OakLog",        False),
    ("BirchLog",      False),
    ("OakLeaves",     True),
    ("BirchLeaves",   True),
    ("OakPlanks",     False),
    ("BirchPlanks",   False),
    ("CraftingTable", False),
]

FACES = ("Top", "Side", "Bottom")

# Placeholder dok korisnik ne nacrta teksturu - da se materijal moze kompajlirati.
FALLBACK_TEXTURE = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"


# ---------------------------------------------------------------------------
# Pomocne funkcije
# ---------------------------------------------------------------------------

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

_failures = []


def _fail(message):
    _failures.append(message)
    unreal.log_error("[BlockMaterials] " + message)


def _info(message):
    unreal.log("[BlockMaterials] " + message)


def _expr(material, expression_class, x, y):
    return MEL.create_material_expression(material, expression_class, x, y)


def _connect(src, src_output, dst, dst_input):
    """Spoji dva izraza i prijavi ako UE odbije spoj (najcesce krivo ime pina)."""
    ok = MEL.connect_material_expressions(src, src_output, dst, dst_input)
    if not ok:
        _fail("spoj odbijen: {0}.'{1}' -> {2}.'{3}'".format(
            src.get_class().get_name(), src_output,
            dst.get_class().get_name(), dst_input))
    return ok


def _connect_property(src, src_output, material, material_property):
    ok = MEL.connect_material_property(src, src_output, material_property)
    if not ok:
        _fail("spoj na property odbijen: {0}.'{1}' -> {2}".format(
            src.get_class().get_name(), src_output, material_property))
    return ok


def _load(package_path):
    if not EAL.does_asset_exist(package_path):
        return None
    return EAL.load_asset(package_path)


def _pick_normal_space():
    """
    Odaberi ciljni prostor za Transform nod na vertex normali.

    Instance space je jedini ispravan izbor za ISM: 'Local' je prostor
    KOMPONENTE (cijeli AVoxelWorld), pa bi rotacija pojedine instance ostala
    nevidljiva. Vraca (source_enum, target_enum, ime) ili (None, None, None)
    ako engine ne nudi nijednu opciju - tada ostajemo u world spaceu.
    """
    src_enum = getattr(unreal, "MaterialVectorCoordTransformSource", None)
    dst_enum = getattr(unreal, "MaterialVectorCoordTransform", None)
    if src_enum is None or dst_enum is None:
        return None, None, None

    source = getattr(src_enum, "TRANSFORMSOURCE_WORLD", None)
    if source is None:
        return None, None, None

    for name in ("TRANSFORM_INSTANCE", "TRANSFORM_LOCAL"):
        target = getattr(dst_enum, name, None)
        if target is not None:
            return source, target, name

    return None, None, None


# ---------------------------------------------------------------------------
# 1. Import postavke tekstura
# ---------------------------------------------------------------------------

def fix_texture_import_settings():
    """
    Pixel-art teksture trebaju Nearest filter i nekomprimiranu RGBA.
    DXT/BC kompresija radi u blokovima 4x4 piksela, sto na 16x16 teksturi
    vidljivo unistava sliku.
    """
    if not EAL.does_directory_exist(TEXTURE_DIR):
        _info("mapa {0} jos ne postoji - preskacem postavke tekstura".format(TEXTURE_DIR))
        return 0

    touched = 0
    for asset_path in EAL.list_assets(TEXTURE_DIR, recursive=True, include_folder=False):
        asset = EAL.load_asset(asset_path)
        if not isinstance(asset, unreal.Texture2D):
            continue

        asset.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        asset.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        asset.set_editor_property("srgb", True)
        asset.set_editor_property(
            "mip_gen_settings",
            unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP if GENERATE_MIPS
            else unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        asset.set_editor_property(
            "lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)

        EAL.save_loaded_asset(asset, only_if_is_dirty=False)
        touched += 1

    _info("import postavke popravljene na {0} tekstura".format(touched))
    return touched


# ---------------------------------------------------------------------------
# 2. Master materijali
# ---------------------------------------------------------------------------

def build_master_material(asset_name, masked):
    """
    Graf:

        VertexNormalWS -> Transform(World->Instance) -> Mask(B)
              |
              +-> Multiply( 10) -> Clamp(0,1) = TopMask
              +-> Multiply(-10) -> Clamp(0,1) = BottomMask

        Lerp(A=Side, B=Top,    Alpha=TopMask)
             -> Lerp(A=^, B=Bottom, Alpha=BottomMask) -> Base Color

    Mask(B) daje +1 na gornjoj strani kocke, -1 na donjoj, 0 na bokovima.
    Mnozenje s +-10 i clamp pretvore to u ostru 0/1 masku.
    """
    package_path = "{0}/{1}".format(MATERIAL_DIR, asset_name)

    material = _load("{0}.{1}".format(package_path, asset_name))
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati materijal {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        MEL.delete_all_material_expressions(material)
        _info("postojeci {0} - graf ocisen i gradi se ponovno".format(package_path))

    material.set_editor_property(
        "blend_mode",
        unreal.BlendMode.BLEND_MASKED if masked else unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", masked)
    if masked:
        material.set_editor_property("opacity_mask_clip_value", 0.333)

    fallback = _load(FALLBACK_TEXTURE)

    # --- tri teksturna parametra ---
    samplers = {}
    for index, face in enumerate(FACES):
        node = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D,
                     -1100, -300 + index * 300)
        node.set_editor_property("parameter_name", face)
        if fallback is not None:
            node.set_editor_property("texture", fallback)
        samplers[face] = node

    # --- izvor normale ---
    normal_ws = _expr(material, unreal.MaterialExpressionVertexNormalWS, -1100, 500)
    source, target, space_name = _pick_normal_space()
    if source is None:
        _info("UPOZORENJE: Transform nod nije dostupan - maska ostaje u world "
              "spaceu; rotacija blokova nece imati vizualni ucinak")
        normal_source = normal_ws
    else:
        transform = _expr(material, unreal.MaterialExpressionTransform, -900, 500)
        transform.set_editor_property("transform_source_type", source)
        transform.set_editor_property("transform_type", target)
        _connect(normal_ws, "", transform, "")
        normal_source = transform
        _info("normala se transformira World -> {0}".format(space_name))
        if space_name == "TRANSFORM_LOCAL":
            _info("UPOZORENJE: instance space nije dostupan; za ISM je 'Local' "
                  "prostor komponente pa rotacija instanci nece raditi")

    mask_b = _expr(material, unreal.MaterialExpressionComponentMask, -700, 500)
    mask_b.set_editor_property("r", False)
    mask_b.set_editor_property("g", False)
    mask_b.set_editor_property("b", True)
    mask_b.set_editor_property("a", False)
    _connect(normal_source, "", mask_b, "")

    saturate_class = getattr(unreal, "MaterialExpressionSaturate", None)

    def sharp_mask(scale, y):
        multiply = _expr(material, unreal.MaterialExpressionMultiply, -500, y)
        multiply.set_editor_property("const_b", scale)
        _connect(mask_b, "", multiply, "A")

        if saturate_class is not None:
            limiter = _expr(material, saturate_class, -350, y)
        else:
            limiter = _expr(material, unreal.MaterialExpressionClamp, -350, y)
            limiter.set_editor_property("min_default", 0.0)
            limiter.set_editor_property("max_default", 1.0)
        # Glavni ulaz i Saturate i Clamp noda je bezimen - pin se adresira "".
        _connect(multiply, "", limiter, "")
        return limiter

    top_mask = sharp_mask(10.0, 400)
    bottom_mask = sharp_mask(-10.0, 620)

    # --- dva lerpa: prvo gornja, pa donja strana ---
    lerp_top = _expr(material, unreal.MaterialExpressionLinearInterpolate, -150, 0)
    _connect(samplers["Side"], "", lerp_top, "A")
    _connect(samplers["Top"], "", lerp_top, "B")
    _connect(top_mask, "", lerp_top, "Alpha")

    lerp_bottom = _expr(material, unreal.MaterialExpressionLinearInterpolate, 50, 200)
    _connect(lerp_top, "", lerp_bottom, "A")
    _connect(samplers["Bottom"], "", lerp_bottom, "B")
    _connect(bottom_mask, "", lerp_bottom, "Alpha")

    _connect_property(lerp_bottom, "", material, unreal.MaterialProperty.MP_BASE_COLOR)

    # --- hrapavost kao parametar da se moze podesiti po bloku ---
    roughness = _expr(material, unreal.MaterialExpressionScalarParameter, -350, 850)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", DEFAULT_ROUGHNESS)
    _connect_property(roughness, "", material, unreal.MaterialProperty.MP_ROUGHNESS)

    # --- alpha cutout za lisce ---
    if masked:
        # Sve tri strane lisca koriste istu teksturu, pa je alpha bocne dovoljna.
        _connect_property(samplers["Side"], "A", material,
                          unreal.MaterialProperty.MP_OPACITY_MASK)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    return material


# ---------------------------------------------------------------------------
# 3. Material Instance po bloku
# ---------------------------------------------------------------------------

def _resolve_face_texture(block_name, face):
    """
    Trazi tocno T_<Blok>_<Strana>. Bez fallbacka i bez posudivanja izmedu
    blokova: ime datoteke jednoznacno odreduje kojem bloku i kojoj strani
    pripada, pa se iz popisa tekstura vidi cijela istina o izgledu bloka.

    Blok koji je sa svih strana isti svejedno treba sve tri datoteke - izvezi
    isti crtez tri puta.
    """
    name = "T_{0}_{1}".format(block_name, face)
    return _load("{0}/{1}.{2}".format(TEXTURE_DIR, name, name)), name


def build_material_instances(masters):
    created = 0
    skipped = []

    for block_name, masked in BLOCKS:
        parent = masters[MASTER_MASKED if masked else MASTER_OPAQUE]
        if parent is None:
            skipped.append((block_name, "master materijal nije izgraden"))
            continue

        resolved = {}
        missing = []
        for face in FACES:
            texture, texture_name = _resolve_face_texture(block_name, face)
            if texture is None:
                missing.append(texture_name)
            else:
                resolved[face] = (texture, texture_name)

        # Sve tri ili nijedna - polovicno postavljen MI bi tiho pokazivao
        # placeholder teksturu na strani koja nedostaje.
        if missing:
            skipped.append((block_name, "nedostaje " + ", ".join(missing)))
            continue

        mi_name = "MI_{0}".format(block_name)
        mi_path = "{0}/{1}".format(MATERIAL_DIR, mi_name)

        instance = _load("{0}.{1}".format(mi_path, mi_name))
        if instance is None:
            instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                mi_name, MATERIAL_DIR, unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew())
            if instance is None:
                _fail("ne mogu kreirati {0}".format(mi_path))
                continue

        MEL.set_material_instance_parent(instance, parent)
        for face, (texture, _texture_name) in resolved.items():
            MEL.set_material_instance_texture_parameter_value(instance, face, texture)

        EAL.save_loaded_asset(instance, only_if_is_dirty=False)
        created += 1
        _info("{0}  <-  {1}".format(
            mi_name, ", ".join("{0}={1}".format(f, resolved[f][1]) for f in FACES)))

    for block_name, reason in skipped:
        _info("preskocen MI_{0}: {1}".format(block_name, reason))

    return created, skipped


# ---------------------------------------------------------------------------

def main():
    _failures[:] = []
    _info("=== pocetak ===")

    for directory in (TEXTURE_DIR, MATERIAL_DIR):
        if not EAL.does_directory_exist(directory):
            EAL.make_directory(directory)

    fix_texture_import_settings()

    masters = {
        MASTER_OPAQUE: build_master_material(MASTER_OPAQUE, masked=False),
        MASTER_MASKED: build_master_material(MASTER_MASKED, masked=True),
    }

    created, skipped = build_material_instances(masters)

    _info("=== gotovo: {0} master materijala, {1} MI instanci, {2} preskoceno, "
          "{3} gresaka ===".format(
              len([m for m in masters.values() if m is not None]),
              created, len(skipped), len(_failures)))

    if _failures:
        for message in _failures:
            unreal.log_error("[BlockMaterials] NEUSPJEH: " + message)


main()
