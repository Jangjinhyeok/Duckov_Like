"""Blender background 전용: 원작 구성(고정 몸통 + 부위별 유형)에 맞춘 부위 파츠를 만든다.

기존 CustomizationPrototype.blend / CustomizationSlots.blend는 건드리지 않고 새 파생 원본
CustomizationParts.blend를 만든다. 공통 rig와 material, 헤어로 재사용하는 깃 A/B/C는 슬롯 원본에서 가져온다.
create : 파생 원본을 만들고 저장한다(이미 있으면 거부).
export : 저장된 파생 원본에서 새 mesh의 FBX를 내보낸다.
verify : 계약 검사만 한다.
render : 기본 조합과 머리 모양 극값을 렌더한다(Saved/Automation/CustomizationParts).
"""

import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Matrix, Vector

sys.dont_write_bytecode = True

ART = Path(__file__).resolve().parent
ROOT = ART.parents[1]
GATE = ROOT / "Saved/Automation/CustomizationParts"
SLOTS = ART / "CustomizationSlots.blend"
DERIVED = ART / "CustomizationParts.blend"
BODY = "SK_CustomizationBaseBody"
HEAD_MORPH = "HeadFlat"
# 머리 윗부분(z > 70)을 이 비율로 눌러 납작한 머리를 만든다. weight 1이 가장 납작하다.
HEAD_PIVOT_Z = 70.0
HEAD_FLATTEN = 0.4
HAIR = ("SK_CustomizationPartA", "SK_CustomizationPartB", "SK_CustomizationPartC")

spec = importlib.util.spec_from_file_location("existing_author_assets", ART / "author_assets.py")
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)


def write(name, value):
    GATE.mkdir(parents=True, exist_ok=True)
    (GATE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def piece(name, center, radius, bone, mat, rotation=(0, 0, 0)):
    """회전을 축별로 줄 수 있는 타원체다. 좌표를 mesh에 반영하고 원점은 rig와 같은 발밑에 둔다."""
    obj = existing.ellipsoid(name, center, radius, bone, mat)
    if any(rotation):
        pivot = Vector(center)
        turn = Euler(rotation).to_matrix()
        for vertex in obj.data.vertices:
            vertex.co = pivot + turn @ (vertex.co - pivot)
    return obj


def head_flat_offset(z):
    """HeadFlat weight 1일 때 높이 z의 머리 표면이 내려가는 거리(cm, 음수)다."""
    return -HEAD_FLATTEN * max(0.0, z - HEAD_PIVOT_Z)


# 파츠 이름 -> (조각 목록, material 이름). 조각은 (이름, 중심, 반지름, bone, 회전).
def part_specs():
    eye = []
    for name, radius, y, z in (("SK_CustomizationEyeRound", (3, 2, 3.5), -18, 76),
                               ("SK_CustomizationEyeLarge", (5, 2.5, 5.5), -17.5, 76),
                               ("SK_CustomizationEyeSleepy", (4.5, 1.5, 1.2), -18.5, 75)):
        eye.append((name, [("EyeL", (10, y, z), radius, "head", (0, 0, 0)),
                           ("EyeR", (-10, y, z), radius, "head", (0, 0, 0))], "M_CustomizationColor"))
    beak = [
        ("SK_CustomizationBeakDuck", [("Beak", (0, -21, 64), (12, 10, 4), "head", (0, 0, 0))], "M_CustomizationColor"),
        ("SK_CustomizationBeakSmall", [("Beak", (0, -20, 65), (6, 6, 3.5), "head", (0, 0, 0))], "M_CustomizationColor"),
        ("SK_CustomizationBeakLong", [("Beak", (0, -27, 64), (6, 14, 3), "head", (0, 0, 0))], "M_CustomizationColor"),
    ]
    wing = [
        ("SK_CustomizationWingRound", [("WingL", (23, 2, 42), (7, 12, 18), "wing_l", (0, -0.30, 0)),
                                       ("WingR", (-23, 2, 42), (7, 12, 18), "wing_r", (0, 0.30, 0))], "M_CustomizationColor"),
        ("SK_CustomizationWingSmall", [("WingL", (24, 2, 46), (5, 8, 11), "wing_l", (0, -0.55, 0)),
                                       ("WingR", (-24, 2, 46), (5, 8, 11), "wing_r", (0, 0.55, 0))], "M_CustomizationColor"),
        # 원작의 장갑 같은 손. 주먹과 엄지 두 조각이다.
        ("SK_CustomizationWingMitten", [("FistL", (27, -1, 31), (9, 9, 8), "wing_l", (0, 0, 0)),
                                        ("ThumbL", (22, -8, 35), (3.5, 3.5, 4), "wing_l", (0, 0, 0)),
                                        ("FistR", (-27, -1, 31), (9, 9, 8), "wing_r", (0, 0, 0)),
                                        ("ThumbR", (-22, -8, 35), (3.5, 3.5, 4), "wing_r", (0, 0, 0))], "M_CustomizationColor"),
    ]
    tail = [
        ("SK_CustomizationTailShort", [("Tail", (0, 20, 24), (8, 7, 6), "body", (0.5, 0, 0))], "M_CustomizationColor"),
        # 부채꼴 깃 세 개. 바깥 두 깃을 옆으로 벌려 정면 미리보기에서도 머리 옆으로 끝이 보이게 한다.
        ("SK_CustomizationTailFan", [("TailC", (0, 25, 40), (3.5, 4, 15), "body", (-0.4, 0, 0)),
                                     ("TailL", (-17, 24, 50), (4, 4, 16), "body", (-0.3, -0.75, 0)),
                                     ("TailR", (17, 24, 50), (4, 4, 16), "body", (-0.3, 0.75, 0))], "M_CustomizationColor"),
    ]
    # 발은 유형마다 색이 정해져 있다(원작과 같다). 색상 parameter가 없는 고정색 material을 쓴다.
    foot = [
        ("SK_CustomizationFootWebbed", [("FootL", (10, -5, 3), (8, 12, 3), "root", (0, 0, 0)),
                                        ("FootR", (-10, -5, 3), (8, 12, 3), "root", (0, 0, 0))], "M_PrototypeBeak"),
        ("SK_CustomizationFootBoot", [("BootL", (10, -3, 5), (8, 11, 5), "root", (0, 0, 0)),
                                      ("ShaftL", (10, 1, 11), (6, 6, 7), "root", (0, 0, 0)),
                                      ("BootR", (-10, -3, 5), (8, 11, 5), "root", (0, 0, 0)),
                                      ("ShaftR", (-10, 1, 11), (6, 6, 7), "root", (0, 0, 0))], "M_PrototypeEye"),
    ]
    return eye + beak + wing + tail + foot


def new_names():
    return (BODY,) + tuple(name for name, _, _ in part_specs())


def add_body(rig):
    color = bpy.data.materials["M_CustomizationColor"]
    torso = piece("Torso", (0, 3, 39), (24, 19, 28), "body", color)
    head = piece("Head", (0, -2, 70), (21, 19, 21), "head", color)
    obj = existing.merge(BODY, [torso, head], rig)
    obj.shape_key_add(name="Basis")
    shape = obj.shape_key_add(name=HEAD_MORPH)
    head_group = obj.vertex_groups["head"].index
    for vertex, point in zip(obj.data.vertices, shape.data):
        if any(g.group == head_group for g in vertex.groups):
            point.co.z += head_flat_offset(point.co.z)
    shape.slider_min = 0.0
    shape.slider_max = 1.0
    shape.value = 0.0
    return obj


def create():
    if DERIVED.exists():
        raise RuntimeError("파생 원본이 이미 있습니다. export/verify 모드를 사용하세요.")
    bpy.ops.wm.open_mainfile(filepath=str(SLOTS))
    rig = bpy.data.objects["Armature"]
    # 이전 구성의 통짜 body와 장식은 새 원본에 두지 않는다. 기존 원본 파일에는 그대로 남는다.
    for name in ("SK_CustomizationBody", "SK_CustomizationRoundGlasses", "SK_CustomizationBeakRing"):
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
    add_body(rig)
    for name, pieces, material in part_specs():
        mat = bpy.data.materials[material]
        objects = [piece(*item[:4], mat, item[4]) for item in pieces]
        existing.merge(name, objects, rig) if len(objects) > 1 else single(name, objects[0], rig)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(DERIVED), check_existing=False)


def single(name, obj, rig):
    obj.name = name
    obj.data.name = name + "_Mesh"
    obj.parent = rig
    modifier = obj.modifiers.new("SharedRig", "ARMATURE")
    modifier.object = rig
    for bone, *_ in existing.BONES:
        if bone not in obj.vertex_groups:
            obj.vertex_groups.new(name=bone)
    return obj


def verify():
    rig = bpy.data.objects["Armature"]
    report = {"status": "PASS", "blender": bpy.app.version_string, "meshes": {},
              "follow_offsets_cm": {"eye_z76": head_flat_offset(76), "hair_base_z88": head_flat_offset(88)}}
    bones = {bone[0] for bone in existing.BONES}
    for name in new_names() + HAIR:
        obj = bpy.data.objects[name]
        assert obj.matrix_world.is_identity and obj.parent == rig, name
        assert len(obj.modifiers) == 1 and obj.modifiers[0].type == "ARMATURE" and obj.modifiers[0].object == rig, name
        assert len(obj.data.materials) == 1 and len(obj.data.uv_layers) == 1, name
        assert set(group.name for group in obj.vertex_groups) == bones, name
        for vertex in obj.data.vertices:
            assert all(math.isfinite(value) for value in vertex.co), name
            assert len(vertex.groups) == 1 and abs(vertex.groups[0].weight - 1.0) < 1e-5, name
        report["meshes"][name] = {"vertices": len(obj.data.vertices), "material": obj.data.materials[0].name,
                                  "bounds_cm": existing.bounds(obj)}
    body = bpy.data.objects[BODY]
    keys = body.data.shape_keys.key_blocks
    assert tuple(key.name for key in keys) == ("Basis", HEAD_MORPH) and keys[HEAD_MORPH].value == 0.0
    deltas = [(a.co - b.co).length for a, b in zip(keys["Basis"].data, keys[HEAD_MORPH].data)]
    assert abs(max(deltas) - HEAD_FLATTEN * 21.0) < 0.05
    report["meshes"][BODY]["morph"] = {"name": HEAD_MORPH, "max_delta_cm": max(deltas),
                                       "changed_vertices": sum(value > 0.001 for value in deltas)}
    for name, _, _ in part_specs():
        assert bpy.data.objects[name].data.shape_keys is None, name
    return report


def export():
    names = existing.NAMES
    existing.NAMES = new_names()
    try:
        return existing.export(ART / "Exports")
    finally:
        existing.NAMES = names


def render():
    scene = bpy.context.scene
    shown = {BODY, "SK_CustomizationPartA", "SK_CustomizationEyeRound", "SK_CustomizationBeakDuck",
             "SK_CustomizationWingRound", "SK_CustomizationTailFan", "SK_CustomizationFootWebbed"}
    alternate = {BODY, "SK_CustomizationPartB", "SK_CustomizationEyeLarge", "SK_CustomizationBeakLong",
                 "SK_CustomizationWingMitten", "SK_CustomizationTailShort", "SK_CustomizationFootBoot"}
    images = []
    for label, names, weight in (("Default", shown, 0.0), ("Alternate", alternate, 0.0), ("HeadFlat", shown, 1.0)):
        for obj in bpy.data.objects:
            if obj.type == "MESH":
                hide = obj.name not in names
                obj.hide_set(hide)
                obj.hide_render = hide
        bpy.data.objects[BODY].data.shape_keys.key_blocks[HEAD_MORPH].value = weight
        target = GATE / (label + ".png")
        scene.render.filepath = str(target)
        bpy.ops.render.render(write_still=True)
        images.append(str(target))
    return images


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("create", "export", "verify", "render"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    GATE.mkdir(parents=True, exist_ok=True)
    slots_hash = hashlib.sha256(SLOTS.read_bytes()).hexdigest()
    if args.mode == "create":
        create()
    bpy.ops.wm.open_mainfile(filepath=str(DERIVED))
    result = verify()
    if args.mode in ("create", "export"):
        result["export"] = export()
    if args.mode == "render":
        result["render"] = render()
    assert hashlib.sha256(SLOTS.read_bytes()).hexdigest() == slots_hash, "슬롯 원본이 변경됐습니다."
    write(args.mode + ".json", result)
    print("C-parts " + args.mode + ": " + result["status"])


if __name__ == "__main__":
    main()
