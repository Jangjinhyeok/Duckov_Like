"""새 슬롯 파츠 FBX를 disposable UE 5.7 pilot에서 import하고 공통 Skeleton/material을 연결한다.

실제 프로젝트에서 실행하지 않는다. Python 플러그인만 켠 pilot 프로젝트(Content/Customization/Prototype 사본)에서
`-ExecutePythonScript="<이 파일> <FBX 이름>..."`으로 실행하고, 저장된 새 package만 저장소 Content로 옮긴다.
옵션은 기존 C3 import와 같다: Skeletal Mesh, Morph Targets, animation/material/texture 없음, 공통 Skeleton 지정.
"""

import hashlib
import json
import sys
from pathlib import Path

import unreal

DEST = "/Game/Customization/Prototype"
SKELETON = DEST + "/SK_CustomizationBody_Skeleton"
# 파츠 → (사용할 material, 필수 Morph). 눈/부리 파츠는 Morph가 없다.
CONTRACT = {
    "SK_CustomizationRoundGlasses": (DEST + "/M_PrototypeEye", set()),
    "SK_CustomizationBeakRing": (DEST + "/M_CustomizationColor", set()),
    "SK_CustomizationPartC": (DEST + "/M_CustomizationColor", {"Shape"}),
}
EDITOR = unreal.EditorAssetLibrary
PILOT = Path(unreal.Paths.project_dir()).resolve()
ART = Path(sys.argv[0]).resolve().parent
assert (PILOT / "CustomizationImportPilot.uproject").exists(), "Disposable pilot only"


def import_one(name, skeleton):
    source = ART / "Exports" / (name + ".fbx")
    assert source.exists(), source
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
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
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(source), "destination_path": DEST, "destination_name": name, "automated": True,
        "replace_existing": False, "save": False, "factory": unreal.FbxFactory(), "options": options,
    }.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = EDITOR.load_asset(DEST + "/" + name)
    assert isinstance(mesh, unreal.SkeletalMesh), name
    return mesh, source


def connect(mesh, material_path, morphs):
    material = EDITOR.load_asset(material_path)
    assert isinstance(material, unreal.MaterialInterface)
    slots = list(mesh.get_editor_property("materials"))
    assert len(slots) == 1, "파츠는 material 슬롯 0만 사용한다"
    slots[0].set_editor_property("material_interface", material)
    mesh.set_editor_property("materials", slots)
    assert mesh.get_editor_property("skeleton").get_path_name().startswith(SKELETON)
    assert set(map(str, mesh.get_all_morph_target_names())) == morphs
    bounds = mesh.get_imported_bounds()
    assert bounds.box_extent.z > 1.0
    return material


def main():
    skeleton = EDITOR.load_asset(SKELETON)
    assert isinstance(skeleton, unreal.Skeleton)
    skeleton_hash = hashlib.sha256((PILOT / "Content/Customization/Prototype/SK_CustomizationBody_Skeleton.uasset").read_bytes()).hexdigest()
    report = {"status": "PASS", "assets": {}}
    for name in sys.argv[1:]:
        material_path, morphs = CONTRACT[name]
        mesh, source = import_one(name, skeleton)
        material = connect(mesh, material_path, morphs)
        mesh.get_editor_property("asset_import_data").scripted_add_filename(str(source), 0, "")
        assert EDITOR.save_loaded_asset(mesh, only_if_is_dirty=False)
        bounds = mesh.get_imported_bounds()
        report["assets"][name] = {
            "material": material.get_path_name(), "morphs": sorted(map(str, mesh.get_all_morph_target_names())),
            "extent_cm": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
    after = hashlib.sha256((PILOT / "Content/Customization/Prototype/SK_CustomizationBody_Skeleton.uasset").read_bytes()).hexdigest()
    report["skeleton_unchanged"] = skeleton_hash == after
    (PILOT / "import-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("C-slots import: PASS " + ", ".join(sys.argv[1:]))


main()
