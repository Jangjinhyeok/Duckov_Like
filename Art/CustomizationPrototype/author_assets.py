"""Blender background 전용: C3 시험 원본 제작, 비파괴 export, 재열기 검사."""

import argparse
import colorsys
import hashlib
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


ART = Path(__file__).resolve().parent
ROOT = ART.parents[1]
EVIDENCE = ROOT / "Saved/Automation/CustomizationAssets"
SOURCE = ART / "CustomizationPrototype.blend"
NAMES = ("SK_CustomizationBody", "SK_CustomizationPartA", "SK_CustomizationPartB")
BONES = (
    ("root", None, (0, 0, 0), (0, 0, 12)),
    ("body", "root", (0, 0, 12), (0, 0, 53)),
    ("head", "body", (0, 0, 53), (0, 0, 81)),
    ("wing_l", "body", (22, 0, 47), (36, 0, 41)),
    ("wing_r", "body", (-22, 0, 47), (-36, 0, 41)),
)
COLOR = (*colorsys.hsv_to_rgb(0.12, 0.7, 0.9), 1.0)


def activate(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def material(name, color):
    result = bpy.data.materials.new(name)
    assert result.node_tree is not None  # Blender 5.2의 material은 node 기반이다.
    result.diffuse_color = color
    shader = result.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = color
    shader.inputs["Roughness"].default_value = 0.68
    return result


def ellipsoid(name, center, radius, bone, mat, tilt=0):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=12, location=center)
    obj = bpy.context.object
    obj.name = name
    obj.scale = radius
    obj.rotation_euler[1] = tilt
    # 좌표를 mesh에 반영하고 모든 object 원점을 rig와 같은 발밑에 둔다.
    matrix = obj.matrix_basis.copy()
    for vertex in obj.data.vertices:
        vertex.co = matrix @ vertex.co
    obj.matrix_basis = Matrix.Identity(4)
    obj.data.materials.append(mat)
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
    for face in obj.data.polygons:
        face.use_smooth = True
    return obj


def merge(name, pieces, rig):
    bpy.ops.object.select_all(action="DESELECT")
    for piece in pieces:
        piece.select_set(True)
    bpy.context.view_layer.objects.active = pieces[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    obj.data.name = name + "_Mesh"
    obj.parent = rig
    modifier = obj.modifiers.new("SharedRig", "ARMATURE")
    modifier.object = rig
    # 모든 FBX에 같은 bone 집합과 bind pose를 내보낸다.
    for bone, *_ in BONES:
        if bone not in obj.vertex_groups:
            obj.vertex_groups.new(name=bone)
    return obj


def shape_key(obj):
    obj.shape_key_add(name="Basis")
    shape = obj.shape_key_add(name="Shape")
    for vertex in shape.data:
        height = max(0.0, min(1.0, (vertex.co.z - 87.0) / 18.0))
        vertex.co.z += 14.0 * height
        vertex.co.x *= 1.0 + 0.20 * height
    shape.slider_min = 0.0
    shape.slider_max = 1.0
    shape.value = 0.5


def create():
    if SOURCE.exists():
        raise RuntimeError("기존 원본이 있습니다. 재생성 대신 export/verify 모드를 사용하세요.")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01
    scene.unit_settings.length_unit = "CENTIMETERS"
    color = material("M_CustomizationColor", COLOR)
    color["UE_VectorParameter"] = "Color"
    rgb = color.node_tree.nodes.new("ShaderNodeRGB")
    rgb.name = "Color"
    rgb.label = "Color"
    rgb.outputs[0].default_value = COLOR
    color.node_tree.links.new(rgb.outputs[0], color.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
    detail = material("M_PrototypeEye", (0.025, 0.032, 0.044, 1))
    beak = material("M_PrototypeBeak", (0.9, 0.27, 0.035, 1))
    data = bpy.data.armatures.new("CustomizationRig")
    rig = bpy.data.objects.new("Armature", data)
    scene.collection.objects.link(rig)
    activate(rig)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, parent, head, tail in BONES:
        bone = data.edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        if parent:
            bone.parent = data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    rig["UE_SkeletonIntent"] = "SKEL_CustomizationPrototype"
    body_specs = (
        ("Torso", (0, 3, 39), (24, 19, 28), "body", color, 0),
        ("Head", (0, -2, 70), (21, 19, 21), "head", color, 0),
        ("WingL", (23, 2, 42), (7, 12, 18), "wing_l", color, -0.30),
        ("WingR", (-23, 2, 42), (7, 12, 18), "wing_r", color, 0.30),
        ("Beak", (0, -21, 64), (12, 10, 4), "head", beak, 0),
        ("FootL", (10, -5, 3), (8, 12, 3), "root", beak, 0),
        ("FootR", (-10, -5, 3), (8, 12, 3), "root", beak, 0),
        ("EyeL", (10, -18, 76), (3, 2, 3.5), "head", detail, 0),
        ("EyeR", (-10, -18, 76), (3, 2, 3.5), "head", detail, 0),
    )
    body = merge(NAMES[0], [ellipsoid(*spec) for spec in body_specs], rig)
    for part_name, tips in (
        (NAMES[1], ((0, -1, 96, 5, 4, 8, 0), (-6, 0, 93, 4, 4, 6, -0.3), (6, 0, 93, 4, 4, 6, 0.3))),
        (NAMES[2], ((-10, -1, 96, 6, 4, 9, -0.38), (10, -1, 96, 6, 4, 9, 0.38))),
    ):
        pieces = [ellipsoid("CrestBase", (0, -1, 88), (15, 12, 3), "head", color)]
        for i, (x, y, z, rx, ry, rz, tilt) in enumerate(tips):
            pieces.append(ellipsoid("CrestTip" + str(i), (x, y, z), (rx, ry, rz), "head", color, tilt))
        shape_key(merge(part_name, pieces, rig))
    # B만 숨긴 상태를 원본의 기본 표시로 저장한다. export는 이 상태를 보존한다.
    bpy.data.objects[NAMES[2]].hide_render = True
    bpy.data.objects[NAMES[2]].hide_set(True)
    setup_render()
    activate(body)
    bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE), check_existing=False)
    return verify()


def setup_render():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 32
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 720
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.world = bpy.data.worlds.new("PrototypeWorld")
    assert scene.world.node_tree is not None
    scene.world.node_tree.nodes["Background"].inputs[0].default_value = (0.14, 0.18, 0.24, 1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value = 0.45
    scene.view_settings.view_transform = "AgX"
    camera_data = bpy.data.cameras.new("PreviewCamera")
    camera = bpy.data.objects.new("PreviewCamera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = (155, -250, 135)
    camera.rotation_euler = (Vector((0, 0, 57)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 155
    camera_data.clip_end = 1000
    scene.camera = camera
    for name, position, power, size in (
        ("Key", (-100, -150, 210), 1000000, 130),
        ("Fill", (160, -80, 140), 600000, 110),
        ("Rim", (0, 140, 190), 1000000, 100),
    ):
        data = bpy.data.lights.new(name, "AREA")
        data.energy = power
        data.shape = "DISK"
        data.size = size
        light = bpy.data.objects.new(name, data)
        scene.collection.objects.link(light)
        light.location = position
        light.rotation_euler = (Vector((0, 0, 55)) - light.location).to_track_quat("-Z", "Y").to_euler()


def verify():
    rig = bpy.data.objects["Armature"]
    assert rig.type == "ARMATURE"
    assert len(rig.data.bones) == len(BONES)
    assert rig.matrix_world.is_identity
    assert abs(bpy.context.scene.unit_settings.scale_length - 0.01) < 1e-7
    for name, parent, head, tail in BONES:
        bone = rig.data.bones[name]
        assert (bone.parent.name if bone.parent else None) == parent
        assert (bone.head_local - Vector(head)).length < 1e-5
        assert (bone.tail_local - Vector(tail)).length < 1e-5
    result = {"blender_version": bpy.app.version_string, "source": str(Path(bpy.data.filepath)), "meshes": {}}
    common = bpy.data.materials["M_CustomizationColor"]
    assert common.node_tree.nodes["Color"].type == "RGB"
    assert common.node_tree.nodes["Color"].outputs[0].is_linked
    assert set(obj.name for obj in bpy.data.objects if obj.type == "MESH") == set(NAMES)
    for name in NAMES:
        obj = bpy.data.objects[name]
        assert obj.matrix_world.is_identity
        assert obj.parent == rig
        assert len(obj.modifiers) == 1 and obj.modifiers[0].type == "ARMATURE"
        assert obj.modifiers[0].object == rig
        assert obj.data.materials[0] == common
        assert len(obj.data.uv_layers) == 1
        assert set(group.name for group in obj.vertex_groups) == {bone[0] for bone in BONES}
        for vertex in obj.data.vertices:
            assert all(math.isfinite(value) for value in vertex.co)
            assert abs(sum(group.weight for group in vertex.groups) - 1.0) < 1e-5
            assert len(vertex.groups) == 1
        summary = {"vertices": len(obj.data.vertices), "polygons": len(obj.data.polygons), "materials": [mat.name for mat in obj.data.materials], "basis_bounds_cm": bounds(obj)}
        if name != NAMES[0]:
            assert len(obj.data.materials) == 1
            keys = obj.data.shape_keys.key_blocks
            assert tuple(key.name for key in keys) == ("Basis", "Shape")
            assert abs(keys["Shape"].value - 0.5) < 1e-6
            assert all(math.isfinite(value) for key in keys for point in key.data for value in point.co)
            distances = [(a.co - b.co).length for a, b in zip(keys["Basis"].data, keys["Shape"].data)]
            assert max(distances) > 10 and sum(value > 0.001 for value in distances) > 100
            assert all(rig.data.bones[obj.vertex_groups[g.group].name].name == "head" for vertex in obj.data.vertices for g in vertex.groups)
            summary["morph"] = {"name": "Shape", "default_weight": 0.5, "changed_vertices": sum(value > 0.001 for value in distances), "max_delta_cm": max(distances)}
        result["meshes"][name] = summary
    result["status"] = "PASS"
    return result


def bounds(obj):
    positions = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    return [[min(v[axis] for v in positions), max(v[axis] for v in positions)] for axis in range(3)]


def roundtrip(destination, expected_bounds):
    reports = []
    for name in NAMES:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = 0.01
        target = destination / (name + ".fbx")
        outcome = bpy.ops.import_scene.fbx(filepath=str(target), use_anim=False, automatic_bone_orientation=False)
        assert outcome == {"FINISHED"}
        meshes = [obj for obj in bpy.data.objects if obj.type == "MESH"]
        rigs = [obj for obj in bpy.data.objects if obj.type == "ARMATURE"]
        assert len(meshes) == len(rigs) == 1
        obj, rig = meshes[0], rigs[0]
        assert set(bone.name for bone in rig.data.bones) == {bone[0] for bone in BONES}
        for bone_name, parent, head, _ in BONES:
            bone = rig.data.bones[bone_name]
            assert (bone.parent.name if bone.parent else None) == parent
            assert (rig.matrix_world @ bone.head_local - Vector(head)).length < 1e-3
        assert obj.data.materials[0].name == "M_CustomizationColor"
        assert len(obj.data.uv_layers) == 1
        for vertex in obj.data.vertices:
            assert abs(sum(group.weight for group in vertex.groups) - 1.0) < 1e-5
        actual_bounds = bounds(obj)
        assert max(abs(a - b) for actual, expected in zip(actual_bounds, expected_bounds[name]) for a, b in zip(actual, expected)) < 1e-3
        item = {"file": target.name, "bones": [bone.name for bone in rig.data.bones], "basis_bounds_cm": actual_bounds, "material_slots": len(obj.data.materials)}
        if name != NAMES[0]:
            keys = obj.data.shape_keys.key_blocks
            assert "Shape" in keys and len(keys) == 2
            delta = max((obj.matrix_world.to_3x3() @ (a.co - b.co)).length for a, b in zip(keys[0].data, keys["Shape"].data))
            assert delta > 10
            item["morph_max_delta_cm"] = delta
            item["imported_morph_weight"] = keys["Shape"].value
        reports.append(item)
    return {"status": "PASS", "fbx_roundtrip": reports, "engine_import": "not_run"}


def export(destination):
    destination.mkdir(parents=True, exist_ok=True)
    rig = bpy.data.objects["Armature"]
    # 선택/표시를 임시로 바꾼 뒤 복원하며 원본 파일에는 저장하지 않는다.
    old_selected = list(bpy.context.selected_objects)
    old_active = bpy.context.view_layer.objects.active
    hidden = {obj.name: (obj.hide_get(), obj.hide_render) for obj in bpy.data.objects if obj.type == "MESH"}
    files = []
    try:
        for name in NAMES:
            obj = bpy.data.objects[name]
            obj.hide_set(False)
            obj.hide_render = False
            activate(obj)
            rig.select_set(True)
            target = destination / (name + ".fbx")
            outcome = bpy.ops.export_scene.fbx(
                filepath=str(target), use_selection=True, object_types={"MESH", "ARMATURE"},
                global_scale=1.0, apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
                axis_forward="-Y", axis_up="Z", bake_space_transform=False,
                use_mesh_modifiers=False, add_leaf_bones=False, use_armature_deform_only=False,
                primary_bone_axis="Y", secondary_bone_axis="X", mesh_smooth_type="FACE",
                bake_anim=False, path_mode="AUTO", use_custom_props=False,
            )
            assert outcome == {"FINISHED"} and target.stat().st_size > 10000
            files.append({"name": target.name, "bytes": target.stat().st_size, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
    finally:
        bpy.ops.object.select_all(action="DESELECT")
        for obj in old_selected:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = old_active
        for name, (viewport, render) in hidden.items():
            bpy.data.objects[name].hide_set(viewport)
            bpy.data.objects[name].hide_render = render
    return {"status": "PASS", "files": files}


def render():
    scene = bpy.context.scene
    images = []
    for part in NAMES[1:]:
        for name in NAMES[1:]:
            obj = bpy.data.objects[name]
            obj.hide_set(name != part)
            obj.hide_render = name != part
        for weight in (0.0, 0.5, 1.0):
            bpy.data.objects[part].data.shape_keys.key_blocks["Shape"].value = weight
            target = EVIDENCE / (part + "_Shape" + str(weight) + ".png")
            scene.render.filepath = str(target)
            bpy.ops.render.render(write_still=True)
            images.append(str(target))
    return {"status": "PASS", "renders": images}


def edit_probe():
    # production 원본을 덮지 않는 복사본에서 편집·저장·재열기·재-export를 확인한다.
    shape = bpy.data.objects[NAMES[1]].data.shape_keys.key_blocks["Shape"]
    index = max(range(len(shape.data)), key=lambda i: shape.data[i].co.z)
    shape.data[index].co.z += 2.0
    expected = shape.data[index].co.z
    target = EVIDENCE / "EditProbe.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(target), check_existing=False)
    bpy.ops.wm.open_mainfile(filepath=str(target))
    assert abs(bpy.data.objects[NAMES[1]].data.shape_keys.key_blocks["Shape"].data[index].co.z - expected) < 1e-6
    result = export(EVIDENCE / "EditProbeExports")
    expected_bounds = {name: bounds(bpy.data.objects[name]) for name in NAMES}
    exported = roundtrip(EVIDENCE / "EditProbeExports", expected_bounds)
    # roundtrip이 마지막 B 장면을 남기므로 편집한 A를 별도로 재읽는다.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.scale_length = 0.01
    bpy.ops.import_scene.fbx(filepath=str(EVIDENCE / "EditProbeExports" / (NAMES[1] + ".fbx")), use_anim=False)
    reloaded = next(obj for obj in bpy.data.objects if obj.type == "MESH")
    exported_max_z = max((reloaded.matrix_world @ point.co).z for point in reloaded.data.shape_keys.key_blocks["Shape"].data)
    assert abs(exported_max_z - expected) < 1e-3
    result["roundtrip"] = exported
    result["edited_vertex"] = index
    result["retained_z_cm"] = expected
    result["exported_max_z_cm"] = exported_max_z
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("create", "verify", "export", "render", "edit-probe", "roundtrip"))
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    if args.mode == "create":
        result = create()
    else:
        # 항상 저장된 원본부터 시작한다. 열려 있는 사용자의 장면은 연결하지 않는다.
        bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
        result = verify()
        if args.mode == "export":
            result = export(ART / "Exports")
        elif args.mode == "render":
            result = render()
        elif args.mode == "edit-probe":
            result = edit_probe()
        elif args.mode == "roundtrip":
            result = roundtrip(ART / "Exports", {name: bounds(bpy.data.objects[name]) for name in NAMES})
    (EVIDENCE / (args.mode + ".json")).write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print("C3 " + args.mode + ": " + result["status"])


if __name__ == "__main__":
    main()
