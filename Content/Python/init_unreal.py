"""
init_unreal.py
==============

PythonScriptPlugin sam pokrece ovu datoteku pri svakom pokretanju editora.

Registrira stavku **Tools > MinecraftClone > Build Block Materials** koja pokrece
Scripts/build_block_materials.py. Time otpada lijepljenje exec(open(...)) u Python
konzolu.

Datoteka mora ostati u Content/Python/ - plugin trazi bas tu putanju.
"""

import unreal

MENU_NAME = "LevelEditor.MainMenu.Tools"
SECTION_NAME = "MinecraftClone"

SCRIPT_PATH = unreal.Paths.convert_relative_path_to_full(
    unreal.Paths.project_dir() + "Scripts/build_block_materials.py")


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

    menus.refresh_all_widgets()
    return True


try:
    if _register_menu():
        unreal.log("[MinecraftClone] Tools > MinecraftClone > Build Block Materials registriran")
    else:
        unreal.log("[MinecraftClone] ToolMenus nedostupan (headless) - izbornik preskocen")
except Exception as error:
    unreal.log_warning("[MinecraftClone] registracija izbornika nije uspjela: {0}".format(error))
