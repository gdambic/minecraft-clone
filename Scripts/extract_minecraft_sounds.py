"""
extract_minecraft_sounds.py
===========================

Izvuce zvukove blokova iz lokalne Minecraft Java instalacije u
`Saved/MinecraftSounds/`. Minecraft od 1.8 drzi assetove pod hashom
(`.minecraft/assets/objects/<2 znaka hasha>/<hash>`), a mapiranje
"virtualno ime -> hash" je u `.minecraft/assets/indexes/<verzija>.json`.

Pokretanje (obicni Python, bez editora):

    python Scripts/extract_minecraft_sounds.py

Izlaz zadrzava originalnu strukturu (`dig/`, `step/`, `block/<grupa>/`) i
pise `manifest.csv` s mapiranjem sound eventa na datoteke. Iz tog izlaza
`Scripts/import_block_sounds.py` uvozi sampleove u Content/Audio/Blocks.

Projekt se ne distribuira (ucenje), pa se originalni sampleovi koriste
direktno; `Saved/` je u .gitignore pa nista od toga ne zavrsi u repou.
"""

import csv
import json
import os
import re
import shutil

MINECRAFT_DIR = os.path.join(os.environ.get("APPDATA", ""), ".minecraft")
ASSETS_DIR = os.path.join(MINECRAFT_DIR, "assets")
OBJECTS_DIR = os.path.join(ASSETS_DIR, "objects")

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(PROJECT_DIR, "Saved", "MinecraftSounds")

# Zanimaju nas samo blok eventi; hit/break su za kopanje, place/step dolaze
# "besplatno" jer koriste iste sampleove.
EVENT_PATTERN = re.compile(r"^block\.[a-z_0-9]+\.(hit|break|place|step)$")

SOUND_PREFIX = "minecraft/sounds/"


def newest_index():
    """Putanja do najnovijeg asset indexa (npr. assets/indexes/29.json)."""
    index_dir = os.path.join(ASSETS_DIR, "indexes")
    candidates = [f for f in os.listdir(index_dir) if f.endswith(".json")]
    if not candidates:
        raise SystemExit("Nema nijednog indexa u {0}".format(index_dir))
    candidates.sort(key=lambda f: os.path.getmtime(os.path.join(index_dir, f)))
    return os.path.join(index_dir, candidates[-1])


def object_path(objects, virtual_name):
    digest = objects[virtual_name]["hash"]
    return os.path.join(OBJECTS_DIR, digest[:2], digest)


def resolve_event(sounds_json, event, depth=0):
    """Datoteke koje event svira; `type: event` pokazuje na drugi event."""
    if depth > 4 or event not in sounds_json:
        return []

    files = []
    for entry in sounds_json[event].get("sounds", []):
        if isinstance(entry, str):
            files.append(SOUND_PREFIX + entry + ".ogg")
        elif entry.get("type") == "event":
            files.extend(resolve_event(sounds_json, entry["name"], depth + 1))
        else:
            files.append(SOUND_PREFIX + entry["name"] + ".ogg")
    return files


def main():
    if not os.path.isdir(ASSETS_DIR):
        raise SystemExit("Minecraft assets nisu nadeni: {0}".format(ASSETS_DIR))

    index_file = newest_index()
    objects = json.load(open(index_file, encoding="utf-8"))["objects"]
    sounds_json = json.load(open(object_path(objects, "minecraft/sounds.json"), encoding="utf-8"))

    rows = []
    copied = set()
    missing = []

    for event in sorted(e for e in sounds_json if EVENT_PATTERN.match(e)):
        for virtual_name in resolve_event(sounds_json, event):
            if virtual_name not in objects:
                missing.append((event, virtual_name))
                continue

            relative = virtual_name[len(SOUND_PREFIX):]
            destination = os.path.join(OUTPUT_DIR, relative.replace("/", os.sep))
            if relative not in copied:
                os.makedirs(os.path.dirname(destination), exist_ok=True)
                shutil.copyfile(object_path(objects, virtual_name), destination)
                copied.add(relative)
            rows.append((event, relative))

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(os.path.join(OUTPUT_DIR, "manifest.csv"), "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["sound_event", "file"])
        writer.writerows(rows)

    print("index:", index_file)
    print("eventa: {0}, datoteka: {1}, mapiranja: {2}".format(
        len({r[0] for r in rows}), len(copied), len(rows)))
    if missing:
        print("nedostaje u storeu:", len(missing), missing[:5])
    print("izlaz:", OUTPUT_DIR)


main()
