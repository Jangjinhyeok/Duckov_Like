"""기존 원본을 보존하면서 body 부위별 Morph 여섯 개를 추가하고 검사한다."""

import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.dont_write_bytecode = True


ART = Path(__file__).resolve().parent
ROOT = ART.parents[1]
GATE = ROOT / "Saved/Automation/CustomizationRegions"
SOURCE = ART / "CustomizationPrototype.blend"
BODY = "SK_CustomizationBody"
MORPHS = ("EyeSmall", "EyeLarge", "BeakShort", "BeakLong", "BodyShort", "BodyLong")
SPEC = {
    "Torso": (0, "body", (0, 3, 39)),
    "Head": (0, "head", (0, -2, 70)),
    "WingL": (0, "wing_l", (23, 2, 42)),
    "WingR": (0, "wing_r", (-23, 2, 42)),
    "Beak": (1, "head", (0, -21, 64)),
    "FootL": (1, "root", (10, -5, 3)),
    "FootR": (1, "root", (-10, -5, 3)),
    "EyeL": (2, "head", (10, -18, 76)),
    "EyeR": (2, "head", (-10, -18, 76)),
}
ALLOWED = {
    "EyeSmall": ("EyeL", "EyeR"), "EyeLarge": ("EyeL", "EyeR"),
    "BeakShort": ("Beak",), "BeakLong": ("Beak",),
    "BodyShort": ("Torso", "WingL", "WingR"),
    "BodyLong": ("Torso", "WingL", "WingR"),
}

spec = importlib.util.spec_from_file_location("existing_author_assets", ART / "author_assets.py")
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)


def write(name, value):
    GATE.mkdir(parents=True, exist_ok=True)
    (GATE / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def signature():
    # body의 새 Shape key를 제외한 기존 geometry와 A/B key를 보호한다.
    meshes = {}
    for name in existing.NAMES:
        obj = bpy.data.objects[name]
        meshes[name] = {
            "vertices": [list(v.co) for v in obj.data.vertices],
            "polygons": [(list(p.vertices), p.material_index, p.use_smooth) for p in obj.data.polygons],
            "uv": [[list(loop.uv) for loop in layer.data] for layer in obj.data.uv_layers],
            "groups": [(v.index, [(obj.vertex_groups[g.group].name, g.weight) for g in v.groups]) for v in obj.data.vertices],
            "materials": [mat.name for mat in obj.data.materials],
            "matrix": [list(row) for row in obj.matrix_world],
        }
        if name != BODY:
            meshes[name]["keys"] = [(k.name, k.value, [list(p.co) for p in k.data]) for k in obj.data.shape_keys.key_blocks]
    rig = bpy.data.objects["Armature"]
    data = {"meshes": meshes, "bones": [(b.name, b.parent.name if b.parent else None, list(b.head_local), list(b.tail_local)) for b in rig.data.bones]}
    return hashlib.sha256(json.dumps(data, sort_keys=True).encode()).hexdigest()


def regions(obj):
    adjacency = [set() for _ in obj.data.vertices]
    for edge in obj.data.edges:
        a, b = edge.vertices
        adjacency[a].add(b)
        adjacency[b].add(a)
    unseen = set(range(len(adjacency)))
    found = {}
    while unseen:
        start = min(unseen)
        island, pending = set(), [start]
        while pending:
            index = pending.pop()
            if index in island:
                continue
            island.add(index)
            pending.extend(adjacency[index] - island)
        unseen -= island
        assert len(island) == 222, "body island topology가 기존 계약과 다릅니다."
        center = sum((obj.data.vertices[i].co for i in island), Vector()) / len(island)
        materials = {p.material_index for p in obj.data.polygons if p.vertices[0] in island}
        bones = {obj.vertex_groups[g.group].name for i in island for g in obj.data.vertices[i].groups if g.weight > 0}
        matches = [name for name, (material, bone, expected) in SPEC.items()
                   if materials == {material} and bones == {bone} and (center - Vector(expected)).length < 0.001]
        assert len(matches) == 1 and matches[0] not in found, "body 부위 계약을 식별할 수 없습니다."
        found[matches[0]] = sorted(island)
    assert set(found) == set(SPEC), "필수 body 부위가 없습니다."
    return found


def add():
    existing.verify()
    obj = bpy.data.objects[BODY]
    assert obj.data.shape_keys is None, "기존 key는 보존합니다. initialize는 최초 한 번만 허용합니다."
    before = signature()
    selection = regions(obj)
    basis = obj.shape_key_add(name="Basis")
    for name in MORPHS:
        key = obj.shape_key_add(name=name)
        key.slider_min, key.slider_max, key.value = 0.0, 1.0, 0.0
        for region in ALLOWED[name]:
            for index in selection[region]:
                value = basis.data[index].co.copy()
                if name.startswith("Eye"):
                    factor = 0.65 if name == "EyeSmall" else 1.5
                    anchor = Vector(SPEC[region][2])
                    value = anchor + (value - anchor) * factor
                elif name.startswith("Beak"):
                    value.y = -11.0 + (value.y + 11.0) * (0.65 if name == "BeakShort" else 1.5)
                else:
                    value.y = 3.0 + (value.y - 3.0) * (0.75 if name == "BodyShort" else 1.35)
                key.data[index].co = value
    assert signature() == before, "기존 원본 geometry가 변경됐습니다."
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE), check_existing=False)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    assert signature() == before, "저장한 원본을 재개방한 뒤 기존 geometry가 변경됐습니다."
    result = verify()
    result["protected_source_signature"] = before
    write("source-preservation.json", result)
    return result


def verify():
    existing.verify()
    obj = bpy.data.objects[BODY]
    selection = regions(obj)
    keys = obj.data.shape_keys.key_blocks
    assert tuple(k.name for k in keys) == ("Basis",) + MORPHS
    assert all(keys[name].value == 0.0 for name in MORPHS), "저장한 부위별 기본값은 neutral이어야 합니다."
    assert all(tuple(v.co) == tuple(p.co) for v, p in zip(obj.data.vertices, keys["Basis"].data))
    report = {"status": "PASS", "source": str(SOURCE), "blender": bpy.app.version_string,
              "protected_source_signature": signature(), "morphs": {}}
    for name in MORPHS:
        allowed = set(i for region in ALLOWED[name] for i in selection[region])
        distances = [(a.co - b.co).length for a, b in zip(keys[name].data, keys["Basis"].data)]
        assert max(distances) > 0.5
        assert all(math.isfinite(c) for p in keys[name].data for c in p.co)
        assert all(value == 0.0 for index, value in enumerate(distances) if index not in allowed)
        if name.startswith("Eye"):
            left = [keys[name].data[i].co.copy() for i in selection["EyeL"]]
            right = [Vector((-keys[name].data[i].co.x, keys[name].data[i].co.y, keys[name].data[i].co.z)) for i in selection["EyeR"]]
            assert all(min((a - b).length for b in right) < 0.0001 for a in left), "Eye key의 대칭이 깨졌습니다."
        report["morphs"][name] = {"changed_vertices": sum(d > 0.0001 for d in distances),
                                  "allowed_vertices": len(allowed), "max_delta_cm": max(distances),
                                  "non_target_unchanged": True}
    return report


def export(destination):
    names = existing.NAMES
    existing.NAMES = (BODY,)
    try:
        return existing.export(destination)
    finally:
        existing.NAMES = names


def roundtrip(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.scale_length = 0.01
    outcome = bpy.ops.import_scene.fbx(filepath=str(path), use_anim=False)
    assert outcome == {"FINISHED"}
    meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
    assert len(meshes) == 1
    obj = meshes[0]
    assert len(obj.data.vertices) == 1998
    assert [mat.name for mat in obj.data.materials] == ["M_CustomizationColor", "M_PrototypeBeak", "M_PrototypeEye"]
    rig = obj.parent
    assert rig.type == "ARMATURE" and set(b.name for b in rig.data.bones) == {b[0] for b in existing.BONES}
    keys = obj.data.shape_keys.key_blocks
    assert set(k.name for k in keys) == {"Basis", *MORPHS}
    result = {"status": "PASS", "file": str(path), "bones": [b.name for b in rig.data.bones],
              "basis_bounds_cm": existing.bounds(obj), "morphs": {}}
    for name in MORPHS:
        deltas = [(a.co - b.co).length for a, b in zip(keys[name].data, keys["Basis"].data)]
        result["morphs"][name] = {"max_delta_cm": max(deltas), "changed_vertices": sum(d > 0.0001 for d in deltas)}
    return result


def edit_probe():
    obj = bpy.data.objects[BODY]
    basis, key = obj.data.shape_keys.key_blocks["Basis"], obj.data.shape_keys.key_blocks["EyeLarge"]
    deltas = [(point.co - basis.data[i].co).length for i, point in enumerate(key.data)]
    index = max(range(len(deltas)), key=deltas.__getitem__)
    direction = (key.data[index].co - basis.data[index].co).normalized()
    key.data[index].co += 2.0 * direction
    expected = deltas[index] + 2.0
    target = GATE / "EditProbe.blend"
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(target), check_existing=False)
    bpy.ops.wm.open_mainfile(filepath=str(target))
    retained = (bpy.data.objects[BODY].data.shape_keys.key_blocks["EyeLarge"].data[index].co
                - bpy.data.objects[BODY].data.shape_keys.key_blocks["Basis"].data[index].co).length
    assert abs(retained - expected) < 0.00001
    exported = export(GATE / "EditProbeExports")
    result = roundtrip(GATE / "EditProbeExports" / (BODY + ".fbx"))
    magnitude = result["morphs"]["EyeLarge"]["max_delta_cm"]
    assert abs(magnitude - expected) < 0.001
    result.update({"source_vertex": index, "before_max_delta_cm": deltas[index],
                   "edited_max_delta_cm": retained, "fbx_max_delta_cm": magnitude,
                   "same_direction_increment_cm": 2.0, "export": exported})
    return result


def render():
    scene = bpy.context.scene
    camera = scene.camera
    obj = bpy.data.objects[BODY]
    images = []
    for parameter, negative, positive, position, target, scale in (
        ("EyeSize", "EyeSmall", "EyeLarge", (75, -220, 126), (0, -9, 73), 75),
        ("BeakLength", "BeakShort", "BeakLong", (170, -230, 118), (0, -8, 66), 100),
        ("BodyLength", "BodyShort", "BodyLong", (250, -110, 110), (0, 0, 52), 145),
    ):
        camera.location = position
        camera.rotation_euler = (Vector(target) - camera.location).to_track_quat("-Z", "Y").to_euler()
        camera.data.ortho_scale = scale
        for value in (-1, 0, 1):
            for name in MORPHS:
                obj.data.shape_keys.key_blocks[name].value = 0
            obj.data.shape_keys.key_blocks[negative].value = max(-value, 0)
            obj.data.shape_keys.key_blocks[positive].value = max(value, 0)
            path = GATE / f"{parameter}_{value:+d}.png"
            scene.render.filepath = str(path)
            bpy.ops.render.render(write_still=True)
            images.append(str(path))
    return {"status": "PASS", "renders": images, "saved_source_render_settings_preserved": True}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("initialize", "verify", "export", "roundtrip", "edit-probe", "render"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    GATE.mkdir(parents=True, exist_ok=True)
    baseline = GATE / "baseline/Art/CustomizationPrototype/CustomizationPrototype.blend"
    baseline_signature = None
    if baseline.exists():
        bpy.ops.wm.open_mainfile(filepath=str(baseline))
        baseline_signature = signature()
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    if args.mode == "initialize":
        result = add()
    else:
        result = verify()
        if args.mode == "export":
            result = export(ART / "Exports")
        elif args.mode == "roundtrip":
            result = roundtrip(ART / "Exports" / (BODY + ".fbx"))
        elif args.mode == "edit-probe":
            result = edit_probe()
        elif args.mode == "render":
            result = render()
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    current_signature = signature()
    if baseline_signature:
        assert current_signature == baseline_signature, "기존 body Basis, rig 또는 A/B가 변경됐습니다."
        write("source-preservation.json", {"status": "PASS", "baseline_signature": baseline_signature,
                                           "saved_source_signature": current_signature})
    write(args.mode + ".json", result)
    print("Regions " + args.mode + ": " + result["status"])


if __name__ == "__main__":
    main()
