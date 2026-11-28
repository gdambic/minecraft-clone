"""
build_entity_light.py
=====================

Svjetlo blokova (baklje) na onome sto nije blok (Docs/PLAN_Torches.md, faza 5).
UBlockLightReceiverComponent svjetlinu mjesta upisuje u vektorski parametar
LightFallbackPos; materijal koji ga nema ostaje taman uz baklju. Skripta:

  1. izgradi M_EntityColor: jednobojni materijal s parametrom Color za igracevu
     ruku i buzdovan (dosad su imali Unrealov materijal kocke, koji se ne moze
     dopuniti)
  2. postojecim materijalima stvorenja (M_Zombie, M_Sheep) doda sjaj:

        Emissive = Base Color * boja svjetla(LightFallbackPos.R) * MPC_BlockLight.Intensity

     Ono sto je spojeno na Base Color ostaje netaknuto. Materijal koji vec ima
     nesto na Emissive se preskace, pa je skripta idempotentna.

Materijal slicica (M_ItemSprite) i materijali blokova dobivaju svjetlo u
build_block_materials.py, jer se tamo grade iz nule.

Pokretanje iz editora:   Tools > MinecraftClone > Import Torch (pokrece i ovu skriptu)

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/build_entity_light.py"
"""

import unreal

MATERIAL_DIR = "/Game/Blocks/Materials"
ENTITY_COLOR_MATERIAL = "M_EntityColor"
LIGHT_COLLECTION = "/Game/Blocks/Materials/MPC_BlockLight.MPC_BlockLight"
LIGHT_COLLECTION_PARAM = "Intensity"
LIGHT_PARAM = "LightFallbackPos"

# Materijali stvorenja koji nisu izgradeni skriptom koja zna za svjetlo
ENTITY_MATERIALS = (
    "/Game/Blueprints/MOBS/Zombie/M_Zombie.M_Zombie",
    "/Game/Blueprints/MOBS/FBX/sheep_sa_animacijama/Materials/M_Sheep.M_Sheep",
)

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

_entity_failures = []


def _entity_fail(message):
    _entity_failures.append(message)
    unreal.log_error("[EntityLight] " + message)


def _entity_info(message):
    unreal.log("[EntityLight] " + message)


def _node(material, expression_class, x, y):
    return MEL.create_material_expression(material, expression_class, x, y)


def _wire(src, src_output, dst, dst_input):
    if not MEL.connect_material_expressions(src, src_output, dst, dst_input):
        _entity_fail("spoj odbijen: {0}.'{1}' -> {2}.'{3}'".format(
            src.get_class().get_name(), src_output, dst.get_class().get_name(), dst_input))


def _times(material, a, a_output, b, x, y):
    multiply = _node(material, unreal.MaterialExpressionMultiply, x, y)
    _wire(a, a_output, multiply, "A")
    _wire(b, "", multiply, "B")
    return multiply


def _scaled(material, source, scale, offset, x, y):
    multiply = _node(material, unreal.MaterialExpressionMultiply, x, y)
    multiply.set_editor_property("const_b", scale)
    _wire(source, "", multiply, "A")
    add = _node(material, unreal.MaterialExpressionAdd, x + 150, y)
    add.set_editor_property("const_b", offset)
    _wire(multiply, "", add, "A")
    return add


def add_entity_light(material, base_node, base_output, x, y):
    """
    Spoji Emissive = baza * boja svjetla * jacina. Boja svjetla je Minecraftova
    (lightmap.fsh), ista kao u build_block_materials.py:

        r = b,  g = b * ((b * 0.6 + 0.4) * 0.6 + 0.4),  b = b * (b * b * 0.6 + 0.4)
    """
    parameter = _node(material, unreal.MaterialExpressionVectorParameter, x, y)
    parameter.set_editor_property("parameter_name", LIGHT_PARAM)
    parameter.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

    brightness = _node(material, unreal.MaterialExpressionComponentMask, x + 250, y)
    brightness.set_editor_property("r", True)
    brightness.set_editor_property("g", False)
    brightness.set_editor_property("b", False)
    brightness.set_editor_property("a", False)
    _wire(parameter, "", brightness, "")

    green_inner = _scaled(material, brightness, 0.6, 0.4, x + 450, y)
    green_factor = _scaled(material, green_inner, 0.6, 0.4, x + 750, y)
    green = _times(material, brightness, "", green_factor, x + 1050, y)

    squared = _times(material, brightness, "", brightness, x + 450, y + 150)
    blue_factor = _scaled(material, squared, 0.6, 0.4, x + 750, y + 150)
    blue = _times(material, brightness, "", blue_factor, x + 1050, y + 150)

    red_green = _node(material, unreal.MaterialExpressionAppendVector, x + 1200, y)
    _wire(brightness, "", red_green, "A")
    _wire(green, "", red_green, "B")
    color = _node(material, unreal.MaterialExpressionAppendVector, x + 1350, y)
    _wire(red_green, "", color, "A")
    _wire(blue, "", color, "B")

    limited = _node(material, unreal.MaterialExpressionClamp, x + 1500, y)
    limited.set_editor_property("min_default", 0.0)
    limited.set_editor_property("max_default", 1.0)
    _wire(color, "", limited, "")

    lit = _times(material, base_node, base_output, limited, x + 1650, y)

    collection = EAL.load_asset(LIGHT_COLLECTION) if EAL.does_asset_exist(LIGHT_COLLECTION) else None
    if collection is not None:
        # Kolekcija prije imena: cvor pri postavljanju imena trazi parametar u njoj
        intensity = _node(material, unreal.MaterialExpressionCollectionParameter, x + 1650, y + 150)
        intensity.set_editor_property("collection", collection)
        intensity.set_editor_property("parameter_name", LIGHT_COLLECTION_PARAM)
    else:
        _entity_info("nema MPC_BlockLight - jacina ostaje skalarni parametar")
        intensity = _node(material, unreal.MaterialExpressionScalarParameter, x + 1650, y + 150)
        intensity.set_editor_property("parameter_name", "BlockLightIntensity")
        intensity.set_editor_property("default_value", 1.0)

    emissive = _times(material, lit, "", intensity, x + 1800, y)
    if not MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        _entity_fail("{0}: spoj na Emissive odbijen".format(material.get_name()))


def build_entity_color_material():
    """Jednobojni materijal: Color -> Base Color, Roughness, i svjetlo blokova."""
    package_path = "{0}/{1}".format(MATERIAL_DIR, ENTITY_COLOR_MATERIAL)
    material = EAL.load_asset(package_path) if EAL.does_asset_exist(package_path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ENTITY_COLOR_MATERIAL, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _entity_fail("ne mogu kreirati {0}".format(package_path))
            return
    else:
        MEL.delete_all_material_expressions(material)

    color = _node(material, unreal.MaterialExpressionVectorParameter, -500, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    if not MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR):
        _entity_fail("M_EntityColor: spoj na Base Color odbijen")

    roughness = _node(material, unreal.MaterialExpressionScalarParameter, -500, 250)
    roughness.set_editor_property("parameter_name", "Roughness")
    roughness.set_editor_property("default_value", 0.5)
    MEL.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    add_entity_light(material, color, "", -500, 500)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    _entity_info("izgraden {0}".format(package_path))


def patch_entity_material(object_path):
    package_path = object_path.split(".")[0]
    if not EAL.does_asset_exist(package_path):
        _entity_info("preskacem {0}: ne postoji".format(package_path))
        return
    material = EAL.load_asset(package_path)
    if not isinstance(material, unreal.Material):
        _entity_fail("{0} nije Material".format(package_path))
        return

    if MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) is not None:
        _entity_info("{0} vec ima Emissive - preskacem".format(material.get_name()))
        return

    base_node = MEL.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    if base_node is None:
        _entity_fail("{0} nema nista na Base Color".format(material.get_name()))
        return
    base_output = MEL.get_material_property_input_node_output_name(
        material, unreal.MaterialProperty.MP_BASE_COLOR)

    add_entity_light(material, base_node, base_output, -400, 900)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    _entity_info("{0} dobio svjetlo blokova".format(material.get_name()))


def build_entity_light():
    _entity_failures[:] = []
    build_entity_color_material()
    for object_path in ENTITY_MATERIALS:
        patch_entity_material(object_path)
    if _entity_failures:
        for message in _entity_failures:
            unreal.log_error("[EntityLight] NEUSPJEH: " + message)
    else:
        _entity_info("=== gotovo bez gresaka ===")


build_entity_light()
