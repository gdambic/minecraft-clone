"""
build_zombie_blender.py
=======================

Izgradi Minecraft zombija u Blenderu: mesh od sedam kutija, kostur od
sedam kostiju i pet animacija, pa sve izveze kao FBX za Unreal
(Docs/PLAN_Zombie.md). Pokrece ga Scripts/import_zombie.py, a moze i rucno:

    blender -b --factory-startup -P Scripts/build_zombie_blender.py -- <zombie.png> <izlazni dir>

Izlaz u <izlazni dir>:
    Zombie.blend            - scena za pregled i rucno dotjerivanje
    SK_Zombie.fbx           - mesh + kostur, bez animacije
    A_Zombie_<Ime>.fbx      - po jedna animacija (samo kostur)

Sve brojke procitane su iz Minecraft 1.21.11 klijentskog jara (javap uz
sluzbene Mojang mappings):

- Geometrija je HumanoidModel.createMesh(CubeDeformation.NONE, 0) s teksturom
  64x64 (LayerDefinitions, ModelLayers.ZOMBIE). Lijeva ruka i noga koriste
  zrcaljeni UV desne (texOffs 40,16 i 0,16 + mirror()).
- UV svake plohe kopira ModelPart.Cube: redoslijed vrhova, UV pravokutnici i
  zrcaljenje (zamjena minX/maxX + obrnut redoslijed vrhova).
- Animacije su HumanoidModel.setupAnim + AnimationUtils.animateZombieArms/
  bobArms + setupAttackAnimation (WHACK, 6 tickova) + LivingEntityRenderer.
  setupRotations (smrt).

Koordinate: Minecraft model ima Y prema dolje, ishodiste 24 px iznad tla,
lice prema -Z i desnu stranu na -X. Ovdje lik gleda prema -Y (Unreal ga
uveze gledajuci prema +Y, kao mannequin), desna strana je -X, Z je gore.
Model (x, y, z) -> Blender (x, z, 24 - y) piksela; 1 px = 6.25 cm, pa je
zombi visok 200 UU.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

PX = 6.25            # 1 piksel modela u cm (1 blok = 16 px = 100 cm)
TEX = 64             # tekstura 64x64
MODEL_ORIGIN_Y = 24  # model y koji pada na tlo
FPS = 60             # gusto uzorkovanje, UE interpolira linearno izmedu kljuceva
TICK = 1.0 / 20.0    # Minecraft tick u sekundama

# ime, kost, texOffs, box origin (x,y,z), velicina (w,h,d), pivot, mirror, grow
PARTS = [
    ("head",      "head",      (0, 0),   (-4, -8, -4), (8, 8, 8),  (0, 0, 0),     False, 0.0),
    ("hat",       "head",      (32, 0),  (-4, -8, -4), (8, 8, 8),  (0, 0, 0),     False, 0.5),
    ("body",      "body",      (16, 16), (-4, 0, -2),  (8, 12, 4), (0, 0, 0),     False, 0.0),
    ("right_arm", "right_arm", (40, 16), (-3, -2, -2), (4, 12, 4), (-5, 2, 0),    False, 0.0),
    ("left_arm",  "left_arm",  (40, 16), (-1, -2, -2), (4, 12, 4), (5, 2, 0),     True,  0.0),
    ("right_leg", "right_leg", (0, 16),  (-2, 0, -2),  (4, 12, 4), (-1.9, 12, 0), False, 0.0),
    ("left_leg",  "left_leg",  (0, 16),  (-2, 0, -2),  (4, 12, 4), (1.9, 12, 0),  True,  0.0),
]

BONES = ["head", "body", "right_arm", "left_arm", "right_leg", "left_leg"]
PIVOTS = {p[1]: p[5] for p in PARTS}


def model_to_blender(x, y, z):
    return Vector((x * PX, z * PX, (MODEL_ORIGIN_Y - y) * PX))


def mc_rotation(x_rot, y_rot, z_rot):
    """ModelPart rotira rotationZYX(z, y, x) u prostoru modela. Model -> Blender
    je rotacija osi (x->x, y->-z, z->y), pa os Y modela postaje -Z, a Z postaje Y."""
    return (Quaternion((0, 1, 0), z_rot)
            @ Quaternion((0, 0, 1), -y_rot)
            @ Quaternion((1, 0, 0), x_rot))


# ---------------------------------------------------------------------------
# Tekstura
# ---------------------------------------------------------------------------

def load_texture(path):
    image = bpy.data.images.load(path)
    if tuple(image.size) != (TEX, TEX):
        raise RuntimeError("zombie.png je {0}, ocekivano {1}x{1}".format(tuple(image.size), TEX))
    return image


def region_has_pixels(image, u, v, w, h):
    """Ima li pravokutnik (u, v, w, h) u pikselima slike ijedan neprozirni piksel."""
    pixels = image.pixels[:]
    for py in range(v, v + h):
        row = TEX - 1 - py   # Blender drzi redove odozdo
        for px in range(u, u + w):
            if pixels[(row * TEX + px) * 4 + 3] > 0.1:
                return True
    return False


# ---------------------------------------------------------------------------
# Mesh (ModelPart.Cube)
# ---------------------------------------------------------------------------

def cube_faces(uv, origin, size, mirror, grow):
    """Vrati listu ploha: (smjer, [(pozicija modela, (u, v) u pikselima)] x4)."""
    u, v = uv
    x, y, z = origin
    w, h, d = size
    min_x, min_y, min_z = x - grow, y - grow, z - grow
    max_x, max_y, max_z = x + w + grow, y + h + grow, z + d + grow
    if mirror:
        min_x, max_x = max_x, min_x

    p = {
        19: (min_x, min_y, min_z), 20: (max_x, min_y, min_z),
        21: (max_x, max_y, min_z), 22: (min_x, max_y, min_z),
        23: (min_x, min_y, max_z), 24: (max_x, min_y, max_z),
        25: (max_x, max_y, max_z), 26: (min_x, max_y, max_z),
    }
    u0, u1, u2 = u, u + d, u + d + w
    u3, u4, u5 = u + d + w + w, u + d + w + d, u + d + w + d + w
    v0, v1, v2 = v, v + d, v + d + h

    faces = [
        ("DOWN",  (24, 23, 19, 20), u1, v0, u2, v1),
        ("UP",    (21, 22, 26, 25), u2, v1, u3, v0),
        ("WEST",  (19, 23, 26, 22), u0, v1, u1, v2),
        ("NORTH", (20, 19, 22, 21), u1, v1, u2, v2),
        ("EAST",  (24, 20, 21, 25), u2, v1, u4, v2),
        ("SOUTH", (23, 24, 25, 26), u4, v1, u5, v2),
    ]
    result = []
    for direction, ids, a_u, a_v, b_u, b_v in faces:
        # ModelPart.Polygon: [0]=(u2,v1) [1]=(u1,v1) [2]=(u1,v2) [3]=(u2,v2)
        uvs = [(b_u, a_v), (a_u, a_v), (a_u, b_v), (b_u, b_v)]
        verts = [(p[i], uvs[k]) for k, i in enumerate(ids)]
        if mirror:
            verts.reverse()
        result.append((direction, verts))
    return result


def build_mesh(image, include_hat):
    mesh = bpy.data.meshes.new("SK_Zombie")
    obj = bpy.data.objects.new("SK_Zombie", mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")
    deform = bm.verts.layers.deform.verify()
    bone_index = {name: i for i, name in enumerate(BONES)}
    for name in BONES:
        obj.vertex_groups.new(name=name)

    for name, bone, uv, origin, size, pivot, mirror, grow in PARTS:
        if name == "hat" and not include_hat:
            continue
        center = Vector(model_to_blender(
            pivot[0] + origin[0] + size[0] / 2,
            pivot[1] + origin[1] + size[1] / 2,
            pivot[2] + origin[2] + size[2] / 2))
        for direction, verts in cube_faces(uv, origin, size, mirror, grow):
            bm_verts = []
            for (mx, my, mz), _ in verts:
                vert = bm.verts.new(model_to_blender(pivot[0] + mx, pivot[1] + my, pivot[2] + mz))
                vert[deform][bone_index[bone]] = 1.0
                bm_verts.append(vert)
            face = bm.faces.new(bm_verts)
            face.normal_update()
            face_center = sum((vv.co for vv in bm_verts), Vector()) / 4
            # Minecraft definira redoslijed u svom (zrcaljenom) prostoru; ovdje
            # samo osiguramo da normala gleda van, UV ostaje vezan uz vrh.
            if face.normal.dot(face_center - center) < 0:
                face.normal_flip()
            for loop in face.loops:
                k = bm_verts.index(loop.vert)
                tu, tv = verts[k][1]
                loop[uv_layer].uv = (tu / TEX, 1.0 - tv / TEX)

    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = False

    material = bpy.data.materials.new("M_Zombie")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    tex_node = nodes.new("ShaderNodeTexImage")
    tex_node.image = image
    tex_node.interpolation = "Closest"
    bsdf = nodes.get("Principled BSDF")
    material.node_tree.links.new(tex_node.outputs["Color"], bsdf.inputs["Base Color"])
    material.node_tree.links.new(tex_node.outputs["Alpha"], bsdf.inputs["Alpha"])
    mesh.materials.append(material)
    return obj


# ---------------------------------------------------------------------------
# Kostur
# ---------------------------------------------------------------------------

BONE_LENGTH = 25.0  # cm, samo za prikaz; kosti gledaju prema +Y pa im je rest rotacija jedinicna


def build_armature():
    data = bpy.data.armatures.new("Armature")
    # Ime "Armature" UE importer preskoci kao suvisnu korijensku kost.
    arm = bpy.data.objects.new("Armature", data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")

    root = data.edit_bones.new("root")
    root.head = (0, 0, 0)
    root.tail = (0, BONE_LENGTH, 0)
    root.roll = 0
    for name in BONES:
        bone = data.edit_bones.new(name)
        head = model_to_blender(*PIVOTS[name])
        bone.head = head
        bone.tail = head + Vector((0, BONE_LENGTH, 0))
        bone.roll = 0
        bone.parent = root
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm


def bind(mesh_obj, arm):
    mesh_obj.parent = arm
    modifier = mesh_obj.modifiers.new("Armature", "ARMATURE")
    modifier.object = arm


# ---------------------------------------------------------------------------
# Poze (Minecraft formule)
# ---------------------------------------------------------------------------

ZOMBIE_ARMS_PASSIVE = -math.pi / 2.25
ZOMBIE_ARMS_AGGRESSIVE = -math.pi / 1.5

# bobModelPart: zRot += s*(cos(age*0.09)*0.05+0.05), xRot += s*sin(age*0.067)*0.05.
# Petlja od 280 tickova (14 s) treba cijeli broj perioda: 0.09 -> 4 perioda
# (0.0898), 0.067 -> 3 perioda (0.0673). Odstupanje frekvencija je ispod 0.5 %.
IDLE_LOOP_TICKS = 280
BOB_Z_FREQ = 2 * math.pi * 4 / IDLE_LOOP_TICKS
BOB_X_FREQ = 2 * math.pi * 3 / IDLE_LOOP_TICKS

WALK_FREQ = 0.6662   # HumanoidModel: cos(walkPos * 0.6662) * 1.4 * walkSpeed
WALK_AMPLITUDE = 1.4
WALK_LOOP_SECONDS = 0.5  # jedan period hoda; C++ pretvara walkPos u vrijeme animacije

ATTACK_TICKS = 6     # SwingAnimation default: WHACK, 6 tickova
DEATH_TICKS = 20     # LivingEntity.tickDeath: uklanja entitet na deathTime >= 20


def rest_pose():
    """Rotacije (x, y, z) i pomak pivota (dx, dy, dz) u prostoru modela po kosti."""
    pose = {name: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0] for name in BONES}
    # HumanoidModel.setupAnim: noge uvijek imaju yRot/zRot +-0.005
    pose["right_leg"][1] = 0.005
    pose["right_leg"][2] = 0.005
    pose["left_leg"][1] = -0.005
    pose["left_leg"][2] = -0.005
    return pose


def zombie_arms(pose, aggressive, attack_time):
    """AnimationUtils.animateZombieArms."""
    base = ZOMBIE_ARMS_AGGRESSIVE if aggressive else ZOMBIE_ARMS_PASSIVE
    f1 = math.sin(attack_time * math.pi)
    f2 = math.sin((1.0 - (1.0 - attack_time) * (1.0 - attack_time)) * math.pi)
    x_rot = base + f1 * 1.2 - f2 * 0.4
    pose["right_arm"][0:3] = [x_rot, -(0.1 - f1 * 0.6), 0.0]
    pose["left_arm"][0:3] = [x_rot, 0.1 - f1 * 0.6, 0.0]


def bob_arms(pose, age):
    """AnimationUtils.bobArms s frekvencijama prilagodenima petlji."""
    for name, scale in (("right_arm", 1.0), ("left_arm", -1.0)):
        pose[name][2] += scale * (math.cos(age * BOB_Z_FREQ) * 0.05 + 0.05)
        pose[name][0] += scale * math.sin(age * BOB_X_FREQ) * 0.05


def attack_body(pose, attack_time):
    """HumanoidModel.setupAttackAnimation: trup se zakrene, ramena idu s njim."""
    body_y = math.sin(math.sqrt(attack_time) * 2 * math.pi) * 0.2
    pose["body"][1] = body_y
    # rightArm.x = -cos*5, z = sin*5; leftArm.x = cos*5, z = -sin*5 (rest je x=-5/+5, z=0)
    pose["right_arm"][3] = -math.cos(body_y) * 5 - PIVOTS["right_arm"][0]
    pose["right_arm"][5] = math.sin(body_y) * 5
    pose["left_arm"][3] = math.cos(body_y) * 5 - PIVOTS["left_arm"][0]
    pose["left_arm"][5] = -math.sin(body_y) * 5


def pose_idle(aggressive):
    def sample(t):
        age = t / TICK
        pose = rest_pose()
        zombie_arms(pose, aggressive, 0.0)
        bob_arms(pose, age)
        return pose, 0.0
    return sample


def pose_walk(t):
    phase = t / WALK_LOOP_SECONDS * 2 * math.pi
    pose = rest_pose()
    pose["right_leg"][0] = math.cos(phase) * WALK_AMPLITUDE
    pose["left_leg"][0] = math.cos(phase + math.pi) * WALK_AMPLITUDE
    return pose, 0.0


def pose_attack(t):
    attack_time = min(1.0, t / (ATTACK_TICKS * TICK))
    pose = rest_pose()
    if attack_time < 1.0:
        attack_body(pose, attack_time)
        zombie_arms(pose, True, attack_time)
    else:
        zombie_arms(pose, True, 0.0)
    return pose, 0.0


def pose_death(t):
    # setupRotations: f = sqrt((deathTime - 1) / 20 * 1.6), najvise 1, rotacija Z za f * 90
    death_time = t / TICK
    f = math.sqrt(max(0.0, death_time - 1.0) / 20.0 * 1.6)
    f = min(1.0, f)
    pose = rest_pose()
    zombie_arms(pose, False, 0.0)
    return pose, f * math.pi / 2


ANIMATIONS = [
    # ime, funkcija poze, trajanje u sekundama, petlja
    ("Idle",           pose_idle(False), IDLE_LOOP_TICKS * TICK, True),
    ("IdleAggressive", pose_idle(True),  IDLE_LOOP_TICKS * TICK, True),
    ("Walk",           pose_walk,        WALK_LOOP_SECONDS,      True),
    ("Attack",         pose_attack,      ATTACK_TICKS * TICK,    False),
    ("Death",          pose_death,       DEATH_TICKS * TICK,     False),
]


def apply_pose(arm, pose, root_roll):
    for name in BONES:
        x_rot, y_rot, z_rot, dx, dy, dz = pose[name]
        pb = arm.pose.bones[name]
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = mc_rotation(x_rot, y_rot, z_rot)
        # Pomak pivota iz prostora modela; rest rotacija kosti je jedinicna.
        pb.location = Vector((dx * PX, dz * PX, -dy * PX))
    root = arm.pose.bones["root"]
    root.rotation_mode = "QUATERNION"
    # Smrt: lik padne na svoj lijevi bok (+X), rotacija oko osi pogleda.
    root.rotation_quaternion = Quaternion((0, 1, 0), root_roll)
    root.location = Vector((0, 0, 0))


def bake_action(arm, name, sample, duration, loop):
    action = bpy.data.actions.new("Zombie_" + name)
    arm.animation_data_create()
    arm.animation_data.action = action
    frames = int(round(duration * FPS))
    last = frames if not loop else frames  # petlja: zadnji kljuc = prvi, UE ga koristi kao kraj
    for frame in range(0, last + 1):
        t = frame / FPS
        if loop and frame == frames:
            t = 0.0
        pose, root_roll = sample(t)
        apply_pose(arm, pose, root_roll)
        for pb in arm.pose.bones:
            pb.keyframe_insert("rotation_quaternion", frame=frame)
            pb.keyframe_insert("location", frame=frame)
    for fcurve in _action_fcurves(action):
        for key in fcurve.keyframe_points:
            key.interpolation = "LINEAR"
    action.use_fake_user = True
    return action, frames


def _action_fcurves(action):
    # Blender 4.4+ slotted actions: fcurve-ovi su u layers/strips/channelbags.
    if hasattr(action, "fcurves") and len(action.fcurves):
        return list(action.fcurves)
    curves = []
    for layer in getattr(action, "layers", []):
        for strip in layer.strips:
            for bag in strip.channelbags:
                curves.extend(bag.fcurves)
    return curves


# ---------------------------------------------------------------------------
# Izvoz
# ---------------------------------------------------------------------------

def export_fbx(path, objects, bake_anim):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={o.type for o in objects},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",
        global_scale=1.0,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        armature_nodetype="NULL",
        use_armature_deform_only=False,
        mesh_smooth_type="FACE",
        path_mode="STRIP",
        bake_anim=bake_anim,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
    )


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) != 2:
        raise SystemExit("upotreba: blender -b -P build_zombie_blender.py -- <zombie.png> <izlazni dir>")
    texture_path, out_dir = (os.path.abspath(a) for a in argv)
    os.makedirs(out_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01   # 1 Blender jedinica = 1 cm = 1 UU
    scene.render.fps = FPS
    scene.render.fps_base = 1.0

    image = load_texture(texture_path)
    include_hat = region_has_pixels(image, 32, 0, 32, 16)
    print("[ZOMBIE] sesir (texOffs 32,0) {0}".format(
        "ima piksele - ukljucen" if include_hat else "je proziran - preskocen"))

    mesh_obj = build_mesh(image, include_hat)
    arm = build_armature()
    bind(mesh_obj, arm)
    print("[ZOMBIE] mesh: {0} vrhova, {1} ploha".format(
        len(mesh_obj.data.vertices), len(mesh_obj.data.polygons)))

    export_fbx(os.path.join(out_dir, "SK_Zombie.fbx"), [arm, mesh_obj], bake_anim=False)

    for name, sample, duration, loop in ANIMATIONS:
        action, frames = bake_action(arm, name, sample, duration, loop)
        scene.frame_start = 0
        scene.frame_end = frames
        export_fbx(os.path.join(out_dir, "A_Zombie_{0}.fbx".format(name)), [arm], bake_anim=True)
        print("[ZOMBIE] animacija {0}: {1} frameova @ {2} fps ({3:.3f} s)".format(
            name, frames, FPS, frames / FPS))

    arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.rotation_quaternion = Quaternion()
        pb.location = Vector()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out_dir, "Zombie.blend"))
    print("[ZOMBIE] GOTOVO: " + out_dir)


if __name__ == "__main__":
    main()
