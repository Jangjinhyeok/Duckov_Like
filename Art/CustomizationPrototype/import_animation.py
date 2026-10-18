"""플레이어 애니메이션 FBX를 disposable UE 5.7 pilot에서 import한다(author_animation.py의 짝).

실제 프로젝트에서 실행하지 않는다. Python 플러그인만 켠 pilot 프로젝트(Content/Customization/Prototype 사본)에서
`-ExecutePythonScript="<이 파일>"`로 실행하고, 저장된 package만 저장소 Content로 옮긴다.
1) body·발 mesh를 7 bones FBX로 같은 asset에 다시 import한다. 공통 Skeleton에 foot_l/foot_r가 merge된다.
   옵션은 import_slots.py와 같다(Skeletal Mesh, Morph Targets, animation/material/texture 없음, 공통 Skeleton 지정).
2) A_DuckIdle/A_DuckWalk를 같은 Skeleton의 AnimSequence로 /Game/Character에 import한다.
   축 변환은 mesh와 같다(Convert Scene, Force Front X Axis, Convert Scene Unit). root motion은 없다.
BlendSpace는 Python에서 sample을 넣을 수 없어 본 프로젝트의 CharacterAnimationAssets.Create가 만든다.
"""

import hashlib
import json
import sys
from pathlib import Path

import unreal

sys.dont_write_bytecode = True
ART = Path(sys.argv[0]).resolve().parent
sys.path.insert(0, str(ART))
import import_slots as base  # noqa: E402  pilot 확인·material 연결 규칙을 그대로 쓴다.

ANIM_DEST = "/Game/Character"
MESHES = {
    "SK_CustomizationBaseBody": (base.DEST + "/M_CustomizationColor", {"HeadFlat"}),
    "SK_CustomizationFootWebbed": (base.DEST + "/M_PrototypeBeak", set()),
    "SK_CustomizationFootBoot": (base.DEST + "/M_PrototypeEye", set()),
}
# 이름 -> (길이 초, frame 수). author_animation.py의 30 fps 루프와 같다.
ANIMATIONS = {"A_DuckIdle": (2.0, 60), "A_DuckWalk": (16 / 30, 16)}
BONES = {"root", "body", "head", "wing_l", "wing_r", "foot_l", "foot_r"}


def run_task(name, destination, options):
    source = ART / "Exports" / (name + ".fbx")
    assert source.exists(), source
    # 기존 import와 같은 legacy FbxFactory 경로를 쓴다. 켜 두면 mesh가 Interchange로 처리된다.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(source), "destination_path": destination, "destination_name": name, "automated": True,
        "replace_existing": True, "save": False, "factory": unreal.FbxFactory(), "options": options,
    }.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return base.EDITOR.load_asset(destination + "/" + name), source


def reimport_mesh(name, skeleton):
    options = unreal.FbxImportUI()
    for key, value in {
        "automated_import_should_detect_type": False,
        "mesh_type_to_import": unreal.FBXImportType.FBXIT_SKELETAL_MESH,
        "import_as_skeletal": True, "import_mesh": True, "import_animations": False,
        "import_materials": False, "import_textures": False, "create_physics_asset": False,
        "skeleton": skeleton,
    }.items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("skeletal_mesh_import_data")
    for key, value in {
        "import_morph_targets": True, "update_skeleton_reference_pose": False,
        "use_t0_as_ref_pose": False, "convert_scene": True, "convert_scene_unit": True,
        "force_front_x_axis": True, "import_uniform_scale": 1.0,
        "normal_import_method": unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    }.items():
        data.set_editor_property(key, value)
    mesh, source = run_task(name, base.DEST, options)
    assert isinstance(mesh, unreal.SkeletalMesh), name
    return mesh, source


def import_animation(name, skeleton):
    options = unreal.FbxImportUI()
    for key, value in {
        "automated_import_should_detect_type": False,
        "mesh_type_to_import": unreal.FBXImportType.FBXIT_ANIMATION,
        "import_as_skeletal": False, "import_mesh": False, "import_animations": True,
        "import_materials": False, "import_textures": False, "create_physics_asset": False,
        "skeleton": skeleton,
    }.items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("anim_sequence_import_data")
    for key, value in {
        "convert_scene": True, "convert_scene_unit": True, "force_front_x_axis": True, "import_uniform_scale": 1.0,
        "animation_length": unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
        "import_bone_tracks": True, "import_custom_attribute": False, "remove_redundant_keys": False,
        "use_default_sample_rate": False, "import_meshes_in_bone_hierarchy": False,
    }.items():
        data.set_editor_property(key, value)
    anim, source = run_task(name, ANIM_DEST, options)
    assert isinstance(anim, unreal.AnimSequence), name
    return anim, source


def component_z(anim, time, bone):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, time, unreal.AnimPoseEvaluationOptions())
    return unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation.z


def main():
    skeleton = base.EDITOR.load_asset(base.SKELETON)
    assert isinstance(skeleton, unreal.Skeleton)
    report = {"status": "PASS", "meshes": {}, "animations": {}}
    for name, (material_path, morphs) in MESHES.items():
        mesh, source = reimport_mesh(name, skeleton)
        material = base.connect(mesh, material_path, morphs)
        mesh.get_editor_property("asset_import_data").scripted_add_filename(str(source), 0, "")
        assert base.EDITOR.save_loaded_asset(mesh, only_if_is_dirty=False)
        bounds = mesh.get_imported_bounds()
        report["meshes"][name] = {"material": material.get_path_name(),
                                  "extent_cm": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
                                  "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
    bones = set(map(str, unreal.AnimPoseExtensions.get_bone_names(unreal.AnimPoseExtensions.get_reference_pose(skeleton))))
    assert bones == BONES, bones
    assert base.EDITOR.save_loaded_asset(skeleton, only_if_is_dirty=False)
    report["skeleton_bones"] = sorted(bones)
    for name, (seconds, frames) in ANIMATIONS.items():
        anim, source = import_animation(name, skeleton)
        assert anim.get_editor_property("skeleton") == skeleton, name
        length = anim.get_play_length()
        keys = unreal.AnimationLibrary.get_num_frames(anim)
        assert abs(length - seconds) < 1e-3 and keys == frames, (name, length, keys)
        anim.get_editor_property("asset_import_data").scripted_add_filename(str(source), 0, "")
        assert base.EDITOR.save_loaded_asset(anim, only_if_is_dirty=False)
        item = {"seconds": length, "frames": keys, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
        if name == "A_DuckWalk":
            # phase 0: 왼발이 6 cm 들리고 오른발은 땅에 있다. 축 변환 뒤에도 위(+Z)가 유지되는지 본다.
            lift = component_z(anim, 0.0, "foot_l") - component_z(anim, 0.0, "foot_r")
            assert abs(lift - 6.0) < 0.1, lift
            item["foot_l_minus_foot_r_z_at_0_cm"] = lift
        report["animations"][name] = item
    (base.PILOT / "import-animation-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("C-anim import: PASS")


main()
