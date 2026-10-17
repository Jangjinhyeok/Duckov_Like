"""부위 파츠 FBX를 disposable UE 5.7 pilot에서 import하고 공통 Skeleton/material을 연결한다(author_parts.py의 짝).

실제 프로젝트에서 실행하지 않는다. Python 플러그인만 켠 pilot 프로젝트(Content/Customization/Prototype 사본)에서
`-ExecutePythonScript="<이 파일>"`로 실행하고, 저장된 새 package만 저장소 Content로 옮긴다.
import 옵션은 import_slots.py와 같다: Skeletal Mesh, Morph Targets, animation/material/texture 없음, 공통 Skeleton 지정.
"""

import hashlib
import json
import sys
from pathlib import Path

import unreal

sys.dont_write_bytecode = True
ART = Path(sys.argv[0]).resolve().parent
sys.path.insert(0, str(ART))
import import_slots as base  # noqa: E402  pilot 확인·import·연결 규칙을 그대로 쓴다.

DEST = base.DEST
COLOR = DEST + "/M_CustomizationColor"
# 파츠 → (사용할 material, 필수 Morph). body만 머리 모양 Morph를 가진다. 발은 유형마다 고정색 material이다.
CONTRACT = {"SK_CustomizationBaseBody": (COLOR, {"HeadFlat"})}
for name in ("EyeRound", "EyeLarge", "EyeSleepy", "BeakDuck", "BeakSmall", "BeakLong",
             "WingRound", "WingSmall", "WingMitten", "TailShort", "TailFan"):
    CONTRACT["SK_Customization" + name] = (COLOR, set())
CONTRACT["SK_CustomizationFootWebbed"] = (DEST + "/M_PrototypeBeak", set())
CONTRACT["SK_CustomizationFootBoot"] = (DEST + "/M_PrototypeEye", set())


def main():
    skeleton = base.EDITOR.load_asset(base.SKELETON)
    assert isinstance(skeleton, unreal.Skeleton)
    skeleton_file = base.PILOT / "Content/Customization/Prototype/SK_CustomizationBody_Skeleton.uasset"
    skeleton_hash = hashlib.sha256(skeleton_file.read_bytes()).hexdigest()
    report = {"status": "PASS", "assets": {}}
    for name, (material_path, morphs) in CONTRACT.items():
        mesh, source = base.import_one(name, skeleton)
        material = base.connect(mesh, material_path, morphs)
        mesh.get_editor_property("asset_import_data").scripted_add_filename(str(source), 0, "")
        assert base.EDITOR.save_loaded_asset(mesh, only_if_is_dirty=False)
        bounds = mesh.get_imported_bounds()
        report["assets"][name] = {
            "material": material.get_path_name(), "morphs": sorted(map(str, mesh.get_all_morph_target_names())),
            "extent_cm": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
            "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
    report["skeleton_unchanged"] = skeleton_hash == hashlib.sha256(skeleton_file.read_bytes()).hexdigest()
    assert report["skeleton_unchanged"]
    (base.PILOT / "import-parts-report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("C-parts import: PASS " + str(len(CONTRACT)))


main()
