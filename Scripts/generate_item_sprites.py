"""
generate_item_sprites.py
=========================

Cita Content/Data/Items.json ("display" polje) i Content/Data/Blocks.json
("material" polje), pa za svaki item s display.type == "block" slozi
izometrijsku ikonu bloka i importa je kao Texture2D asset u
Content/Items/Generated/T_Item_<Block>.

Nacin rada je cisto 2D kompozitiranje, kao u pravom Minecraftu - NEMA
SceneCapture-a, svjetala ni shadera:

  1. Blocks.json "material" -> MI_* asset
  2. iz MI-ja se procitaju "Top" i "Side" texture parametri
     (isti parametri koje postavlja build_block_materials.py)
  3. teksture se dekodiraju u piksele
  4. tri vidljive plohe kocke (gornja + dvije bocne) mapiraju se afino
     na platno s fiksnim Minecraft sjencanjem (top 100%, lijeva ~80%,
     desna ~60%), pozadina ostaje prozirna
  5. rezultat se zapise kao PNG i importa kao asset s pixel-art
     postavkama (Nearest, UserInterface2D, bez mipova)

Item s display.type != "block" (npr. "none", buduci "sprite") se preskace.

Pokretanje: Tools > MinecraftClone > Generate Items Sprites (registrirano u
Content/Python/init_unreal.py), ili rucno iz Output Log > Python:
    exec(open(r"C:/RVS/C++GameDev/MinecraftClone/Scripts/generate_item_sprites.py").read())

Skripta je idempotentna i pregazuje postojece T_Item_* teksture.
"""

import json
import os
import shutil
import struct
import zlib

import unreal

ITEMS_JSON = "Data/Items.json"
BLOCKS_JSON = "Data/Blocks.json"
OUTPUT_DIR = "/Game/Items/Generated"

CANVAS = 256          # rezolucija ikone
EDGE = 100.0          # duljina brida kocke u pikselima platna
ISO_X = 0.866         # cos(30) - vodoravna komponenta izometrijskog brida

# Minecraft stil sjencanja ploha
SHADE_TOP = 1.00
SHADE_LEFT = 0.80
SHADE_RIGHT = 0.60

# Debug: kopija svakog PNG-a u Saved/ItemSpritesDebug/ za pregled bez editora
DEBUG_EXPORT_PNG = True

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

_generated = []
_failures = []


def _info(message):
    unreal.log("[ItemSprites] " + message)


def _warn(message):
    unreal.log_warning("[ItemSprites] " + message)


def _fail(message):
    _failures.append(message)
    unreal.log_error("[ItemSprites] " + message)


def _load_json(relative_path):
    path = os.path.join(unreal.Paths.project_content_dir(), relative_path)
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError) as error:
        _fail("ne mogu procitati {0}: {1}".format(path, error))
        return []


def _load_blocks_by_type():
    blocks = {}
    for entry in _load_json(BLOCKS_JSON):
        name = entry.get("blockType")
        if name:
            blocks[name] = entry
    return blocks


def _collect_blocks_to_render(blocks_by_type):
    wanted = []
    seen = set()
    for entry in _load_json(ITEMS_JSON):
        item_type = entry.get("itemType", "?")
        display = entry.get("display") or {}
        if display.get("type", "none") != "block":
            continue

        block_name = display.get("block")
        if not block_name:
            _warn("item '{0}' ima display.type=block bez 'block' polja - preskacem".format(item_type))
            continue
        if block_name not in blocks_by_type:
            _fail("item '{0}' referencira nepostojeci blok '{1}' - preskacem".format(item_type, block_name))
            continue
        if block_name not in seen:
            seen.add(block_name)
            wanted.append(block_name)
    return wanted


# ---------------------------------------------------------------------------
# Citanje piksela teksture: ExportTexture2D pise Radiance HDR (RGBE) sadrzaj
# bez obzira na ekstenziju datoteke, pa ga dekodiramo rucno.
# ---------------------------------------------------------------------------

def _decode_radiance_hdr(path):
    """Vrati (width, height, [[(r,g,b) 0-255 po retku]]) ili None."""
    with open(path, "rb") as handle:
        data = handle.read()

    if not data.startswith(b"#?"):
        return None

    pos = 0
    while True:
        eol = data.index(b"\n", pos)
        line = data[pos:eol]
        pos = eol + 1
        if line == b"":
            break

    eol = data.index(b"\n", pos)
    parts = data[pos:eol].split()
    pos = eol + 1
    height, width = int(parts[1]), int(parts[3])

    rows = []
    for _ in range(height):
        row = []
        if (pos + 3 < len(data) and data[pos] == 2 and data[pos + 1] == 2
                and ((data[pos + 2] << 8) | data[pos + 3]) == width):
            pos += 4
            channels = [[], [], [], []]
            for ch in range(4):
                count = 0
                while count < width:
                    run = data[pos]
                    pos += 1
                    if run > 128:
                        value = data[pos]
                        pos += 1
                        channels[ch].extend([value] * (run - 128))
                        count += run - 128
                    else:
                        channels[ch].extend(data[pos:pos + run])
                        pos += run
                        count += run
            for x in range(width):
                row.append((channels[0][x], channels[1][x], channels[2][x], channels[3][x]))
        else:
            for _ in range(width):
                row.append(tuple(data[pos:pos + 4]))
                pos += 4
        rows.append(row)

    # RGBE (linearno) -> sRGB 0-255
    out = []
    for row in rows:
        out_row = []
        for r, g, b, e in row:
            if e == 0:
                out_row.append((0, 0, 0))
            else:
                scale = 2.0 ** (e - 136)
                out_row.append(tuple(
                    int(round(min(1.0, max(0.0, c * scale)) ** (1.0 / 2.2) * 255))
                    for c in (r, g, b)))
        out.append(out_row)
    return width, height, out


def _read_texture_pixels(texture, tmp_dir, kismet_rendering):
    """Exportaj UTexture2D i dekodiraj u (w, h, pikseli)."""
    name = texture.get_name()
    kismet_rendering.export_texture2d(None, texture, tmp_dir, name + ".hdr")
    for candidate in (name + ".hdr", name + ".hdr.hdr", name + ".png"):
        path = os.path.join(tmp_dir, candidate)
        if os.path.isfile(path):
            decoded = _decode_radiance_hdr(path)
            if decoded:
                return decoded
    _fail("ne mogu dekodirati exportanu teksturu '{0}'".format(name))
    return None


# ---------------------------------------------------------------------------
# Kompozitiranje: tri plohe kocke kao afine transformacije na platnu.
# Koordinate platna: x desno, y dolje. F = prednji-gornji vrh kocke (centar).
# ---------------------------------------------------------------------------

def _compose_icon(top_pixels, side_pixels):
    """Vrati CANVAS x CANVAS RGBA piksele slozene izometrijske kocke."""
    fx, fy = CANVAS / 2.0, CANVAS / 2.0 - EDGE * 0.0  # F u sredini platna

    # Bridovi u screen prostoru
    u_vec = (ISO_X * EDGE, -0.5 * EDGE)    # gore-desno (top ploha)
    v_vec = (-ISO_X * EDGE, -0.5 * EDGE)   # gore-lijevo (top ploha)
    w_vec = (0.0, EDGE)                    # okomito dolje

    # (ploha, E1, E2, tekstura, sjencanje); tex v=0 je GORNJI rub teksture
    faces = [
        ("top", u_vec, v_vec, top_pixels, SHADE_TOP),
        ("right", u_vec, w_vec, side_pixels, SHADE_RIGHT),
        ("left", v_vec, w_vec, side_pixels, SHADE_LEFT),
    ]

    canvas = [[(0, 0, 0, 0)] * CANVAS for _ in range(CANVAS)]

    for _name, e1, e2, tex, shade in faces:
        tw, th, tp = tex
        det = e1[0] * e2[1] - e1[1] * e2[0]
        if abs(det) < 1e-6:
            continue
        inv = (e2[1] / det, -e2[0] / det, -e1[1] / det, e1[0] / det)

        xs = [fx, fx + e1[0], fx + e2[0], fx + e1[0] + e2[0]]
        ys = [fy, fy + e1[1], fy + e2[1], fy + e1[1] + e2[1]]
        x0, x1 = max(0, int(min(xs)) - 1), min(CANVAS - 1, int(max(xs)) + 1)
        y0, y1 = max(0, int(min(ys)) - 1), min(CANVAS - 1, int(max(ys)) + 1)

        for py in range(y0, y1 + 1):
            for px in range(x0, x1 + 1):
                dx, dy = (px + 0.5) - fx, (py + 0.5) - fy
                u = inv[0] * dx + inv[1] * dy
                v = inv[2] * dx + inv[3] * dy
                if 0.0 <= u < 1.0 and 0.0 <= v < 1.0:
                    tx = min(tw - 1, int(u * tw))
                    ty = min(th - 1, int(v * th))
                    r, g, b = tp[ty][tx]
                    canvas[py][px] = (int(r * shade), int(g * shade), int(b * shade), 255)

    return canvas


def _write_png(path, canvas):
    """Zapisi RGBA PNG bez vanjskih ovisnosti (zlib + struct)."""
    raw = b"".join(
        b"\x00" + b"".join(struct.pack("4B", *pixel) for pixel in row)
        for row in canvas)

    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", CANVAS, CANVAS, 8, 6, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        handle.write(chunk(b"IEND", b""))


# ---------------------------------------------------------------------------
# Import u Content i pixel-art postavke
# ---------------------------------------------------------------------------

def _import_png_as_texture(png_path, texture_name):
    target_path = "{0}/{1}".format(OUTPUT_DIR, texture_name)
    if EAL.does_asset_exist(target_path):
        EAL.delete_asset(target_path)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png_path)
    task.set_editor_property("destination_path", OUTPUT_DIR)
    task.set_editor_property("destination_name", texture_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not EAL.does_asset_exist(target_path):
        _fail("import PNG-a nije uspio za {0}".format(texture_name))
        return False

    texture = EAL.load_asset(target_path)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    EAL.save_asset(target_path, only_if_is_dirty=False)
    return True


def main():
    _generated[:] = []
    _failures[:] = []
    _info("=== pocetak ===")

    kismet_rendering = getattr(unreal, "RenderingLibrary", None) or getattr(unreal, "KismetRenderingLibrary", None)
    if kismet_rendering is None:
        _fail("unreal.RenderingLibrary/KismetRenderingLibrary ne postoji - prekidam")
        return

    if not EAL.does_directory_exist(OUTPUT_DIR):
        EAL.make_directory(OUTPUT_DIR)

    blocks_by_type = _load_blocks_by_type()
    if not blocks_by_type:
        _fail("Blocks.json nije dao nijedan blok - prekidam")
        return

    block_names = _collect_blocks_to_render(blocks_by_type)
    if not block_names:
        _info("nema itema s display.type=block - nema sto generirati")
        return
    _info("{0} blokova za obradu: {1}".format(len(block_names), ", ".join(block_names)))

    tmp_dir = os.path.join(unreal.Paths.project_saved_dir(), "ItemSpritesTmp")
    debug_dir = os.path.join(unreal.Paths.project_saved_dir(), "ItemSpritesDebug")
    os.makedirs(tmp_dir, exist_ok=True)
    if DEBUG_EXPORT_PNG:
        os.makedirs(debug_dir, exist_ok=True)

    for block_name in block_names:
        material_path = blocks_by_type[block_name].get("material")
        if not material_path:
            _warn("blok '{0}' nema 'material' polje - preskacem".format(block_name))
            continue
        if not EAL.does_asset_exist(material_path):
            _fail("blok '{0}': materijal {1} ne postoji - preskacem".format(block_name, material_path))
            continue

        material = EAL.load_asset(material_path)
        if not isinstance(material, unreal.MaterialInstanceConstant):
            _warn("blok '{0}': {1} nije MaterialInstanceConstant - preskacem".format(block_name, material_path))
            continue

        top_tex = MEL.get_material_instance_texture_parameter_value(material, "Top")
        side_tex = MEL.get_material_instance_texture_parameter_value(material, "Side")
        if top_tex is None or side_tex is None:
            _fail("blok '{0}': MI nema 'Top'/'Side' texture parametre - preskacem".format(block_name))
            continue

        top_pixels = _read_texture_pixels(top_tex, tmp_dir, kismet_rendering)
        side_pixels = _read_texture_pixels(side_tex, tmp_dir, kismet_rendering)
        if top_pixels is None or side_pixels is None:
            continue

        canvas = _compose_icon(top_pixels, side_pixels)

        texture_name = "T_Item_{0}".format(block_name)
        png_path = os.path.join(tmp_dir, texture_name + ".png")
        _write_png(png_path, canvas)
        if DEBUG_EXPORT_PNG:
            shutil.copyfile(png_path, os.path.join(debug_dir, texture_name + ".png"))

        if _import_png_as_texture(png_path, texture_name):
            _generated.append(texture_name)
            _info("{0}  <-  Top={1}, Side={2}".format(
                texture_name, top_tex.get_name(), side_tex.get_name()))

    _info("=== gotovo: {0} generirano, {1} gresaka ===".format(len(_generated), len(_failures)))


main()
