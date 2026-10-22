"""Validate saved handoff files and rebuild its ZIP after PDF visual review."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import zipfile
from PIL import Image
from pypdf import PdfReader

BASE = Path(__file__).resolve().parents[1]
ROOT = BASE.parents[3]
SOURCE = ROOT / "ArtSource/Weapons/RMB93/v01/blockout"
PREVIEWS = ROOT / "tmp/pdfs/rmb93_v01"

for page, name in [(2,"01_Orthographic_Blockout"),
                   (3,"02_Perspective_Blockout"),(4,"03_Counter_Blockout")]:
    shutil.copy2(PREVIEWS / f"sheet02-{page:02d}.png", BASE / "Sheets" / (name+".png"))

contract = json.loads((BASE / "asset_contract.json").read_text(encoding="utf-8"))
readme = (BASE / "README.md").read_text(encoding="utf-8")
links = re.findall(r"\]\(([^)]+)\)",readme)
local_links = [s for s in links if not s.startswith(("https:","http:","#"))]
for link in local_links:
    assert (BASE / link).resolve().exists(), link
assert len(contract["textures"]) == 9
assert len({t["name"] for t in contract["textures"]}) == 9
for texture in contract["textures"]:
    assert texture["name"] in readme
    assert all(s > 0 and s & (s-1) == 0 for s in texture["size"])
for parameter in contract["counter"]["parameters"]:
    assert parameter["name"] in readme
atlas = contract["textures"][-1]
assert atlas["semantics"] == ["HotCathodeCore","OrangeGasDischarge","SoftLocalHalo"]
assert contract["counter"]["atlas"]["cells"][10] == "OFF"
assert contract["counter"]["current_magazine_capacity"] == 6
assert contract["geometry"]["right_side_counter"] is False
render_manifest = json.loads((BASE/"Renders/blockout_manifest.json").read_text(encoding="utf-8"))
assert len(render_manifest["views"]) == 15
assert render_manifest["revision"] == contract["art_revision"]
images = []
for path in sorted(BASE.rglob("*.png")):
    with Image.open(path) as im:
        images.append({"file":path.relative_to(BASE).as_posix(),"size":list(im.size),"mode":im.mode})
        im.verify()
ratios = {}
for profile in ["01_left","02_right"]:
    with Image.open(BASE/"Renders"/(profile+".png")) as im:
        bounds = im.getchannel("A").point(lambda a:255 if a>=128 else 0).getbbox()
        ratio = (bounds[2]-bounds[0])/(bounds[3]-bounds[1])
        assert 3.85 <= ratio <= 4.15, (profile, ratio)
        ratios[profile] = round(ratio,4)

pdf = BASE / "RMB93_Modeller_Sheet_v01.pdf"
reader = PdfReader(str(pdf))
assert len(reader.pages) == 10
pdf_text = "\n".join(p.extract_text() for p in reader.pages)
assert "SP_DischargeStrength" in pdf_text
assert "SP_GhostStrength" in pdf_text
assert (SOURCE/"Jam_RMB93_Blockout_v01.blend").stat().st_size > 100000
sha = lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original = Path("C:/Users/user/.codex/generated_images/01a158e3-be18-7f02-bed5-5bdda2645cf4/call_2b9ec82e2c174e13a2f5e28bbc6fe135.png")
assert sha(original) == sha(BASE/"Sheets/00_Approved_Receiver.png")
report = {
    "status":"brief_checks_passed", "date":"2026-10-20",
    "revision":contract["art_revision"], "contract_schema":contract["schema_version"],
    "pdf_pages":len(reader.pages), "images":images,
    "local_links_checked":len(local_links), "texture_contracts":9,
    "parameter_names_checked":len(contract["counter"]["parameters"]),
    "reference_blockout_triangles":render_manifest["triangles_in_preview_scene"],
    "rendered_profile_length_height_ratio":ratios,
    "approved_master_unchanged":True,
    "visual_review":"All 10 PDF pages inspected; revised page 4 inspected after final composition. Six orthographic, four perspective and four counter states inspected. New art target preserves the approved exterior composition.",
    "pdf_sha256":sha(pdf),
    "not_verified":["user_acceptance_of_revision_02","production_mesh_UV_and_PBR_maps",
                    "FBX_roundtrip","UE_material_and_import","in_game_counter_readability",
                    "runtime_behavior_and_cooked_Windows"]
}
(BASE/"brief_validation.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")

files = sorted(p for folder in [BASE,SOURCE] for p in folder.rglob("*")
               if p.is_file() and "__pycache__" not in p.parts and p.name!="file_manifest.json")
manifest = [{"file":p.relative_to(ROOT).as_posix(),"bytes":p.stat().st_size,"sha256":sha(p)} for p in files]
manifest_path = BASE/"file_manifest.json"
manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding="utf-8")
files.append(manifest_path)
archive = ROOT/"output/art/RMB93_Modeller_Package_v01.zip"
archive.parent.mkdir(parents=True,exist_ok=True)
prefix = "RMB93_Modeller_Package_v01/"
with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as z:
    for path in files:
        z.write(path,prefix+path.relative_to(ROOT).as_posix())
    z.writestr(prefix+"START_HERE.txt",
        "РМБ-93 / v01, правка 02: пропорции и газоразрядный счётчик.\n"
        "Откройте Docs/Art/RMB93/v01/RMB93_Modeller_Sheet_v01.pdf (10 страниц).\n"
        "Полный контракт 1.1: README.md и asset_contract.json рядом.\n"
        "Боковое фото определяет пропорции; 04_Nixie_Art_Target_rev02.png задаёт характер лампы.\n"
        "Исходник блокаута: ArtSource/Weapons/RMB93/v01/blockout/.\n"
        "Это бриф и пространственный ориентир; производственные FBX, UV, PBR и интеграция UE ещё не выполнены.\n")
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    count = len(z.infolist())
print(json.dumps({"status":"PASS","pdf_pages":10,"png_files":len(images),
                  "profile_ratios":ratios,"zip_files":count,
                  "zip_MiB":round(archive.stat().st_size/1024**2,1)},ensure_ascii=False))
