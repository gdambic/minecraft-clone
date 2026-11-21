"""
import_block_sounds.py
======================

Uveze sampleove zvukova blokova iz `Saved/MinecraftSounds/` (izvuce ih
`Scripts/extract_minecraft_sounds.py`) u `/Game/Audio/Blocks/<Grupa>/` kao
USoundWave assetove i napravi attenuation asset `/Game/Audio/SA_BlockSounds`.

Imena assetova su ono sto trazi `Content/Data/BlockSounds.json`:
`Hit_1..N` i `Break_1..N` po grupi. Place event koristi iste sampleove kao
break (tako radi i vanilla), pa se nista dodatno ne uvozi.

Pokretanje iz editora:   Window > Developer Tools > Output Log > Python
                         exec(open(r"C:/RVS/C++GameDev/MinecraftClone/Scripts/import_block_sounds.py").read())

Pokretanje headless:     UnrealEditor-Cmd.exe MinecraftClone.uproject
                         -run=pythonscript -script="Scripts/import_block_sounds.py"

Skripta je idempotentna - ponovni import gazi postojeci asset, pa reference
iz BlockSounds.json prezive.
"""

import os

import unreal

SOURCE_DIR = os.path.join(
    unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),
    "Saved", "MinecraftSounds")

AUDIO_ROOT = "/Game/Audio"
BLOCKS_ROOT = AUDIO_ROOT + "/Blocks"
ATTENUATION_NAME = "SA_BlockSounds"

# Domet zvuka bloka - vanilla cuje blok ~16 blokova daleko (1 blok = 100 uu).
ATTENUATION_FALLOFF = 1600.0
ATTENUATION_INNER_RADIUS = 100.0

# Grupa -> (hit sampleovi, break sampleovi), putanje relativno na SOURCE_DIR.
# Hit koristi "step" datoteke, break "dig" - tako su posloženi i originali.
SOUND_GROUPS = {
    "Stone": {
        "hit": ["step/stone{0}.ogg".format(i) for i in range(1, 7)],
        "break": ["dig/stone{0}.ogg".format(i) for i in range(1, 5)],
    },
    "Wood": {
        "hit": ["step/wood{0}.ogg".format(i) for i in range(1, 7)],
        "break": ["dig/wood{0}.ogg".format(i) for i in range(1, 5)],
    },
    "Gravel": {
        "hit": ["step/gravel{0}.ogg".format(i) for i in range(1, 5)],
        "break": ["dig/gravel{0}.ogg".format(i) for i in range(1, 5)],
    },
    "Grass": {
        "hit": ["step/grass{0}.ogg".format(i) for i in range(1, 7)],
        "break": ["dig/grass{0}.ogg".format(i) for i in range(1, 5)],
    },
    "Basalt": {
        "hit": ["block/basalt/step{0}.ogg".format(i) for i in range(1, 7)],
        "break": ["block/basalt/break{0}.ogg".format(i) for i in range(1, 6)],
    },
}


def import_sound(source_file, package_path, asset_name):
    """Uveze jedan .ogg u /Game/... kao USoundWave. Vraca True ako je uspjelo."""
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source_file)
    task.set_editor_property("destination_path", package_path)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return len(task.get_editor_property("imported_object_paths")) > 0


def import_group(group, events):
    imported = 0
    package_path = "{0}/{1}".format(BLOCKS_ROOT, group)

    for event, prefix in (("hit", "Hit"), ("break", "Break")):
        for index, relative in enumerate(events[event], start=1):
            source_file = os.path.join(SOURCE_DIR, relative.replace("/", os.sep))
            if not os.path.isfile(source_file):
                unreal.log_warning(
                    "[BlockSounds] nema izvorne datoteke {0} - preskacem".format(source_file))
                continue

            asset_name = "{0}_{1}".format(prefix, index)
            if import_sound(source_file, package_path, asset_name):
                imported += 1
            else:
                unreal.log_warning(
                    "[BlockSounds] import nije uspio: {0}/{1}".format(package_path, asset_name))

    return imported


def build_attenuation():
    """Jedan attenuation asset za sve zvukove blokova (sferni falloff)."""
    asset_path = "{0}/{1}".format(AUDIO_ROOT, ATTENUATION_NAME)

    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        attenuation = unreal.EditorAssetLibrary.load_asset(asset_path)
    else:
        attenuation = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            ATTENUATION_NAME, AUDIO_ROOT, unreal.SoundAttenuation,
            unreal.SoundAttenuationFactory())

    if attenuation is None:
        unreal.log_warning("[BlockSounds] attenuation asset nije napravljen - zvuk ce biti bez prostora")
        return False

    # Sferni oblik: X komponenta extenta je radijus pune glasnoce, a
    # falloff_distance je udaljenost preko koje zvuk padne u tisinu.
    settings = attenuation.get_editor_property("attenuation")
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents",
                                 unreal.Vector(ATTENUATION_INNER_RADIUS, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", ATTENUATION_FALLOFF)
    attenuation.set_editor_property("attenuation", settings)

    unreal.EditorAssetLibrary.save_loaded_asset(attenuation)
    return True


def main():
    if not os.path.isdir(SOURCE_DIR):
        unreal.log_error(
            "[BlockSounds] nema {0} - pokreni prvo Scripts/extract_minecraft_sounds.py".format(SOURCE_DIR))
        return

    total = 0
    for group in sorted(SOUND_GROUPS):
        count = import_group(group, SOUND_GROUPS[group])
        unreal.log("[BlockSounds] {0}: {1} sampleova".format(group, count))
        total += count

    build_attenuation()
    unreal.log("[BlockSounds] gotovo - ukupno {0} sampleova u {1}".format(total, BLOCKS_ROOT))


main()
