"""
build_water_material.py
=======================

Izgradi materijal vode M_Water i instancu MI_Water za stimanje
(Docs/PLAN_Water.md, faza 2). AVoxelWorld stavlja MI_Water na plohu mora
svakog chunka. Izgradi i post-process ispod vode M_Underwater s instancom
MI_Underwater (faza 3).

Pokretanje iz editora:   Tools > MinecraftClone > Build Water Material

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/build_water_material.py"

Materijal koristi shading model Single Layer Water (dio enginea, ne Water
plugina). On sam racuna kako svjetlo slabi i rasprsuje se s dubinom, lomi
sliku dna i radi odsjaje. Graf:

    Valovi (World Position Offset): zbroj cetiri Gerstnerova vala u Custom
    cvoru. Svaki val ima smjer i valnu duljinu (WaveN, RGB = smjer X, smjer Y,
    valna duljina u UU) i amplitudu (WaveN.A, u UU). Brzina vala slijedi iz
    valne duljine kao na pravoj vodi (c = sqrt(g / k), g = 980 UU/s^2).
    Steepness (0..1) odreduje koliko se vrhovi zasilje. Valovi se stisaju na
    tockama uz obalu (vertex color R = 1, ShoreDamping) i s udaljenoscu od
    kamere (WaveFadeStart, WaveFadeLength).

    Normala (world space): analiticka normala istih valova + dvije normal mape
    koje klize (DetailScaleA/B, DetailPanA/B, DetailStrength).

    Pjena: gdje je dno blizu povrsine (SceneDepthWithoutWater - PixelDepth
    manje od FoamDepth). Single Layer Water preko Opacity mijesa Base Color
    (FoamColor) preko vode.

    Boja vode: AbsorptionCoefficients i ScatteringCoefficients (jedinica 1/cm),
    PhaseG. Vrijednosti su polazne, stimaju se u MI_Water.

Ista formula valova bit ce i u C++ (faza 4, plivanje), s parametrima procitanim
iz MI_Water - zato su svi parametri valova na jednom mjestu.
"""

import unreal

WATER_DIR = "/Game/Water"
MATERIAL_NAME = "M_Water"
INSTANCE_NAME = "MI_Water"
UNDERWATER_MATERIAL_NAME = "M_Underwater"
UNDERWATER_INSTANCE_NAME = "MI_Underwater"
DETAIL_NORMAL_TEXTURE = "/Engine/Functions/Engine_MaterialFunctions02/ExampleContent/Textures/water_n"

# Polazni valovi: (smjer X, smjer Y, valna duljina UU, amplituda UU)
WAVES = [
    (1.0, 0.3, 1200.0, 6.0),
    (-0.4, 1.0, 700.0, 4.0),
    (0.7, -0.6, 400.0, 2.5),
    (-0.9, -0.2, 230.0, 1.5),
]

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

_failures = []


def _fail(message):
    _failures.append(message)
    unreal.log_error("[WaterMaterial] " + message)


def _info(message):
    unreal.log("[WaterMaterial] " + message)


def _expr(material, expression_class, x, y):
    return MEL.create_material_expression(material, expression_class, x, y)


def _connect(src, src_output, dst, dst_input):
    ok = MEL.connect_material_expressions(src, src_output, dst, dst_input)
    if not ok:
        _fail("spoj odbijen: {0}.'{1}' -> {2}.'{3}'".format(
            src.get_class().get_name(), src_output,
            dst.get_class().get_name(), dst_input))
    return ok


def _connect_property(src, src_output, prop, label):
    if not MEL.connect_material_property(src, src_output, prop):
        _fail("spoj na {0} odbijen".format(label))


def _scalar(material, name, value, x, y):
    node = _expr(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def _vector(material, name, rgba, x, y):
    node = _expr(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*rgba))
    return node


def _world_position(material, x, y, exclude_offsets):
    node = _expr(material, unreal.MaterialExpressionWorldPosition, x, y)
    if exclude_offsets:
        # Za World Position Offset: polozaj bez vlastitog pomaka, inace bi
        # val ovisio sam o sebi
        node.set_editor_property(
            "world_position_shader_offset",
            unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    return node


def _custom(material, code, inputs, x, y, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    node = _expr(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    custom_inputs = []
    for name in inputs:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    node.set_editor_property("inputs", custom_inputs)
    return node


# Zajednicki pocetak oba Custom cvora: valovi i prigusenje (obala + udaljenost)
_WAVE_SETUP = """
float4 W[4] = { float4(W0, A0), float4(W1, A1), float4(W2, A2), float4(W3, A3) };
float Fade = 1.0 - saturate((distance(Cam, P) - FadeStart) / max(FadeLength, 1.0));
float Damp = saturate(1.0 - VC * ShoreDamp) * Fade;
"""

_WAVE_LOOP_HEAD = """
for (int i = 0; i < 4; i++)
{
    float2 D = normalize(W[i].xy + float2(1e-5, 0.0));
    float K = 6.2831853 / max(W[i].z, 1.0);
    float C = sqrt(980.0 / K);
    float F = K * (dot(D, P.xy) - C * T);
    float A = W[i].w * Damp;
    float Q = S / (K * max(W[i].w, 0.001) * 4.0);
"""

OFFSET_CODE = _WAVE_SETUP + """
float3 Offset = 0;
""" + _WAVE_LOOP_HEAD + """
    Offset.xy += Q * A * D * cos(F);
    Offset.z += A * sin(F);
}
return Offset;
"""

NORMAL_CODE = _WAVE_SETUP + """
float3 N = float3(0.0, 0.0, 1.0);
""" + _WAVE_LOOP_HEAD + """
    float WA = K * A;
    N.x -= D.x * WA * cos(F);
    N.y -= D.y * WA * cos(F);
    N.z -= Q * WA * sin(F);
}
N = normalize(N);
float2 Detail = (DA.xy + DB.xy) * 0.5 * DS * Fade;
return normalize(float3(N.xy + Detail, N.z));
"""

WAVE_INPUTS = ["P", "T", "Cam", "VC", "ShoreDamp", "FadeStart", "FadeLength", "S",
               "W0", "A0", "W1", "A1", "W2", "A2", "W3", "A3"]


def _wire_wave_inputs(node, shared, waves):
    for name, (src, output) in shared.items():
        _connect(src, output, node, name)
    for index, wave in enumerate(waves):
        _connect(wave, "", node, "W{0}".format(index))
        _connect(wave, "A", node, "A{0}".format(index))


def _detail_sample(material, texture, sampler_type, world_xy, time, scale_name, scale, pan_name, pan, y):
    scale_node = _scalar(material, scale_name, scale, -2200, y)
    divided = _expr(material, unreal.MaterialExpressionDivide, -1950, y)
    _connect(world_xy, "", divided, "A")
    _connect(scale_node, "", divided, "B")

    pan_node = _vector(material, pan_name, (pan[0], pan[1], 0.0, 0.0), -2200, y + 120)
    pan_mask = _expr(material, unreal.MaterialExpressionComponentMask, -1950, y + 120)
    pan_mask.set_editor_property("r", True)
    pan_mask.set_editor_property("g", True)
    _connect(pan_node, "", pan_mask, "")
    panned = _expr(material, unreal.MaterialExpressionMultiply, -1750, y + 120)
    _connect(pan_mask, "", panned, "A")
    _connect(time, "", panned, "B")

    uv = _expr(material, unreal.MaterialExpressionAdd, -1550, y)
    _connect(divided, "", uv, "A")
    _connect(panned, "", uv, "B")

    sampler = _expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -1350, y)
    sampler.set_editor_property("parameter_name", scale_name.replace("Scale", "Normal"))
    sampler.set_editor_property("texture", texture)
    sampler.set_editor_property("sampler_type", sampler_type)
    _connect(uv, "", sampler, "UVs")
    return sampler


def build_material():
    # unreal.load_asset ucitava paket izravno; EAL.load_asset trazi asset u Asset
    # Registryju, a headless commandlet engine sadrzaj tamo ne skenira
    texture = unreal.load_asset(DETAIL_NORMAL_TEXTURE)
    if texture is None:
        _fail("ne mogu ucitati {0}".format(DETAIL_NORMAL_TEXTURE))
        return None
    is_normal_map = texture.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_NORMALMAP
    sampler_type = (unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if is_normal_map
                    else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    _info("detaljna normal mapa {0} (normal map kompresija: {1})".format(DETAIL_NORMAL_TEXTURE, is_normal_map))

    package_path = "{0}/{1}".format(WATER_DIR, MATERIAL_NAME)
    material = EAL.load_asset(package_path) if EAL.does_asset_exist(package_path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            MATERIAL_NAME, WATER_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        _clear_graph(material)
        _info("postojeci {0} - graf ociscen i gradi se ponovno".format(package_path))

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("tangent_space_normal", False)

    # --- Zajednicki ulazi valova ---
    time_node = _expr(material, unreal.MaterialExpressionTime, -2600, -600)
    speed = _scalar(material, "WaveSpeed", 1.0, -2600, -500)
    wave_time = _expr(material, unreal.MaterialExpressionMultiply, -2350, -560)
    _connect(time_node, "", wave_time, "A")
    _connect(speed, "", wave_time, "B")

    camera = _expr(material, unreal.MaterialExpressionCameraPositionWS, -2600, -400)
    vertex_color = _expr(material, unreal.MaterialExpressionVertexColor, -2600, -300)
    shore = _scalar(material, "ShoreDamping", 1.0, -2600, -150)
    fade_start = _scalar(material, "WaveFadeStart", 3000.0, -2600, -50)
    fade_length = _scalar(material, "WaveFadeLength", 3000.0, -2600, 50)
    steepness = _scalar(material, "Steepness", 0.5, -2600, 150)

    waves = []
    for index, (dir_x, dir_y, length, amplitude) in enumerate(WAVES):
        waves.append(_vector(material, "Wave{0}".format(index),
                             (dir_x, dir_y, length, amplitude), -2600, 300 + index * 150))

    vs_position = _world_position(material, -2350, -800, exclude_offsets=True)
    ps_position = _world_position(material, -2350, 1000, exclude_offsets=False)

    shared = {
        "T": (wave_time, ""),
        "Cam": (camera, ""),
        "VC": (vertex_color, "R"),
        "ShoreDamp": (shore, ""),
        "FadeStart": (fade_start, ""),
        "FadeLength": (fade_length, ""),
        "S": (steepness, ""),
    }

    # --- Valovi -> World Position Offset ---
    offset = _custom(material, OFFSET_CODE, WAVE_INPUTS, -1800, -700)
    _connect(vs_position, "", offset, "P")
    _wire_wave_inputs(offset, shared, waves)
    _connect_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, "World Position Offset")

    # --- Normala: valovi + dvije normal mape koje klize ---
    world_xy = _expr(material, unreal.MaterialExpressionComponentMask, -2350, 1150)
    world_xy.set_editor_property("r", True)
    world_xy.set_editor_property("g", True)
    _connect(ps_position, "", world_xy, "")
    detail_a = _detail_sample(material, texture, sampler_type, world_xy, time_node,
                              "DetailScaleA", 400.0, "DetailPanA", (0.02, 0.01), 1300)
    detail_b = _detail_sample(material, texture, sampler_type, world_xy, time_node,
                              "DetailScaleB", 250.0, "DetailPanB", (-0.015, 0.02), 1600)
    detail_strength = _scalar(material, "DetailStrength", 0.3, -1350, 1900)

    normal = _custom(material, NORMAL_CODE, WAVE_INPUTS + ["DA", "DB", "DS"], -800, 900)
    _connect(ps_position, "", normal, "P")
    _wire_wave_inputs(normal, shared, waves)
    _connect(detail_a, "", normal, "DA")
    _connect(detail_b, "", normal, "DB")
    _connect(detail_strength, "", normal, "DS")
    _connect_property(normal, "", unreal.MaterialProperty.MP_NORMAL, "Normal")

    # --- Pjena: dno blizu povrsine ---
    below = _expr(material, unreal.MaterialExpressionSceneDepthWithoutWater, -1300, 2200)
    pixel_depth = _expr(material, unreal.MaterialExpressionPixelDepth, -1300, 2350)
    depth = _expr(material, unreal.MaterialExpressionSubtract, -1100, 2250)
    _connect(below, "", depth, "A")
    _connect(pixel_depth, "", depth, "B")
    foam_depth = _scalar(material, "FoamDepth", 40.0, -1100, 2400)
    depth_ratio = _expr(material, unreal.MaterialExpressionDivide, -900, 2250)
    _connect(depth, "", depth_ratio, "A")
    _connect(foam_depth, "", depth_ratio, "B")
    depth_clamped = _expr(material, unreal.MaterialExpressionSaturate, -750, 2250)
    _connect(depth_ratio, "", depth_clamped, "")
    foam_mask = _expr(material, unreal.MaterialExpressionOneMinus, -600, 2250)
    _connect(depth_clamped, "", foam_mask, "")
    foam_intensity = _scalar(material, "FoamIntensity", 0.8, -600, 2400)
    foam = _expr(material, unreal.MaterialExpressionMultiply, -400, 2250)
    _connect(foam_mask, "", foam, "A")
    _connect(foam_intensity, "", foam, "B")
    _connect_property(foam, "", unreal.MaterialProperty.MP_OPACITY, "Opacity")

    foam_color = _vector(material, "FoamColor", (0.9, 0.95, 1.0, 1.0), -400, 2000)
    _connect_property(foam_color, "", unreal.MaterialProperty.MP_BASE_COLOR, "Base Color")

    roughness = _scalar(material, "Roughness", 0.05, -400, 1800)
    _connect_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS, "Roughness")

    # --- Boja vode: Single Layer Water izlaz ---
    water_output = _expr(material, unreal.MaterialExpressionSingleLayerWaterMaterialOutput, -200, 2700)
    scattering = _vector(material, "ScatteringCoefficients", (0.0002, 0.0006, 0.0008, 0.0), -600, 2600)
    absorption = _vector(material, "AbsorptionCoefficients", (0.0045, 0.0012, 0.0008, 0.0), -600, 2750)
    phase_g = _scalar(material, "PhaseG", 0.1, -600, 2900)
    _connect(scattering, "", water_output, "ScatteringCoefficients")
    _connect(absorption, "", water_output, "AbsorptionCoefficients")
    _connect(phase_g, "", water_output, "PhaseG")

    return _compile_and_save(material)


def _compile_and_save(material):
    # Headless commandlet shadere ne prevodi - greska prevodenja vidi se tek u
    # editoru (Stats panel materijala)
    MEL.recompile_material(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    return material


# Maska ispod vode: tocka na near planeu kamere kroz ovaj piksel je ispod plohe
# vala. Isti valovi kao OFFSET_CODE, ali samo visina (vodoravni pomak Gerstnera
# se zanemaruje) i bez stisavanja uz obalu - kamera nema vertex color.
UNDERWATER_MASK_CODE = """
float4 W[4] = { float4(W0, A0), float4(W1, A1), float4(W2, A2), float4(W3, A3) };
float3 N = Cam + normalize(P - Cam) * NearDist;
float H = 0.0;
for (int i = 0; i < 4; i++)
{
    float2 D = normalize(W[i].xy + float2(1e-5, 0.0));
    float K = 6.2831853 / max(W[i].z, 1.0);
    float C = sqrt(980.0 / K);
    H += W[i].w * sin(K * (dot(D, N.xy) - C * T));
}
return saturate((SurfaceZ + H - N.z) * 0.5 + 0.5);
"""

# Boja ispod vode: zamucena slika obojena Tintom tone u FogColor s udaljenoscu
UNDERWATER_COLOR_CODE = """
float Fog = 1.0 - exp(-Depth * FogDensity);
float3 Under = lerp(Blurred * Tint, FogColor, Fog);
return lerp(Sharp, Under, Mask);
"""

# Pomaci uzoraka zamucenja (u jedinicama BlurRadius, viewport UV)
BLUR_OFFSETS = [(0.0, 0.0), (1.0, 1.0), (-1.0, 1.0), (1.0, -1.0), (-1.0, -1.0)]


def _mask_channels(material, src, output, channels, x, y):
    mask = _expr(material, unreal.MaterialExpressionComponentMask, x, y)
    for channel in channels:
        mask.set_editor_property(channel, True)
    _connect(src, output, mask, "")
    return mask


def _clear_graph(material):
    # delete_all_material_expressions brise iz liste kroz koju iterira, pa jednim
    # prolazom ostavi svaki drugi cvor (MaterialEditingLibrary.cpp). Ostaci bi se
    # gomilali svakim pokretanjem, a drugi Single Layer Water izlaz rusi prevodenje.
    before = MEL.get_num_material_expressions(material)
    for _ in range(64):
        if MEL.get_num_material_expressions(material) == 0:
            _info("{0}: obrisano {1} starih cvorova".format(material.get_name(), before))
            return
        MEL.delete_all_material_expressions(material)
    _fail("{0}: graf se ne da ocistiti ({1} cvorova ostalo)".format(
        material.get_name(), MEL.get_num_material_expressions(material)))


def _scene_texture(material, texture_id, uv, x, y):
    node = _expr(material, unreal.MaterialExpressionSceneTexture, x, y)
    node.set_editor_property("scene_texture_id", texture_id)
    _connect(uv, "", node, "UVs")
    return node


def build_underwater_material():
    """Post-process ispod vode (faza 3). AVoxelWorld ga ukljuci kad je kamera nad
    morem blizu plohe, a piksel po piksel odlucuje maska vala, pa crta vode na
    ekranu prati val. Parametre valova i SurfaceZ AVoxelWorld svaki frame
    prepisuje iz MI_Water, pa se u MI_Underwater stimaju samo boja, magla,
    zamucenje i njihanje slike."""
    package_path = "{0}/{1}".format(WATER_DIR, UNDERWATER_MATERIAL_NAME)
    material = EAL.load_asset(package_path) if EAL.does_asset_exist(package_path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            UNDERWATER_MATERIAL_NAME, WATER_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            _fail("ne mogu kreirati {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        _clear_graph(material)
        _info("postojeci {0} - graf ociscen i gradi se ponovno".format(package_path))

    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    # Prije tonemappera: magla se mijesa u linearnoj HDR boji
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_DOF)

    # --- Maska: piksel ispod plohe vala ---
    time_node = _expr(material, unreal.MaterialExpressionTime, -2400, -600)
    speed = _scalar(material, "WaveSpeed", 1.0, -2400, -500)
    wave_time = _expr(material, unreal.MaterialExpressionMultiply, -2150, -560)
    _connect(time_node, "", wave_time, "A")
    _connect(speed, "", wave_time, "B")
    camera = _expr(material, unreal.MaterialExpressionCameraPositionWS, -2400, -400)
    position = _world_position(material, -2400, -300, exclude_offsets=False)
    surface = _scalar(material, "SurfaceZ", 37.5, -2400, -200)
    near = _scalar(material, "NearDist", 10.0, -2400, -100)
    waves = [_vector(material, "Wave{0}".format(index), (dir_x, dir_y, length, amplitude),
                     -2400, 50 + index * 150)
             for index, (dir_x, dir_y, length, amplitude) in enumerate(WAVES)]

    mask_inputs = ["P", "Cam", "T", "SurfaceZ", "NearDist", "W0", "A0", "W1", "A1", "W2", "A2", "W3", "A3"]
    mask = _custom(material, UNDERWATER_MASK_CODE, mask_inputs, -1900, -300,
                   unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    _connect(position, "", mask, "P")
    _connect(camera, "", mask, "Cam")
    _connect(wave_time, "", mask, "T")
    _connect(surface, "", mask, "SurfaceZ")
    _connect(near, "", mask, "NearDist")
    for index, wave in enumerate(waves):
        _connect(wave, "", mask, "W{0}".format(index))
        _connect(wave, "A", mask, "A{0}".format(index))

    # --- Valovita slika: UV se njise sinusom, samo ispod vode ---
    screen = _expr(material, unreal.MaterialExpressionScreenPosition, -2400, 800)
    viewport_uv = _mask_channels(material, screen, "ViewportUV", ("r", "g"), -2150, 800)
    wobble_code = "return UV + sin(UV.yx * Freq + T * Speed) * Amount * Mask;"
    wobble = _custom(material, wobble_code, ["UV", "T", "Freq", "Speed", "Amount", "Mask"], -1700, 900,
                     unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    _connect(viewport_uv, "", wobble, "UV")
    _connect(time_node, "", wobble, "T")
    _connect(_scalar(material, "DistortionFrequency", 30.0, -2150, 950), "", wobble, "Freq")
    _connect(_scalar(material, "DistortionSpeed", 2.0, -2150, 1050), "", wobble, "Speed")
    _connect(_scalar(material, "DistortionAmount", 0.003, -2150, 1150), "", wobble, "Amount")
    _connect(mask, "", wobble, "Mask")

    # --- Zamucenje: prosjek pet uzoraka oko valovitog UV-a ---
    blur_radius = _scalar(material, "BlurRadius", 0.0015, -1700, 1300)
    blur_sum, blur_output = None, ""
    for index, (ox, oy) in enumerate(BLUR_OFFSETS):
        y = 1300 + index * 200
        offset = _expr(material, unreal.MaterialExpressionConstant2Vector, -1500, y)
        offset.set_editor_property("r", ox)
        offset.set_editor_property("g", oy)
        scaled = _expr(material, unreal.MaterialExpressionMultiply, -1300, y)
        _connect(offset, "", scaled, "A")
        _connect(blur_radius, "", scaled, "B")
        uv = _expr(material, unreal.MaterialExpressionAdd, -1100, y)
        _connect(wobble, "", uv, "A")
        _connect(scaled, "", uv, "B")
        sample = _scene_texture(material, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, uv, -900, y)
        sample_rgb = _mask_channels(material, sample, "Color", ("r", "g", "b"), -700, y)
        if blur_sum is None:
            blur_sum = sample_rgb
            continue
        added = _expr(material, unreal.MaterialExpressionAdd, -500, y)
        _connect(blur_sum, "", added, "A")
        _connect(sample_rgb, "", added, "B")
        blur_sum = added
    blurred = _expr(material, unreal.MaterialExpressionDivide, -300, 2300)
    _connect(blur_sum, "", blurred, "A")
    blurred.set_editor_property("const_b", float(len(BLUR_OFFSETS)))

    # --- Ostra slika (iznad vode) i dubina ---
    sharp = _scene_texture(material, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, viewport_uv, -900, 500)
    sharp_rgb = _mask_channels(material, sharp, "Color", ("r", "g", "b"), -700, 500)
    depth = _scene_texture(material, unreal.SceneTextureId.PPI_SCENE_DEPTH, wobble, -900, 300)
    depth_r = _mask_channels(material, depth, "Color", ("r",), -700, 300)

    color = _custom(material, UNDERWATER_COLOR_CODE,
                    ["Sharp", "Blurred", "Depth", "Mask", "Tint", "FogColor", "FogDensity"], -100, 600)
    _connect(sharp_rgb, "", color, "Sharp")
    _connect(blurred, "", color, "Blurred")
    _connect(depth_r, "", color, "Depth")
    _connect(mask, "", color, "Mask")
    _connect(_vector(material, "Tint", (0.4, 0.75, 0.8, 1.0), -450, 700), "", color, "Tint")
    _connect(_vector(material, "FogColor", (0.01, 0.07, 0.09, 1.0), -450, 850), "", color, "FogColor")
    _connect(_scalar(material, "FogDensity", 0.0015, -450, 1000), "", color, "FogDensity")
    _connect_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR, "Emissive Color")

    return _compile_and_save(material)


def build_instance(parent, instance_name):
    package_path = "{0}/{1}".format(WATER_DIR, instance_name)
    instance = EAL.load_asset(package_path) if EAL.does_asset_exist(package_path) else None
    if instance is None:
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            instance_name, WATER_DIR, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        if instance is None:
            _fail("ne mogu kreirati {0}".format(package_path))
            return None
        _info("kreiran {0}".format(package_path))
    else:
        # Postojeca instanca zadrzava stimane vrijednosti - mijenja se samo roditelj
        _info("postojeci {0} - stimane vrijednosti ostaju".format(package_path))
    MEL.set_material_instance_parent(instance, parent)
    EAL.save_loaded_asset(instance, only_if_is_dirty=False)
    return instance


def main():
    _failures[:] = []
    _info("=== pocetak ===")

    if not EAL.does_directory_exist(WATER_DIR):
        EAL.make_directory(WATER_DIR)

    material = build_material()
    instance = build_instance(material, INSTANCE_NAME) if material is not None else None
    underwater = build_underwater_material()
    underwater_instance = (build_instance(underwater, UNDERWATER_INSTANCE_NAME)
                           if underwater is not None else None)

    built = [material, instance, underwater, underwater_instance]
    _info("=== gotovo: {0}/4 asseta, {1} gresaka ===".format(
        sum(1 for asset in built if asset is not None), len(_failures)))
    for message in _failures:
        unreal.log_error("[WaterMaterial] NEUSPJEH: " + message)


main()
