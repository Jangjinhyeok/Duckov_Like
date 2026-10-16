"""Blender background 전용: 슬롯 확장용 새 파츠를 기존 원본의 파생 .blend에 추가하고 export한다.

기존 CustomizationPrototype.blend와 Exports/의 body/A/B는 건드리지 않는다.
slots   : 눈 슬롯 안경(RoundGlasses)과 부리 슬롯 부리 고리(BeakRing)를 만들어 파생 .blend로 저장하고 export한다.
crest-c : 머리 장식 슬롯의 새 선택지 C(한 갈래 깃)를 같은 파생 .blend에 추가하고 export한다.
verify  : 파생 .blend의 새 파츠 계약을 검사한다.
"""

import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

import bpy

sys.dont_write_bytecode = True

ART = Path(__file__).resolve().parent
ROOT = ART.parents[1]
GATE = ROOT / "Saved/Automation/CustomizationCatalog"
SOURCE = ART / "CustomizationPrototype.blend"
DERIVED = ART / "CustomizationSlots.blend"
GLASSES = "SK_CustomizationRoundGlasses"
BEAK_RING = "SK_CustomizationBeakRing"
CREST_C = "SK_CustomizationPartC"

spec = importlib.util.spec_from_file_location("existing_author_assets", ART / "author_assets.py")
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)


def write(name, value):
    GATE.mkdir(parents=True, exist_ok=True)
    (GATE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def torus(name, center, major, minor, scale, rotation, bone, mat):
    bpy.ops.mesh.primitive_torus_add(major_segments=28, minor_segments=10, major_radius=major,
                                     minor_radius=minor, location=center, rotation=rotation, generate_uvs=True)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    matrix = obj.matrix_basis.copy()
    for vertex in obj.data.vertices:
        vertex.co = matrix @ vertex.co
    obj.matrix_basis = existing.Matrix.Identity(4)
    obj.data.materials.append(mat)
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj


def finish(name, pieces, rig):
    """조각을 하나의 skinned mesh로 합치고 공통 rig를 연결한다. 조각이 하나여도 같은 계약을 만든다."""
    if len(pieces) > 1:
        return existing.merge(name, pieces, rig)
    obj = pieces[0]
    obj.name = name
    obj.data.name = name + "_Mesh"
    obj.parent = rig
    modifier = obj.modifiers.new("SharedRig", "ARMATURE")
    modifier.object = rig
    for bone, *_ in existing.BONES:
        if bone not in obj.vertex_groups:
            obj.vertex_groups.new(name=bone)
    return obj


def add_slots():
    rig = bpy.data.objects["Armature"]
    color = bpy.data.materials["M_CustomizationColor"]
    eye = bpy.data.materials["M_PrototypeEye"]
    # 안경: 눈 바깥으로 약간 벌어진 두 고리와 코걸이. 눈은 head bone에만 묶인다.
    ring_l = torus("RingL", (10, -19.5, 76), 6.4, 1.3, (1, 1, 1), (math.radians(90), 0, math.radians(-14)), "head", eye)
    ring_r = torus("RingR", (-10, -19.5, 76), 6.4, 1.3, (1, 1, 1), (math.radians(90), 0, math.radians(14)), "head", eye)
    bridge = existing.ellipsoid("Bridge", (0, -22, 76), (5.5, 1.3, 1.3), "head", eye)
    finish(GLASSES, [ring_l, ring_r, bridge], rig)
    # 부리 고리: 부리 단면(폭 약 21, 높이 약 7)을 감싸는 납작한 고리. 색상은 Hue로 바뀐다.
    ring = torus("BeakBand", (0, -26, 64), 6.0, 1.3, (1.85, 1, 0.72), (math.radians(90), 0, 0), "head", color)
    finish(BEAK_RING, [ring], rig)


def add_crest_c():
    rig = bpy.data.objects["Armature"]
    color = bpy.data.materials["M_CustomizationColor"]
    pieces = [existing.ellipsoid("CrestBase", (0, -1, 88), (15, 12, 3), "head", color),
              existing.ellipsoid("CrestTip", (0, -1, 98), (6, 5, 11), "head", color)]
    obj = finish(CREST_C, pieces, rig)
    existing.shape_key(obj)


def new_names():
    return [name for name in (GLASSES, BEAK_RING, CREST_C) if name in bpy.data.objects]


def verify():
    rig = bpy.data.objects["Armature"]
    report = {"status": "PASS", "blender": bpy.app.version_string, "meshes": {}}
    for name in new_names():
        obj = bpy.data.objects[name]
        assert obj.matrix_world.is_identity and obj.parent == rig
        assert len(obj.modifiers) == 1 and obj.modifiers[0].type == "ARMATURE" and obj.modifiers[0].object == rig
        assert len(obj.data.materials) == 1 and len(obj.data.uv_layers) == 1
        assert set(group.name for group in obj.vertex_groups) == {bone[0] for bone in existing.BONES}
        for vertex in obj.data.vertices:
            assert all(math.isfinite(value) for value in vertex.co)
            assert len(vertex.groups) == 1 and abs(vertex.groups[0].weight - 1.0) < 1e-5
            assert obj.vertex_groups[vertex.groups[0].group].name == "head"
        item = {"vertices": len(obj.data.vertices), "material": obj.data.materials[0].name,
                "bounds_cm": existing.bounds(obj)}
        if name == CREST_C:
            keys = obj.data.shape_keys.key_blocks
            assert tuple(key.name for key in keys) == ("Basis", "Shape") and abs(keys["Shape"].value - 0.5) < 1e-6
            distances = [(a.co - b.co).length for a, b in zip(keys["Basis"].data, keys["Shape"].data)]
            assert max(distances) > 10
            item["morph"] = {"name": "Shape", "max_delta_cm": max(distances)}
        else:
            assert obj.data.shape_keys is None
        report["meshes"][name] = item
    return report


def export():
    names = existing.NAMES
    existing.NAMES = tuple(new_names())
    try:
        return existing.export(ART / "Exports")
    finally:
        existing.NAMES = names


def render(label):
    scene = bpy.context.scene
    visible = {name: bpy.data.objects[name].hide_get() for name in existing.NAMES + tuple(new_names())}
    try:
        crest = CREST_C if CREST_C in bpy.data.objects else existing.NAMES[1]
        for name in existing.NAMES[1:] + tuple(new_names()):
            hide = name not in (crest, GLASSES, BEAK_RING)
            bpy.data.objects[name].hide_set(hide)
            bpy.data.objects[name].hide_render = hide
        bpy.data.objects[existing.NAMES[0]].hide_set(False)
        bpy.data.objects[existing.NAMES[0]].hide_render = False
        target = GATE / (label + ".png")
        scene.render.filepath = str(target)
        bpy.ops.render.render(write_still=True)
    finally:
        for name, hidden in visible.items():
            bpy.data.objects[name].hide_set(hidden)
            bpy.data.objects[name].hide_render = hidden
    return str(target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("slots", "crest-c", "verify"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    GATE.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    if args.mode == "slots":
        if DERIVED.exists():
            raise RuntimeError("파생 원본이 이미 있습니다. crest-c/verify 모드를 사용하세요.")
        bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
        existing.verify()
        add_slots()
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(DERIVED), check_existing=False)
    else:
        bpy.ops.wm.open_mainfile(filepath=str(DERIVED))
        if args.mode == "crest-c":
            if CREST_C in bpy.data.objects:
                raise RuntimeError("PartC가 이미 있습니다.")
            add_crest_c()
            bpy.context.preferences.filepaths.save_version = 0
            bpy.ops.wm.save_as_mainfile(filepath=str(DERIVED), check_existing=False)
    bpy.ops.wm.open_mainfile(filepath=str(DERIVED))
    result = verify()
    if args.mode != "verify":
        result["export"] = export()
        result["render"] = render(args.mode)
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == source_hash, "기존 원본이 변경됐습니다."
    result["source_sha256"] = source_hash
    write(args.mode + ".json", result)
    print("C-slots " + args.mode + ": " + result["status"])


if __name__ == "__main__":
    main()
