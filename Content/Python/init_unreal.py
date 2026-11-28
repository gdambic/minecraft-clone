"""
init_unreal.py
==============

PythonScriptPlugin sam pokrece ovu datoteku pri svakom pokretanju editora.

Registrira stavke:
- **Tools > MinecraftClone > Build Block Materials** - pokrece Scripts/build_block_materials.py
- **Tools > MinecraftClone > Generate Items Sprites** - pokrece Scripts/generate_item_sprites.py
- **Tools > MinecraftClone > Import Block Sounds** - pokrece Scripts/import_block_sounds.py
- **Tools > MinecraftClone > Import Block Cracks** - pokrece Scripts/import_block_cracks.py
- **Tools > MinecraftClone > Import Underground Blocks** - pokrece Scripts/import_underground_assets.py
- **Tools > MinecraftClone > Import Torch** - pokrece Scripts/import_torch_assets.py
- **Tools > MinecraftClone > Build Water Material** - pokrece Scripts/build_water_material.py
- **Tools > MinecraftClone > Import Zombie** - pokrece Scripts/import_zombie.py

Time otpada lijepljenje exec(open(...)) u Python konzolu.

Datoteka mora ostati u Content/Python/ - plugin trazi bas tu putanju.
"""

import unreal

MENU_NAME = "LevelEditor.MainMenu.Tools"
SECTION_NAME = "MinecraftClone"

SCRIPT_PATH = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/build_block_materials.py")

SCRIPT_PATH_ITEM_SPRITES = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/generate_item_sprites.py")

SCRIPT_PATH_BLOCK_SOUNDS = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/import_block_sounds.py")

SCRIPT_PATH_BLOCK_CRACKS = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/import_block_cracks.py")

SCRIPT_PATH_WATER_MATERIAL = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/build_water_material.py")

SCRIPT_PATH_ZOMBIE = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/import_zombie.py")

SCRIPT_PATH_UNDERGROUND = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/import_underground_assets.py")

SCRIPT_PATH_TORCH = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/import_torch_assets.py")


def _register_menu():
    menus = unreal.ToolMenus.get()
    if menus is None:
        return False

    # extend_menu, a ne find_menu: u trenutku pokretanja ove skripte izbornici
    # editora jos ne moraju postojati. extend_menu registrira prosirenje koje se
    # primijeni kad se izbornik stvarno izgradi.
    menu = menus.extend_menu(MENU_NAME)
    if menu is None:
        return False

    menu.add_section(SECTION_NAME, "MinecraftClone")

    entry = unreal.ToolMenuEntry(
        name="BuildBlockMaterials",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry.set_label("Build Block Materials")
    entry.set_tool_tip(
        "Izgradi M_VoxelBlock, M_VoxelBlock_Masked i MI_* instance iz tekstura "
        "u Content/Blocks/Textures")
    # exec datoteke, a ne import modula - nema cacheiranja pa svaki klik cita
    # skriptu s diska i vidi zadnje izmjene bez restarta editora.
    entry.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH))
    menu.add_menu_entry(SECTION_NAME, entry)

    entry_item_sprites = unreal.ToolMenuEntry(
        name="GenerateItemSprites",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_item_sprites.set_label("Generate Items Sprites")
    entry_item_sprites.set_tool_tip(
        "Renderiraj izometrijske ikone blok-itema iz Items.json 'display' polja "
        "u Content/Items/Generated")
    entry_item_sprites.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_ITEM_SPRITES))
    menu.add_menu_entry(SECTION_NAME, entry_item_sprites)

    entry_block_sounds = unreal.ToolMenuEntry(
        name="ImportBlockSounds",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_block_sounds.set_label("Import Block Sounds")
    entry_block_sounds.set_tool_tip(
        "Uvezi sampleove kopanja/loma blokova iz Saved/MinecraftSounds u "
        "Content/Audio/Blocks (izvuce ih Scripts/extract_minecraft_sounds.py)")
    entry_block_sounds.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_BLOCK_SOUNDS))
    menu.add_menu_entry(SECTION_NAME, entry_block_sounds)

    entry_block_cracks = unreal.ToolMenuEntry(
        name="ImportBlockCracks",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_block_cracks.set_label("Import Block Cracks")
    entry_block_cracks.set_tool_tip(
        "Izvuci destroy_stage_0..9 iz Minecraft jara, uvezi ih u "
        "Content/Blocks/Crack i izgradi M_BlockCrack")
    entry_block_cracks.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_BLOCK_CRACKS))
    menu.add_menu_entry(SECTION_NAME, entry_block_cracks)

    entry_water_material = unreal.ToolMenuEntry(
        name="BuildWaterMaterial",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_water_material.set_label("Build Water Material")
    entry_water_material.set_tool_tip(
        "Izgradi materijal vode M_Water i instancu MI_Water u Content/Water")
    entry_water_material.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_WATER_MATERIAL))
    menu.add_menu_entry(SECTION_NAME, entry_water_material)

    entry_zombie = unreal.ToolMenuEntry(
        name="ImportZombie",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_zombie.set_label("Import Zombie")
    entry_zombie.set_tool_tip(
        "Izvuci zombie.png iz Minecraft jara, izgradi model i animacije u "
        "Blenderu i uvezi ih u Content/Blueprints/MOBS/Zombie")
    entry_zombie.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_ZOMBIE))
    menu.add_menu_entry(SECTION_NAME, entry_zombie)

    entry_underground = unreal.ToolMenuEntry(
        name="ImportUndergroundBlocks",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_underground.set_label("Import Underground Blocks")
    entry_underground.set_tool_tip(
        "Izvuci teksture kamena, ruda i njihovih dropova iz Minecraft jara, "
        "uvezi ih i izgradi materijale i ikone (Docs/PLAN_Underground.md)")
    entry_underground.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_UNDERGROUND))
    menu.add_menu_entry(SECTION_NAME, entry_underground)

    entry_torch = unreal.ToolMenuEntry(
        name="ImportTorch",
        type=unreal.MultiBlockType.MENU_ENTRY)
    entry_torch.set_label("Import Torch")
    entry_torch.set_tool_tip(
        "Izvuci teksturu baklje iz Minecraft jara, uvezi je i izgradi materijale "
        "blokova sa svjetlom (Docs/PLAN_Torches.md)")
    entry_torch.set_string_command(
        unreal.ToolMenuStringCommandType.PYTHON, "",
        'exec(open(r"{0}").read())'.format(SCRIPT_PATH_TORCH))
    menu.add_menu_entry(SECTION_NAME, entry_torch)

    menus.refresh_all_widgets()
    return True


try:
    if _register_menu():
        unreal.log("[MinecraftClone] Tools > MinecraftClone > Build Block Materials / Generate Items Sprites / Import Block Sounds / Import Block Cracks / Build Water Material / Import Zombie / Import Underground Blocks / Import Torch registrirano")
    else:
        unreal.log("[MinecraftClone] ToolMenus nedostupan (headless) - izbornik preskocen")
except Exception as error:
    unreal.log_warning("[MinecraftClone] registracija izbornika nije uspjela: {0}".format(error))
