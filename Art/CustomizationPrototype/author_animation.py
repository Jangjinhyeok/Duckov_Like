"""Blender background 전용: 플레이어 오리의 Idle·Walk 애니메이션을 만든다.

CustomizationParts.blend는 건드리지 않고 새 파생 원본 CharacterAnimation.blend를 만든다.
공통 rig에 발 bone(foot_l/foot_r)을 더하고 발 mesh 두 종류를 그 bone에 weight한 뒤 두 action을 넣는다.
create : 파생 원본을 만들고 저장한다(이미 있으면 거부).
export : 저장된 파생 원본에서 애니메이션 FBX와 bone이 늘어난 body·발 FBX를 내보낸다.
verify : 계약 검사만 한다(bone·weight·루프 연속성·지면 관통·FBX 왕복).
render : 움직임 미리보기 frame PNG를 만든다(Saved/Automation/CharacterAnimation/frames).
"""

import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

sys.dont_write_bytecode = True

ART = Path(__file__).resolve().parent
ROOT = ART.parents[1]
GATE = ROOT / "Saved/Automation/CharacterAnimation"
PARTS = ART / "CustomizationParts.blend"
DERIVED = ART / "CharacterAnimation.blend"
EXPORTS = ART / "Exports"
BODY = "SK_CustomizationBaseBody"
FEET = ("SK_CustomizationFootWebbed", "SK_CustomizationFootBoot")
# 발목 위치의 bone이다. 앞(-Y)을 향하게 두어 발끝 방향과 맞춘다.
FOOT_BONES = (
    ("foot_l", "root", (10, 0, 6), (10, -10, 6)),
    ("foot_r", "root", (-10, 0, 6), (-10, -10, 6)),
)
FPS = 30
# 이름 -> 루프 길이(frame). 마지막 frame은 첫 frame과 같은 pose라 UE에서 이음매 없이 반복된다.
IDLE = ("A_DuckIdle", 60)
WALK = ("A_DuckWalk", 16)
# 총을 장착했을 때의 같은 길이 루프다. 몸·머리·발은 기본 동작과 같고 날개만 앞으로 모아 총을 쥔다.
IDLE_RIFLE = ("A_DuckIdleRifle", 60)
WALK_RIFLE = ("A_DuckWalkRifle", 16)
# 근접 무기를 한 번 휘두르는 동작이다(반복하지 않음). 몸을 비틀며 오른날개를 뒤 위에서 앞으로 크게 휘두른다.
MELEE_SWING = ("A_DuckMeleeSwing", 10)
ONE_SHOTS = (MELEE_SWING,)
ACTIONS = (IDLE, WALK, IDLE_RIFLE, WALK_RIFLE, MELEE_SWING)
# 날개 mesh(Round)는 어깨에서 거의 아래로 늘어져 있다(끝 x ±31.5, z 25). bone 방향(옆으로 14, 아래 6)보다 mesh 방향을 기준으로 돌린다.
WING_HANG = {"wing_l": (9.5, 2.0, -22.0), "wing_r": (-9.5, 2.0, -22.0)}
# 총을 쥔 자세에서 날개 끝이 향하는 점(cm, rest 기준)이다. 오른날개는 손잡이, 왼날개는 총몸 앞쪽을 받친다.
HOLD_TARGET = {"wing_l": (3.0, -30.0, 42.0), "wing_r": (-7.0, -22.0, 40.0)}
# 미리보기 render에만 쓰는 총 모형이다(body bone을 따름). 이름 -> (중심, 크기) cm, rest 기준이며 앞은 -Y다.
# 게임의 총 모형(DuckovCharacter.cpp WeaponParts)은 UE 축(x=-y, y=-x)으로 같은 값을 쓴다. 원본에는 저장하지 않는다.
GUN_PARTS = {
    "PreviewGunStock": ((-3.0, -12.0, 41.0), (5.0, 10.0, 8.0)),
    "PreviewGunBody": ((-3.0, -30.0, 43.0), (6.0, 26.0, 9.0)),
    "PreviewGunMagazine": ((-3.0, -33.0, 35.0), (4.0, 6.0, 10.0)),
    "PreviewGunBarrel": ((-3.0, -57.0, 45.0), (3.0, 28.0, 3.0)),
}
# 애니메이션 FBX만 armature를 Z축으로 이만큼 돌려 내보낸다. UE 5.7의 FBX 애니메이션 import는 mesh import(Force Front X)와 달리
# 애니메이션 전체를 reference pose보다 yaw +90° 돌려 놓았다(armature만 내보내도, mesh를 함께 내보내도 같았다). 2026-10-18.
# 원본 action은 그대로 두고 export용 복사본의 root bone 키에만 넣는다. armature object를 돌리면 UE가 버려 효과가 없었다.
# UE 쪽 검사는 import_animation.py의 첫 frame 비교다.
ANIMATION_EXPORT_YAW = math.pi / 2

spec = importlib.util.spec_from_file_location("existing_author_assets", ART / "author_assets.py")
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)
ALL_BONES = tuple(bone[0] for bone in existing.BONES) + tuple(bone[0] for bone in FOOT_BONES)


def write(name, value):
    GATE.mkdir(parents=True, exist_ok=True)
    (GATE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def rig():
    return bpy.data.objects["Armature"]


def add_foot_bones(armature):
    existing.activate(armature)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, parent, head, tail in FOOT_BONES:
        bone = armature.data.edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        bone.parent = armature.data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")


def reweight_feet():
    # 발 mesh는 좌우 조각이 x 부호로 갈린다(중심 ±10 cm, 폭 8 cm). root weight를 발 bone으로 옮긴다.
    for name in FEET:
        obj = bpy.data.objects[name]
        root = obj.vertex_groups["root"]
        left, right = obj.vertex_groups["foot_l"], obj.vertex_groups["foot_r"]
        indices = [vertex.index for vertex in obj.data.vertices]
        root.remove(indices)
        for vertex in obj.data.vertices:
            assert abs(vertex.co.x) > 0.5, name
            (left if vertex.co.x > 0 else right).add([vertex.index], 1.0, "REPLACE")


def set_pose(armature, frame, poses):
    """bone별 (회전 XYZ rad, 이동 cm)을 부모 기준·armature 축으로 받아 key를 넣는다. 회전 중심은 bone head다."""
    for name in ALL_BONES:
        rotation, location = poses.get(name, ((0, 0, 0), (0, 0, 0)))
        pose_bone = armature.pose.bones[name]
        rest = pose_bone.bone.matrix_local.to_3x3()
        inverse = rest.inverted()
        pose_bone.rotation_mode = "QUATERNION"
        pose_bone.rotation_quaternion = (inverse @ Euler(rotation).to_matrix() @ rest).to_quaternion()
        pose_bone.location = inverse @ Vector(location)
        pose_bone.keyframe_insert("rotation_quaternion", frame=frame)
        pose_bone.keyframe_insert("location", frame=frame)


def idle_pose(phase):
    # 2초 호흡. 몸이 살짝 오르내리고 머리가 반 박자 늦게 끄덕이며 날개가 숨에 맞춰 벌어진다.
    breath = 0.5 - 0.5 * math.cos(phase)
    return {
        "body": ((0.02 * math.sin(phase), 0, 0), (0, 0, 0.8 * breath)),
        "head": ((-0.035 * math.sin(phase - 0.8), 0.03 * math.sin(phase), 0), (0, 0, 0)),
        "wing_l": ((0, -0.08 * breath, 0), (0, 0, 0)),
        "wing_r": ((0, 0.08 * breath, 0), (0, 0, 0)),
    }


def walk_pose(phase):
    # 한 주기에 두 걸음. phase 0에 왼발이 들려 앞으로 지나가고 phase π에 오른발이 지나간다.
    # 앞은 -Y, 왼쪽은 +X다. +X 회전은 몸을 앞으로 숙이고, +Y 회전은 위를 왼쪽으로 기울인다.
    stride, lift, toe = 9.0, 6.0, 0.35
    pose = {}
    for name, offset in (("foot_l", 0.0), ("foot_r", math.pi)):
        p = phase + offset
        swing = max(0.0, math.cos(p))
        pose[name] = ((-toe * swing, 0, 0), (0, -stride * math.sin(p), lift * swing))
    # 뒤뚱걸음: 디딘 발 쪽으로 몸이 기울고, 두 걸음마다 두 번 튀어 오른다.
    roll = -0.13 * math.cos(phase)
    bounce = 1.6 * (0.5 + 0.5 * math.cos(2 * phase))
    pose["body"] = ((0.10 + 0.02 * math.cos(2 * phase), roll, 0.06 * math.cos(phase)), (0, 0, bounce))
    # 머리는 몸의 기울기를 일부 되돌려 시선을 유지한다.
    pose["head"] = ((-0.06 - 0.04 * math.cos(2 * phase + 0.6), -0.6 * roll, -0.04 * math.cos(phase)), (0, 0, 0))
    # 날개는 같은 쪽 발과 반대로 흔든다. 아래로 늘어진 날개 끝은 +X 회전에서 뒤(+Y)로 간다.
    swing = 0.35 * math.sin(phase)
    pose["wing_l"] = ((swing, -0.15, 0), (0, 0, 0))
    pose["wing_r"] = ((-swing, 0.15, 0), (0, 0, 0))
    return pose


# 미리보기 render에만 쓰는 방망이다(wing_r bone을 따름). 날개 끝 손잡이에서 앞 아래로 뻗는다. rest 기준 cm, 앞은 -Y.
# 게임의 방망이(DuckovCharacter.cpp MeleeParts)는 UE 축(x = -y, y = -x)으로 같은 값을 쓴다. 원본에는 저장하지 않는다.
BAT_GRIP = (-30.0, -3.0, 28.0)
BAT_DIRECTION = (0.0, -0.94, -0.33)
# 이름 -> (손잡이에서 중심까지 거리, 길이, 지름) cm
BAT_PARTS = {"PreviewBatHandle": (7.0, 14.0, 3.0), "PreviewBatBarrel": (27.0, 26.0, 6.0)}
# 휘두르기 key: (진행 0~1, 오른날개 끝 방향(어깨 기준), 몸 비틀기 Z rad). 음수 Z는 오른어깨를 뒤로 뺀다.
SWING_KEYS = (
    (0.0, None, 0.0),
    (0.3, (-4.0, 10.0, 25.0), -0.35),
    (0.55, (8.0, -14.0, 0.0), 0.4),
    (0.75, (14.0, -6.0, -8.0), 0.5),
    (1.0, None, 0.0),
)


def wing_turn(name, direction):
    """늘어진 날개 방향을 어깨에서 direction 쪽으로 돌리는 최소 회전이다. None이면 rest다."""
    if direction is None:
        return Quaternion()
    return Vector(WING_HANG[name]).normalized().rotation_difference(Vector(direction).normalized())


def melee_swing_pose(phase):
    # make_action은 반복 동작처럼 0~2π phase를 준다. 한 번 재생 동작은 0~1 진행으로 바꿔 key 사이를 부드럽게 잇는다.
    t = phase / (2 * math.pi)
    for (t0, d0, z0), (t1, d1, z1) in zip(SWING_KEYS, SWING_KEYS[1:]):
        if t <= t1 + 1e-9:
            u = (t - t0) / (t1 - t0)
            u = u * u * (3 - 2 * u)
            break
    turn = wing_turn("wing_r", d0).slerp(wing_turn("wing_r", d1), u)
    twist = z0 + (z1 - z0) * u
    return {
        # 비틀며 치는 순간 몸을 살짝 앞으로 숙이고, 머리는 비틀기를 절반 되돌려 정면을 본다.
        "body": ((0.1 * math.sin(math.pi * t), 0, twist), (0, 0, 0)),
        "head": ((0, 0, -0.5 * twist), (0, 0, 0)),
        "wing_r": (tuple(turn.to_euler("XYZ")), (0, 0, 0)),
    }


def add_swing_action(armature):
    if MELEE_SWING[0] in bpy.data.actions:
        raise RuntimeError("휘두르기 action이 이미 있습니다. export/verify/render 모드를 사용하세요.")
    current = armature.animation_data.action
    make_action(armature, *MELEE_SWING, melee_swing_pose)
    use_action(armature, current)


def hold_wings():
    # 늘어진 날개 방향을 어깨(bone head)에서 목표 점 방향으로 돌리는 최소 회전이다. 날개는 body 자식이라 몸 동작을 그대로 따른다.
    pose = {}
    for name, hang in WING_HANG.items():
        head = Vector(next(bone[2] for bone in existing.BONES if bone[0] == name))
        turn = Vector(hang).normalized().rotation_difference((Vector(HOLD_TARGET[name]) - head).normalized())
        pose[name] = (tuple(turn.to_euler("XYZ")), (0, 0, 0))
    return pose


def idle_rifle_pose(phase):
    pose = idle_pose(phase)
    pose.update(hold_wings())
    return pose


def walk_rifle_pose(phase):
    pose = walk_pose(phase)
    pose.update(hold_wings())
    return pose


def add_rifle_actions(armature):
    if IDLE_RIFLE[0] in bpy.data.actions or WALK_RIFLE[0] in bpy.data.actions:
        raise RuntimeError("총 쥔 action이 이미 있습니다. export/verify/render 모드를 사용하세요.")
    current = armature.animation_data.action
    make_action(armature, *IDLE_RIFLE, idle_rifle_pose)
    make_action(armature, *WALK_RIFLE, walk_rifle_pose)
    use_action(armature, current)


def make_action(armature, name, length, pose_at):
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    armature.animation_data_create().action = action
    for frame in range(length + 1):
        set_pose(armature, frame, pose_at(2 * math.pi * frame / length))
    for fcurve in action_fcurves(action):
        for point in fcurve.keyframe_points:
            point.interpolation = "LINEAR"
    action.frame_range = (0, length)
    return action


def action_fcurves(action):
    # Blender 4.4+의 slotted action은 layer/strip/channelbag 아래에 fcurve가 있다.
    if hasattr(action, "layers"):
        return [fcurve for layer in action.layers for strip in layer.strips for bag in strip.channelbags for fcurve in bag.fcurves]
    return list(action.fcurves)


def use_action(armature, action):
    armature.animation_data.action = action
    if hasattr(armature.animation_data, "action_slot") and action.slots:
        armature.animation_data.action_slot = action.slots[0]
    scene = bpy.context.scene
    scene.frame_start, scene.frame_end = (int(value) for value in action.frame_range)


def create():
    if DERIVED.exists():
        raise RuntimeError("파생 원본이 이미 있습니다. export/verify/render 모드를 사용하세요.")
    bpy.ops.wm.open_mainfile(filepath=str(PARTS))
    armature = rig()
    add_foot_bones(armature)
    # 모든 mesh에 같은 bone 집합의 vertex group을 둔다(기존 관례). 발 bone weight는 발 mesh만 갖는다.
    for obj in bpy.data.objects:
        if obj.type == "MESH":
            for name, *_ in FOOT_BONES:
                obj.vertex_groups.new(name=name)
    reweight_feet()
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    make_action(armature, *IDLE, idle_pose)
    walk = make_action(armature, *WALK, walk_pose)
    use_action(armature, walk)
    add_rifle_actions(armature)
    add_swing_action(armature)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DERIVED), check_existing=False)


def evaluated_min_z(names):
    depsgraph = bpy.context.evaluated_depsgraph_get()
    lowest = math.inf
    for name in names:
        obj = bpy.data.objects[name]
        mesh = obj.evaluated_get(depsgraph).to_mesh()
        lowest = min(lowest, min((obj.matrix_world @ vertex.co).z for vertex in mesh.vertices))
        obj.evaluated_get(depsgraph).to_mesh_clear()
    return lowest


def bone_heads(armature):
    return {name: armature.matrix_world @ armature.pose.bones[name].head for name in ALL_BONES}


def verify():
    armature = rig()
    report = {"status": "PASS", "blender": bpy.app.version_string, "bones": list(armature.data.bones.keys()), "actions": {}}
    assert set(armature.data.bones.keys()) == set(ALL_BONES)
    for name, parent, head, tail in existing.BONES + FOOT_BONES:
        bone = armature.data.bones[name]
        assert (bone.parent.name if bone.parent else None) == parent, name
        assert (bone.head_local - Vector(head)).length < 1e-5 and (bone.tail_local - Vector(tail)).length < 1e-5, name
    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        assert set(group.name for group in obj.vertex_groups) == set(ALL_BONES), obj.name
        for vertex in obj.data.vertices:
            assert len(vertex.groups) == 1 and abs(vertex.groups[0].weight - 1.0) < 1e-5, obj.name
        used = {obj.vertex_groups[g.group].name for vertex in obj.data.vertices for g in vertex.groups}
        if obj.name in FEET:
            assert used == {"foot_l", "foot_r"}, obj.name
        else:
            assert not used & {"foot_l", "foot_r"}, obj.name
    assert bpy.context.scene.render.fps == FPS
    original = armature.animation_data.action
    feet_meshes = [obj.name for obj in bpy.data.objects if obj.type == "MESH" and obj.name in FEET]
    for name, length in ACTIONS:
        action = bpy.data.actions[name]
        assert tuple(action.frame_range) == (0, length), name
        use_action(armature, action)
        scene = bpy.context.scene
        scene.frame_set(0)
        start = bone_heads(armature)
        start_rot = {bone: armature.pose.bones[bone].matrix.to_quaternion() for bone in ALL_BONES}
        scene.frame_set(length)
        end = bone_heads(armature)
        loop_gap = max((start[bone] - end[bone]).length for bone in ALL_BONES)
        loop_angle = max(start_rot[bone].rotation_difference(armature.pose.bones[bone].matrix.to_quaternion()).angle
                         for bone in ALL_BONES)
        assert loop_gap < 1e-3 and loop_angle < 1e-3, name
        lowest, lift = math.inf, 0.0
        for frame in range(length + 1):
            scene.frame_set(frame)
            lowest = min(lowest, evaluated_min_z(feet_meshes))
            lift = max(lift, max(bone_heads(armature)[bone].z - 6.0 for bone in ("foot_l", "foot_r")))
        # 발밑이 지면 아래로 1 cm 넘게 파고들지 않는다.
        assert lowest > -1.0, (name, lowest)
        report["actions"][name] = {"frames": length, "seconds": length / FPS, "loop_gap_cm": loop_gap,
                                   "loop_angle_rad": loop_angle, "feet_min_z_cm": lowest, "foot_max_lift_cm": lift}
    use_action(armature, original)
    return report


def export_mesh(names):
    previous = existing.NAMES
    existing.NAMES = names
    armature = rig()
    # bind pose는 rest 기준이다. 현재 action의 pose가 mesh FBX에 섞이지 않게 한다.
    armature.data.pose_position = "REST"
    try:
        return existing.export(EXPORTS)
    finally:
        existing.NAMES = previous
        armature.data.pose_position = "POSE"


def export_animation(action_name):
    armature = rig()
    original = bpy.data.actions[action_name]
    temp = original.copy()
    use_action(armature, temp)
    root = armature.pose.bones["root"]
    rest = root.bone.matrix_local.to_3x3()
    turn = (rest.inverted() @ Matrix.Rotation(ANIMATION_EXPORT_YAW, 3, "Z") @ rest).to_quaternion()
    for frame in range(int(temp.frame_range[0]), int(temp.frame_range[1]) + 1):
        root.rotation_quaternion = turn
        root.keyframe_insert("rotation_quaternion", frame=frame)
    existing.activate(armature)
    target = EXPORTS / (action_name + ".fbx")
    try:
        outcome = export_armature(target)
    finally:
        use_action(armature, original)
        bpy.data.actions.remove(temp)
    assert outcome == {"FINISHED"} and target.stat().st_size > 5000
    return {"name": target.name, "bytes": target.stat().st_size, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()}


def export_armature(target):
    return bpy.ops.export_scene.fbx(
        filepath=str(target), use_selection=True, object_types={"ARMATURE"},
        global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        axis_forward="-Y", axis_up="Z", bake_space_transform=False,
        add_leaf_bones=False, use_armature_deform_only=False,
        primary_bone_axis="Y", secondary_bone_axis="X",
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
        bake_anim_step=1.0, bake_anim_simplify_factor=0.0, use_custom_props=False,
    )


def export():
    meshes = export_mesh((BODY,) + FEET)
    animations = [export_animation(name) for name, _ in ACTIONS]
    return {"meshes": meshes, "animations": animations}


def roundtrip():
    """애니메이션 FBX를 빈 scene으로 다시 읽어 bone 집합·길이·발 위치가 원본(export 회전 포함)과 같은지 본다."""
    expected = {}
    armature = rig()
    turn = Matrix.Rotation(ANIMATION_EXPORT_YAW, 4, "Z")
    for name, length in ACTIONS:
        use_action(armature, bpy.data.actions[name])
        expected[name] = {}
        for frame in (0, length // 4, length // 2):
            bpy.context.scene.frame_set(frame)
            expected[name][frame] = {bone: turn @ head for bone, head in bone_heads(armature).items()}
    results = {}
    for name, length in ACTIONS:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        scene = bpy.context.scene
        scene.unit_settings.system = "METRIC"
        scene.unit_settings.scale_length = 0.01
        scene.render.fps = FPS
        assert bpy.ops.import_scene.fbx(filepath=str(EXPORTS / (name + ".fbx")), use_anim=True,
                                        automatic_bone_orientation=False) == {"FINISHED"}
        imported = [obj for obj in bpy.data.objects if obj.type == "ARMATURE"]
        assert len(imported) == 1
        imported = imported[0]
        assert set(imported.data.bones.keys()) == set(ALL_BONES), name
        action = imported.animation_data.action
        first, last = action.frame_range
        assert round(last - first) == length, (name, first, last)
        gap = 0.0
        for frame, heads in expected[name].items():
            scene.frame_set(int(first) + frame)
            for bone, head in heads.items():
                gap = max(gap, ((imported.matrix_world @ imported.pose.bones[bone].head) - head).length)
        assert gap < 0.05, (name, gap)
        results[name] = {"frames": length, "max_bone_gap_cm": gap}
    return results


def render_frames():
    """게임 카메라와 비슷한 비스듬한 시점과 옆 시점의 frame PNG를 만든다. GIF 합치기는 Blender 밖에서 한다."""
    scene = bpy.context.scene
    armature = rig()
    shown = {BODY, "SK_CustomizationPartA", "SK_CustomizationEyeRound", "SK_CustomizationBeakDuck",
             "SK_CustomizationWingRound", "SK_CustomizationTailFan", "SK_CustomizationFootWebbed"}
    for obj in bpy.data.objects:
        if obj.type == "MESH":
            obj.hide_render = obj.name not in shown
    bpy.ops.mesh.primitive_plane_add(size=220, location=(0, 0, 0))
    ground = bpy.context.object
    ground_material = bpy.data.materials.new("PreviewGround")
    ground_material.diffuse_color = (0.32, 0.36, 0.30, 1)
    ground.data.materials.append(ground_material)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = False
    scene.render.resolution_x = 360
    scene.render.resolution_y = 360
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    camera = scene.camera
    camera.data.ortho_scale = 140
    views = {
        # 게임 카메라처럼 높은 곳에서 비스듬히 본다.
        "Game": ((150, -190, 215), (0, 0, 40)),
        # 발 동작이 잘 보이는 옆 시점이다(캐릭터는 -Y로 걷는다).
        "Side": ((260, 0, 50), (0, 0, 42)),
    }
    gun = add_preview_gun(armature)
    bat = add_preview_bat(armature)
    frames = {}
    for name, length in ACTIONS:
        use_action(armature, bpy.data.actions[name])
        for obj in gun:
            obj.hide_render = name not in (IDLE_RIFLE[0], WALK_RIFLE[0])
        for obj in bat:
            obj.hide_render = name in (IDLE_RIFLE[0], WALK_RIFLE[0])
        for view, (location, target) in views.items():
            camera.location = location
            camera.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
            paths = []
            # 반복 동작의 마지막 frame은 첫 frame과 같아 GIF에서는 뺀다. 한 번 재생 동작은 끝 frame까지 그린다.
            for frame in range(length + (1 if (name, length) in ONE_SHOTS else 0)):
                scene.frame_set(frame)
                path = GATE / "frames" / f"{name}_{view}_{frame:03d}.png"
                scene.render.filepath = str(path)
                bpy.ops.render.render(write_still=True)
                paths.append(str(path))
            frames[f"{name}_{view}"] = paths
    return frames


def add_preview_gun(armature):
    """총 모형을 rest pose 위치에 놓고 body bone에 붙인다. 저장하지 않는 render 전용이다."""
    material = bpy.data.materials.new("PreviewGun")
    material.diffuse_color = (0.16, 0.16, 0.18, 1)
    armature.data.pose_position = "REST"
    bpy.context.view_layer.update()
    parts = []
    for name, (center, size) in GUN_PARTS.items():
        bpy.ops.mesh.primitive_cube_add(size=1)
        obj = bpy.context.object
        obj.name = name
        obj.data.materials.append(material)
        # matrix_world는 다음 depsgraph 갱신 전까지 scale을 반영하지 않으므로 직접 만든다.
        world = Matrix.LocRotScale(Vector(center), None, Vector(size))
        obj.parent = armature
        obj.parent_type = "BONE"
        obj.parent_bone = "body"
        bpy.context.view_layer.update()
        obj.matrix_world = world
        parts.append(obj)
    armature.data.pose_position = "POSE"
    return parts


def add_preview_bat(armature):
    """방망이를 rest pose 위치에 놓고 wing_r bone에 붙인다. 저장하지 않는 render 전용이다."""
    material = bpy.data.materials.new("PreviewBat")
    material.diffuse_color = (0.55, 0.35, 0.18, 1)
    armature.data.pose_position = "REST"
    bpy.context.view_layer.update()
    grip, direction = Vector(BAT_GRIP), Vector(BAT_DIRECTION).normalized()
    turn = Vector((0, 0, 1)).rotation_difference(direction).to_matrix().to_4x4()
    parts = []
    for name, (offset, length, diameter) in BAT_PARTS.items():
        bpy.ops.mesh.primitive_cylinder_add(radius=0.5, depth=1)
        obj = bpy.context.object
        obj.name = name
        obj.data.materials.append(material)
        world = Matrix.Translation(grip + direction * offset) @ turn @ Matrix.Diagonal((diameter, diameter, length, 1))
        obj.parent = armature
        obj.parent_type = "BONE"
        obj.parent_bone = "wing_r"
        bpy.context.view_layer.update()
        obj.matrix_world = world
        parts.append(obj)
    armature.data.pose_position = "POSE"
    return parts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("create", "add-rifle", "add-swing", "export", "export-animations", "verify", "render"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    GATE.mkdir(parents=True, exist_ok=True)
    parts_hash = hashlib.sha256(PARTS.read_bytes()).hexdigest()
    if args.mode == "create":
        create()
    bpy.ops.wm.open_mainfile(filepath=str(DERIVED))
    if args.mode in ("add-rifle", "add-swing"):
        # 이미 만든 파생 원본에 action만 더해 저장한다. 기존 action·mesh·bone은 바꾸지 않는다.
        (add_rifle_actions if args.mode == "add-rifle" else add_swing_action)(rig())
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(DERIVED), check_existing=False)
    result = verify()
    if args.mode in ("create", "export"):
        result["export"] = export()
        result["roundtrip"] = roundtrip()
    if args.mode == "export-animations":
        # mesh FBX는 그대로 두고 애니메이션 FBX만 다시 낸다.
        result["export"] = {"animations": [export_animation(name) for name, _ in ACTIONS]}
        result["roundtrip"] = roundtrip()
    if args.mode == "render":
        result["frames"] = render_frames()
    assert hashlib.sha256(PARTS.read_bytes()).hexdigest() == parts_hash, "부위 원본이 변경됐습니다."
    write(args.mode + ".json", result)
    print("C-anim " + args.mode + ": " + result["status"])


if __name__ == "__main__":
    main()
