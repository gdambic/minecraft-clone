"""
build_block_materials.py
========================

Generira materijalni sustav za voxel blokove:

  1. popravi import postavke svih tekstura u /Game/Blocks/Textures
  2. izgradi master materijale M_VoxelBlock i M_VoxelBlock_Masked
     te M_ItemSprite (sprite itema u ruci, /Game/Items/Generated/Materials)
  3. izgradi MI instancu po bloku iz Content/Data/Blocks.json (jedini izvor
     istine): "material" polje daje putanju MI asseta, "masked" (default
     false) bira masked master (alpha-cutout, za lisce)
  4. izgradi MPC_BlockLight i M_BlockModel: svjetlo blokova (baklje,
     Docs/PLAN_Torches.md) i materijal blokova s modelom umjesto kocke

Pokretanje iz editora:   Window > Developer Tools > Output Log > Python
                         exec(open(r"C:/RVS/C++GameDev/MinecraftClone/Scripts/build_block_materials.py").read())

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/build_block_materials.py"

Skripta je idempotentna: postojeci se materijali NE brisu nego im se graf
ocisti i ponovno izgradi, pa reference iz koda i iz MI instanci prezive.
Slobodno je pokreni ponovno svaki put kad dodas ili promijenis teksturu.
"""

import json
import os

import unreal

# ---------------------------------------------------------------------------
# Konfiguracija
# ---------------------------------------------------------------------------

TEXTURE_DIR = "/Game/Blocks/Textures"
MATERIAL_DIR = "/Game/Blocks/Materials"

# Rucno nacrtani item spriteovi (T_Item_<X>, display type "sprite") - trebaju
# iste pixel-art import postavke kao blok teksture.
ITEM_TEXTURE_DIR = "/Game/Items/Textures"

MASTER_OPAQUE = "M_VoxelBlock"
MASTER_MASKED = "M_VoxelBlock_Masked"

# Blokovi s "model" poljem u Blocks.json (baklja): jedna tekstura T_<ime MI-ja
# bez "MI_">, UV dolazi iz modela. C++ gradi mesh iz Blocks.json.
MASTER_MODEL = "M_BlockModel"

# Jacina svjetla blokova za sve materijale odjednom. C++ je postavlja iz
# konzolne varijable voxel.light.intensity (AVoxelWorld::UpdateLightIntensity).
LIGHT_COLLECTION = "MPC_BlockLight"
LIGHT_COLLECTION_PARAM = "Intensity"

# Svjetlina stranica od svjetla blokova: custom data floatovi 3-5 su stranice
# +X +Y +Z, 6-8 su -X -Y -Z (AVoxelWorld::ComputeFaceLights). 0-2 je tint bioma.
LIGHT_DATA_INDEX_POS = 3
LIGHT_DATA_INDEX_NEG = 6

# Sprite materijal za prikaz itema u ruci (HOLDING). C++ ga ucitava po ovoj
# putanji (FirstPersonArmComponent) i mijenja SpriteTexture parametar po
# odabranom itemu - jedan materijal za sve iteme.
ITEM_SPRITE_MATERIAL_DIR = "/Game/Items/Generated/Materials"
ITEM_SPRITE_MATERIAL = "M_ItemSprite"
ITEM_SPRITE_PARAM = "SpriteTexture"

# Mipovi: 16x16 bez mipova daje ostre piksele izbliza, ali u daljini titra
# (aliasing kroz TSR). S mipovima je mirno, a Filter=Nearest i dalje cuva
# pixel-art izgled na bliskim blokovima. Prebaci na False za cisti MC izgled.
GENERATE_MIPS = True

# Konstantna hrapavost - pixel-art blokovi ne trebaju ORM mapu.
DEFAULT_ROUGHNESS = 0.9

# Popis blokova se cita iz Blocks.json - isti file koji cita UBlockRegistry.
BLOCKS_JSON = "Data/Blocks.json"

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
# 0. Popis blokova iz Blocks.json
# ---------------------------------------------------------------------------

def load_blocks_from_json():
    """
    Vrati [(block_name, masked, mi_package_path, biome_tint, has_model)] iz
    Content/Data/Blocks.json.

    "material" polje odreduje putanju MI asseta koji se gradi - skripta time
    garantirano proizvodi tocno onaj asset koji UBlockRegistry ocekuje.
    Blok bez "material" polja se preskace: namjerno nema vlastiti MI (siva
    fallback kocka).
    """
    path = os.path.join(unreal.Paths.project_content_dir(), BLOCKS_JSON)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            entries = json.load(handle)
    except (OSError, ValueError) as err:
        _fail("ne mogu procitati {0}: {1}".format(path, err))
        return []

    blocks = []
    for entry in entries:
        name = entry.get("blockType")
        if not name or name == "Air":
            continue

        material = entry.get("material")
        if not material:
            _info("blok {0} nema 'material' polje - preskacem MI".format(name))
            continue

        # "/Game/.../MI_X.MI_X" -> package path "/Game/.../MI_X"
        blocks.append((name, bool(entry.get("masked", False)), material.split(".")[0],
                       entry.get("biomeTint"), "model" in entry))

    return blocks


# ---------------------------------------------------------------------------
# 1. Import postavke tekstura
# ---------------------------------------------------------------------------

def fix_texture_import_settings():
    """
    Pixel-art teksture trebaju Nearest filter i nekomprimiranu RGBA.
    DXT/BC kompresija radi u blokovima 4x4 piksela, sto na 16x16 teksturi
    vidljivo unistava sliku.
    """
    touched = 0
    for directory in (TEXTURE_DIR, ITEM_TEXTURE_DIR):
        if not EAL.does_directory_exist(directory):
            _info("mapa {0} jos ne postoji - preskacem postavke tekstura".format(directory))
            continue

        for asset_path in EAL.list_assets(directory, recursive=True, include_folder=False):
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
            # ItemMeshExtruder cita piksele s CPU-a (mip 0) - bez streaminga
            # su podaci garantirano dostupni i u cooked/packaged buildu
            asset.set_editor_property("never_stream", True)

            EAL.save_loaded_asset(asset, only_if_is_dirty=False)
            touched += 1

    _info("import postavke popravljene na {0} tekstura".format(touched))
    return touched


# ---------------------------------------------------------------------------
# 2. Master materijali
# ---------------------------------------------------------------------------

def ensure_light_collection():
    """
    MPC_BlockLight sa skalarom Intensity (default 1). Postojeci se ne dira, jer
    novi parametar dobiva novi GUID i cvorovi u materijalima bi ga izgubili.
    """
    package_path = "{0}/{1}".format(MATERIAL_DIR, LIGHT_COLLECTION)
    collection = _load("{0}.{1}".format(package_path, LIGHT_COLLECTION))
    if collection is None:
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            LIGHT_COLLECTION, MATERIAL_DIR, unreal.MaterialParameterCollection, None)
        if collection is None:
            _fail("ne mogu kreirati {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))

    names = [str(p.get_editor_property("parameter_name"))
             for p in collection.get_editor_property("scalar_parameters")]
    if LIGHT_COLLECTION_PARAM not in names:
        parameter = unreal.CollectionScalarParameter()
        parameter.set_editor_property("parameter_name", LIGHT_COLLECTION_PARAM)
        parameter.set_editor_property("default_value", 1.0)
        collection.set_editor_property("scalar_parameters", [parameter])
        EAL.save_loaded_asset(collection, only_if_is_dirty=False)
    return collection


def _scale_offset(material, source, scale, offset, x, y):
    """source * scale + offset"""
    multiply = _expr(material, unreal.MaterialExpressionMultiply, x, y)
    multiply.set_editor_property("const_b", scale)
    _connect(source, "", multiply, "A")
    add = _expr(material, unreal.MaterialExpressionAdd, x + 150, y)
    add.set_editor_property("const_b", offset)
    _connect(multiply, "", add, "A")
    return add


def _product(material, a, b, x, y):
    multiply = _expr(material, unreal.MaterialExpressionMultiply, x, y)
    _connect(a, "", multiply, "A")
    _connect(b, "", multiply, "B")
    return multiply


def connect_block_light(material, base_color, brightness, collection, x, y):
    """
    Emissive = boja bloka * boja svjetla * jacina.

    brightness je svjetlina stranice koju racuna C++ (Minecraftov lightmap:
    1.5 * l / (4 - 3l), l = razina / 15). Boja svjetla je iz istog shadera
    (assets/minecraft/shaders/core/lightmap.fsh), toplo zuta pri srednjim
    razinama i bijela pri punoj:

        r = b
        g = b * ((b * 0.6 + 0.4) * 0.6 + 0.4)
        b = b * (b * b * 0.6 + 0.4)
    """
    green_inner = _scale_offset(material, brightness, 0.6, 0.4, x, y)
    green_factor = _scale_offset(material, green_inner, 0.6, 0.4, x + 300, y)
    green = _product(material, brightness, green_factor, x + 600, y)

    squared = _product(material, brightness, brightness, x, y + 150)
    blue_factor = _scale_offset(material, squared, 0.6, 0.4, x + 300, y + 150)
    blue = _product(material, brightness, blue_factor, x + 600, y + 150)

    red_green = _expr(material, unreal.MaterialExpressionAppendVector, x + 750, y)
    _connect(brightness, "", red_green, "A")
    _connect(green, "", red_green, "B")
    color = _expr(material, unreal.MaterialExpressionAppendVector, x + 900, y)
    _connect(red_green, "", color, "A")
    _connect(blue, "", color, "B")

    limited = _expr(material, unreal.MaterialExpressionClamp, x + 1050, y)
    limited.set_editor_property("min_default", 0.0)
    limited.set_editor_property("max_default", 1.0)
    _connect(color, "", limited, "")

    lit = _product(material, base_color, limited, x + 1200, y)

    # Jacina iz MPC_BlockLight; bez kolekcije ostaje skalarni parametar
    intensity = None
    if collection is not None:
        intensity = _expr(material, unreal.MaterialExpressionCollectionParameter, x + 1200, y + 150)
        intensity.set_editor_property("collection", collection)
        intensity.set_editor_property("parameter_name", LIGHT_COLLECTION_PARAM)
        # Redoslijed je bitan: cvor pri postavljanju imena trazi parametar u vec
        # postavljenoj kolekciji i pamti njegov GUID. Je li ga nasao vidi se tek
        # pri prevodenju materijala (headless: dodaj -AllowCommandletRendering i
        # u logu potrazi "Failed to compile").
    if intensity is None:
        intensity = _expr(material, unreal.MaterialExpressionScalarParameter, x + 1200, y + 150)
        intensity.set_editor_property("parameter_name", "BlockLightIntensity")
        intensity.set_editor_property("default_value", 1.0)

    emissive = _product(material, lit, intensity, x + 1350, y)
    _connect_property(emissive, "", material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def light_vector(material, data_index, fallback_name, x, y):
    """
    Svjetline tri stranice: custom data ISM instance + parametar za promovirani
    actor (jedno od toga je uvijek nula, kao kod tinta).
    """
    custom_data = _expr(material, unreal.MaterialExpressionPerInstanceCustomData3Vector, x, y)
    custom_data.set_editor_property("const_default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    custom_data.set_editor_property("data_index", data_index)

    fallback = _expr(material, unreal.MaterialExpressionVectorParameter, x, y + 200)
    fallback.set_editor_property("parameter_name", fallback_name)
    fallback.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

    total = _expr(material, unreal.MaterialExpressionAdd, x + 250, y)
    _connect(custom_data, "", total, "A")
    _connect(fallback, "", total, "B")
    return total


def build_master_material(asset_name, masked, with_tint=False, light_collection=None):
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

    with_tint dodaje biome tint granu (vidi Docs/PLAN_Biomes.md):

        FinalTint = PerInstanceCustomData3Vector(default bijelo)
                    * VectorParameter "TintFallback" (default bijelo)

      - Top ulaz u lerp postaje Top * FinalTint (grayscale trava -> boja bioma)
      - Side ulaz postaje Lerp(Side, SideOverlay * FinalTint,
                               SideOverlay.A * "SideOverlayStrength")
        SideOverlay je tintani rub trave PREKO netintane zemlje; strength je
        0 po defaultu pa blokovi bez overlay teksture ostaju netaknuti.

    Na ISM putu boju daje custom data (TintFallback bijel); na actor/item putu
    custom data pada na bijeli default a boju daje TintFallback - uvijek mnozi
    tocno jedna boja. Blokovi bez custom data i bez parametra: bijelo x bijelo.
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

    # --- biome tint grana (samo opaque master dok je lisce izvan opsega) ---
    top_source = samplers["Top"]
    side_source = samplers["Side"]
    if with_tint:
        custom_data = _expr(material, unreal.MaterialExpressionPerInstanceCustomData3Vector,
                            -1100, -600)
        custom_data.set_editor_property("const_default_value",
                                        unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        custom_data.set_editor_property("data_index", 0)

        tint_fallback = _expr(material, unreal.MaterialExpressionVectorParameter, -1100, -800)
        tint_fallback.set_editor_property("parameter_name", "TintFallback")
        tint_fallback.set_editor_property("default_value",
                                          unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

        final_tint = _expr(material, unreal.MaterialExpressionMultiply, -900, -700)
        _connect(custom_data, "", final_tint, "A")
        _connect(tint_fallback, "", final_tint, "B")

        # Top: cijela grayscale tekstura se tinta
        tinted_top = _expr(material, unreal.MaterialExpressionMultiply, -700, -300)
        _connect(samplers["Top"], "", tinted_top, "A")
        _connect(final_tint, "", tinted_top, "B")
        top_source = tinted_top

        # Side: tintani overlay (rub trave) preko netintane zemlje
        overlay = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D,
                        -1100, -100)
        overlay.set_editor_property("parameter_name", "SideOverlay")
        if fallback is not None:
            overlay.set_editor_property("texture", fallback)

        overlay_strength = _expr(material, unreal.MaterialExpressionScalarParameter,
                                 -900, 100)
        overlay_strength.set_editor_property("parameter_name", "SideOverlayStrength")
        overlay_strength.set_editor_property("default_value", 0.0)

        overlay_alpha = _expr(material, unreal.MaterialExpressionMultiply, -700, 50)
        _connect(overlay, "A", overlay_alpha, "A")
        _connect(overlay_strength, "", overlay_alpha, "B")

        tinted_overlay = _expr(material, unreal.MaterialExpressionMultiply, -700, -100)
        _connect(overlay, "", tinted_overlay, "A")
        _connect(final_tint, "", tinted_overlay, "B")

        side_with_overlay = _expr(material, unreal.MaterialExpressionLinearInterpolate,
                                  -500, -50)
        _connect(samplers["Side"], "", side_with_overlay, "A")
        _connect(tinted_overlay, "", side_with_overlay, "B")
        _connect(overlay_alpha, "", side_with_overlay, "Alpha")
        side_source = side_with_overlay

    # --- dva lerpa: prvo gornja, pa donja strana ---
    lerp_top = _expr(material, unreal.MaterialExpressionLinearInterpolate, -150, 0)
    _connect(side_source, "", lerp_top, "A")
    _connect(top_source, "", lerp_top, "B")
    _connect(top_mask, "", lerp_top, "Alpha")

    lerp_bottom = _expr(material, unreal.MaterialExpressionLinearInterpolate, 50, 200)
    _connect(lerp_top, "", lerp_bottom, "A")
    _connect(samplers["Bottom"], "", lerp_bottom, "B")
    _connect(bottom_mask, "", lerp_bottom, "Alpha")

    _connect_property(lerp_bottom, "", material, unreal.MaterialProperty.MP_BASE_COLOR)

    # --- svjetlo blokova: svjetlina stranice odabrana po normali ---
    #   dot(saturate(N), svjetline +X +Y +Z) + dot(saturate(-N), svjetline -X -Y -Z)
    def clamp01(source, x, y):
        node = _expr(material, unreal.MaterialExpressionClamp, x, y)
        node.set_editor_property("min_default", 0.0)
        node.set_editor_property("max_default", 1.0)
        _connect(source, "", node, "")
        return node

    flipped = _expr(material, unreal.MaterialExpressionMultiply, -700, 1300)
    flipped.set_editor_property("const_b", -1.0)
    _connect(normal_source, "", flipped, "A")

    dot_pos = _expr(material, unreal.MaterialExpressionDotProduct, -300, 1200)
    _connect(clamp01(normal_source, -500, 1200), "", dot_pos, "A")
    _connect(light_vector(material, LIGHT_DATA_INDEX_POS, "LightFallbackPos", -1100, 1100), "", dot_pos, "B")

    dot_neg = _expr(material, unreal.MaterialExpressionDotProduct, -300, 1500)
    _connect(clamp01(flipped, -500, 1500), "", dot_neg, "A")
    _connect(light_vector(material, LIGHT_DATA_INDEX_NEG, "LightFallbackNeg", -1100, 1550), "", dot_neg, "B")

    face_brightness = _expr(material, unreal.MaterialExpressionAdd, -100, 1350)
    _connect(dot_pos, "", face_brightness, "A")
    _connect(dot_neg, "", face_brightness, "B")

    connect_block_light(material, lerp_bottom, face_brightness, light_collection, 100, 1350)

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
# 2a. Materijal blokova s modelom (baklja)
# ---------------------------------------------------------------------------

def build_model_master(light_collection):
    """
    Graf:

        TextureSampleParameter2D "Texture" (UV iz modela)
            RGB -> Base Color
            A   -> Opacity Mask

    Blok s modelom nije puna kocka, pa je cijeli osvijetljen razinom svoje
    celije: C++ u svih 6 svjetlina upise istu vrijednost i materijal cita samo
    prvu (R). Dvostran je da model ne ovisi o redoslijedu vrhova trokuta.
    """
    package_path = "{0}/{1}".format(MATERIAL_DIR, MASTER_MODEL)

    material = _load("{0}.{1}".format(package_path, MASTER_MODEL))
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MASTER_MODEL, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati materijal {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        MEL.delete_all_material_expressions(material)
        _info("postojeci {0} - graf ocisen i gradi se ponovno".format(package_path))

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.333)

    sampler = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
    sampler.set_editor_property("parameter_name", "Texture")
    fallback = _load(FALLBACK_TEXTURE)
    if fallback is not None:
        sampler.set_editor_property("texture", fallback)

    _connect_property(sampler, "", material, unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_property(sampler, "A", material, unreal.MaterialProperty.MP_OPACITY_MASK)

    own_light = light_vector(material, LIGHT_DATA_INDEX_POS, "LightFallbackPos", -1100, 500)
    mask_r = _expr(material, unreal.MaterialExpressionComponentMask, -700, 500)
    mask_r.set_editor_property("r", True)
    mask_r.set_editor_property("g", False)
    mask_r.set_editor_property("b", False)
    mask_r.set_editor_property("a", False)
    _connect(own_light, "", mask_r, "")
    connect_block_light(material, sampler, mask_r, light_collection, -500, 500)

    roughness = _expr(material, unreal.MaterialExpressionScalarParameter, -600, 300)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", DEFAULT_ROUGHNESS)
    _connect_property(roughness, "", material, unreal.MaterialProperty.MP_ROUGHNESS)

    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    return material


# ---------------------------------------------------------------------------
# 2b. Sprite materijal za item u ruci (HOLDING)
# ---------------------------------------------------------------------------

def build_item_sprite_material(light_collection=None):
    """
    Graf (jedan node, dvije zice):

        TextureSampleParameter2D "SpriteTexture"
            RGB -> Base Color
            A   -> Opacity Mask

    Masked + two-sided: pikseli izvan izometrijske kocke sprite-a su prozirni,
    pa quad u ruci vizualno nestane i ostane samo "kocka". Roughness konstanta
    kao kod blokova da item u ruci svjetlom izgleda kao ostatak svijeta.
    """
    package_path = "{0}/{1}".format(ITEM_SPRITE_MATERIAL_DIR, ITEM_SPRITE_MATERIAL)

    material = _load("{0}.{1}".format(package_path, ITEM_SPRITE_MATERIAL))
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ITEM_SPRITE_MATERIAL, ITEM_SPRITE_MATERIAL_DIR,
            unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati materijal {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        MEL.delete_all_material_expressions(material)
        _info("postojeci {0} - graf ocisen i gradi se ponovno".format(package_path))

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    # Sprite ima binarnu alphu (potpuno unutra ili potpuno izvan kocke)
    material.set_editor_property("opacity_mask_clip_value", 0.5)

    sampler = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D,
                    -400, 0)
    sampler.set_editor_property("parameter_name", ITEM_SPRITE_PARAM)
    fallback = _load(FALLBACK_TEXTURE)
    if fallback is not None:
        sampler.set_editor_property("texture", fallback)

    _connect_property(sampler, "", material, unreal.MaterialProperty.MP_BASE_COLOR)
    _connect_property(sampler, "A", material, unreal.MaterialProperty.MP_OPACITY_MASK)

    # Svjetlo blokova na predmetu u ruci i na dropu: jedna svjetlina za cijeli
    # predmet, upisuje je UBlockLightReceiverComponent u LightFallbackPos
    own_light = light_vector(material, LIGHT_DATA_INDEX_POS, "LightFallbackPos", -900, 600)
    mask_r = _expr(material, unreal.MaterialExpressionComponentMask, -500, 600)
    mask_r.set_editor_property("r", True)
    mask_r.set_editor_property("g", False)
    mask_r.set_editor_property("b", False)
    mask_r.set_editor_property("a", False)
    _connect(own_light, "", mask_r, "")
    connect_block_light(material, sampler, mask_r, light_collection, -300, 600)

    roughness = _expr(material, unreal.MaterialExpressionScalarParameter, -400, 300)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", DEFAULT_ROUGHNESS)
    _connect_property(roughness, "", material, unreal.MaterialProperty.MP_ROUGHNESS)

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


def build_model_instance(parent, mi_package):
    """MI bloka s modelom: jedna tekstura T_<ime MI-ja bez "MI_"> (MI_Torch -> T_Torch)."""
    if parent is None:
        return False

    mi_dir, mi_name = mi_package.rsplit("/", 1)
    texture_name = "T_" + (mi_name[3:] if mi_name.startswith("MI_") else mi_name)
    texture = _load("{0}/{1}.{2}".format(TEXTURE_DIR, texture_name, texture_name))
    if texture is None:
        _info("preskocen {0}: nedostaje {1}".format(mi_name, texture_name))
        return False

    instance = _load("{0}.{1}".format(mi_package, mi_name))
    if instance is None:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            mi_name, mi_dir, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        if instance is None:
            _fail("ne mogu kreirati {0}".format(mi_package))
            return False

    MEL.set_material_instance_parent(instance, parent)
    MEL.set_material_instance_texture_parameter_value(instance, "Texture", texture)
    EAL.save_loaded_asset(instance, only_if_is_dirty=False)
    _info("{0}  <-  Texture={1}".format(mi_name, texture_name))
    return True


def build_material_instances(masters, blocks):
    created = 0
    skipped = []
    built_model_instances = set()

    for block_name, masked, mi_package, biome_tint, has_model in blocks:
        if has_model:
            # Vise blokova smije dijeliti isti MI (stojeca i zidna baklja)
            if mi_package not in built_model_instances:
                built_model_instances.add(mi_package)
                if build_model_instance(masters.get(MASTER_MODEL), mi_package):
                    created += 1
                else:
                    skipped.append((block_name, "MI modela nije izgraden"))
            continue

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

        mi_dir, mi_name = mi_package.rsplit("/", 1)

        instance = _load("{0}.{1}".format(mi_package, mi_name))
        if instance is None:
            instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                mi_name, mi_dir, unreal.MaterialInstanceConstant,
                unreal.MaterialInstanceConstantFactoryNew())
            if instance is None:
                _fail("ne mogu kreirati {0}".format(mi_package))
                continue

        MEL.set_material_instance_parent(instance, parent)
        for face, (texture, _texture_name) in resolved.items():
            MEL.set_material_instance_texture_parameter_value(instance, face, texture)

        # Blok s biome tintom: ukljuci tintani side overlay (rub trave) ako
        # T_<Blok>_SideOverlay postoji. Bez overlaya rub na bocnoj strani
        # ostaje netintan (siv) - zato warning, ne tihi preskok.
        if biome_tint and not masked:
            overlay_name = "T_{0}_SideOverlay".format(block_name)
            overlay_tex = _load("{0}/{1}.{2}".format(TEXTURE_DIR, overlay_name, overlay_name))
            if overlay_tex is not None:
                MEL.set_material_instance_texture_parameter_value(
                    instance, "SideOverlay", overlay_tex)
                MEL.set_material_instance_scalar_parameter_value(
                    instance, "SideOverlayStrength", 1.0)
            else:
                _info("UPOZORENJE: blok {0} ima biomeTint, a {1} ne postoji - "
                      "rub na bocnoj strani ostaje netintan".format(block_name, overlay_name))

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

    for directory in (TEXTURE_DIR, MATERIAL_DIR, ITEM_SPRITE_MATERIAL_DIR):
        if not EAL.does_directory_exist(directory):
            EAL.make_directory(directory)

    blocks = load_blocks_from_json()
    if not blocks:
        _fail("Blocks.json nije dao nijedan blok - prekidam")
        return
    _info("Blocks.json: {0} blokova za obradu".format(len(blocks)))

    fix_texture_import_settings()

    # Tint grana samo u opaque masteru - lisce (masked) je izvan opsega dok
    # se biome tint ne prosiri na foliage (PLAN_Biomes.md)
    light_collection = ensure_light_collection()
    masters = {
        MASTER_OPAQUE: build_master_material(MASTER_OPAQUE, masked=False, with_tint=True,
                                             light_collection=light_collection),
        MASTER_MASKED: build_master_material(MASTER_MASKED, masked=True,
                                             light_collection=light_collection),
        MASTER_MODEL: build_model_master(light_collection),
    }

    sprite_material = build_item_sprite_material(light_collection)

    created, skipped = build_material_instances(masters, blocks)

    _info("=== gotovo: {0} master materijala, sprite materijal {1}, "
          "{2} MI instanci, {3} preskoceno, {4} gresaka ===".format(
              len([m for m in masters.values() if m is not None]),
              "OK" if sprite_material is not None else "NEUSPJEH",
              created, len(skipped), len(_failures)))

    if _failures:
        for message in _failures:
            unreal.log_error("[BlockMaterials] NEUSPJEH: " + message)


main()
