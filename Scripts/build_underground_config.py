"""
build_underground_config.py
===========================

Iz klijentskog jara lokalne Minecraft Java instalacije procita nakupine kamena
i rude (data/minecraft/worldgen/placed_feature + configured_feature) i upise ih
u Content/Data/WorldGen/Underground.json kao "features", zajedno sa slojevima
("layers": Bedrock na dnu, Deepslate u dubini). Dio "caves" ostaje netaknut.

Obican Python, bez editora:

    python Scripts/build_underground_config.py

Redoslijed nakupina je redoslijed kojim ih Minecraft postavlja u biomu plains
(korak underground_ores u worldgen/biome/plains.json); Emerald dolazi iz
stony_peaks i ogranicen je na nas biom StonyPeaks.

Visine se preracunavaju iz Minecraftovog Y u nas Z: razina mora je kod njih 63,
kod nas 0, pa je Z = Y - 63. Dno Minecraftovog svijeta (Y = -64) je Z = -127.

Skripta pregazi "layers" i "features" - rucne izmjene u tim dijelovima se gube.
Nova ruda koja nije iz Minecrafta dodaje se u EXTRA_FEATURES dolje ili rucno u
JSON (i tada se ova skripta vise ne pokrece).
"""

import json
import os
import re
import sys
import zipfile

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.path.join(PROJECT_DIR, "Content", "Data", "WorldGen", "Underground.json")
MINECRAFT_VERSIONS_DIR = os.path.join(os.environ.get("APPDATA", ""), ".minecraft", "versions")

SEA_LEVEL_Y = 63
MIN_Y = -64
MAX_Y = 319

WORLDGEN = "data/minecraft/worldgen/"
TAGS = "data/minecraft/tags/block/"

# Nakupine koje ne prenosimo: infested_stone (srebrne ribice)
SKIPPED_FEATURES = {"ore_infested"}

# (biom u jaru, nas biom ili None = svi biomi)
BIOME_SOURCES = [("plains", None), ("stony_peaks", "StonyPeaks")]

# Rucno dodane nakupine (isti oblik kao unosi u "features")
EXTRA_FEATURES = []


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


def block_name(minecraft_id):
    """minecraft:deepslate_coal_ore -> DeepslateCoalOre (ime u EBlockType)"""
    return "".join(part.capitalize() for part in minecraft_id.split(":")[-1].split("_"))


def anchor_to_z(anchor):
    if "absolute" in anchor:
        y = anchor["absolute"]
    elif "above_bottom" in anchor:
        y = MIN_Y + anchor["above_bottom"]
    else:
        y = MAX_Y - anchor["below_top"]
    return y - SEA_LEVEL_Y


def convert_feature(jar, name, biome):
    placed = json.loads(jar.read(WORLDGEN + "placed_feature/" + name + ".json"))
    configured_name = placed["feature"].split(":")[-1]
    configured = json.loads(jar.read(WORLDGEN + "configured_feature/" + configured_name + ".json"))
    if configured["type"] != "minecraft:ore":
        return None

    feature = {"name": name}
    for modifier in placed["placement"]:
        kind = modifier["type"].split(":")[-1]
        if kind == "count":
            count = modifier["count"]
            if isinstance(count, dict):
                feature["attemptsMin"] = count["min_inclusive"]
                feature["attemptsMax"] = count["max_inclusive"]
            else:
                feature["attempts"] = count
        elif kind == "rarity_filter":
            feature["oneInChunks"] = modifier["chance"]
        elif kind == "height_range":
            height = modifier["height"]
            height_type = height["type"].split(":")[-1]
            if height_type not in ("uniform", "trapezoid") or height.get("plateau", 0) != 0:
                raise ValueError("{0}: nepodrzana raspodjela visine {1}".format(name, height))
            feature["height"] = {
                "type": "uniform" if height_type == "uniform" else "triangle",
                "min": anchor_to_z(height["min_inclusive"]),
                "max": anchor_to_z(height["max_inclusive"]),
            }
        elif kind not in ("in_square", "biome"):
            raise ValueError("{0}: nepodrzan placement {1}".format(name, kind))

    config = configured["config"]
    feature["size"] = config["size"]
    feature["discardOnAirExposure"] = config["discard_chance_on_air_exposure"]
    feature["targets"] = []
    for target in config["targets"]:
        rule = target["target"]
        if "tag" in rule:
            tag = json.loads(jar.read(TAGS + rule["tag"].split(":")[-1] + ".json"))
            replace = [block_name(value) for value in tag["values"]]
        else:
            replace = [block_name(rule["block"])]
        feature["targets"].append({"replace": replace, "block": block_name(target["state"]["Name"])})
    if biome:
        feature["biomes"] = [biome]
    return feature


def main():
    jar_path = find_client_jar()
    if jar_path is None:
        print("nema Minecraft jara u " + MINECRAFT_VERSIONS_DIR)
        return 1

    features = []
    seen = set()
    with zipfile.ZipFile(jar_path) as jar:
        for biome_file, our_biome in BIOME_SOURCES:
            biome = json.loads(jar.read(WORLDGEN + "biome/" + biome_file + ".json"))
            for step in biome["features"]:
                for entry in step:
                    name = entry.split(":")[-1]
                    if not name.startswith("ore_") or name in seen or name in SKIPPED_FEATURES:
                        continue
                    seen.add(name)
                    feature = convert_feature(jar, name, our_biome)
                    if feature:
                        features.append(feature)
    features.extend(EXTRA_FEATURES)

    with open(CONFIG_PATH, "r", encoding="utf-8") as handle:
        config = json.load(handle)

    # Slojevi iz surface_rule u worldgen/noise_settings/overworld.json:
    # bedrock_floor (puno na dnu, nista 5 slojeva iznad) i deepslate
    # (puno na Y <= 0, nista na Y >= 8), s nasumicnim prijelazom izmedu
    config["layers"] = {
        "bedrock": {"block": "Bedrock", "layers": 5},
        "deepslate": {
            "block": "Deepslate",
            "replaces": "Stone",
            "fullAtAndBelow": 0 - SEA_LEVEL_Y,
            "noneAtAndAbove": 8 - SEA_LEVEL_Y,
        },
    }
    config["features"] = features

    with open(CONFIG_PATH, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(config, handle, indent=2)
        handle.write("\n")
    print("{0}: {1} nakupina iz {2}".format(CONFIG_PATH, len(features), jar_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
