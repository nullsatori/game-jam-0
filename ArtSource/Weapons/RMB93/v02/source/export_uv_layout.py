"""Export the actual saved native UV0 without resaving the model.

Run with Blender --background --python source/export_uv_layout.py.
"""
import bpy
import gpu
import hashlib
import json
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
NATIVE=ROOT/'Jam_RMB93_v02.blend'
before=hashlib.sha256(NATIVE.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(NATIVE))
gpu.init()  # Blender 5.2's PNG UV exporter needs a background drawing context.
objects=[ob for ob in bpy.context.scene.objects if ob.type=='MESH' and 'assembly' in ob]
results=[]
for material_name,resolution in [('Body',4096),('Counter',512)]:
    selected=[ob for ob in objects if any(m and m.name==material_name for m in ob.data.materials)]
    assert selected,material_name
    assert all(ob.data.uv_layers.active for ob in selected),material_name
    # Current delivery assigns exactly one runtime material per editable part.
    # Refuse to silently export unrelated faces if that contract changes.
    assert all(len(ob.data.materials)==1 and ob.data.materials[0].name==material_name for ob in selected)
    bpy.ops.object.select_all(action='DESELECT')
    for ob in selected:
        ob.hide_set(False);ob.select_set(True)
    bpy.context.view_layer.objects.active=selected[0]
    faces=sum(len(ob.data.polygons)for ob in selected)
    files=[]
    for mode,extension in [('SVG','svg'),('PNG','png')]:
        path=ROOT/f'UV_{material_name}.{extension}'
        result=bpy.ops.uv.export_layout(filepath=str(path),export_all=True,
            export_tiles='NONE',modified=False,mode=mode,size=(resolution,resolution),
            opacity=.10,check_existing=False)
        assert 'FINISHED' in result and path.exists() and path.stat().st_size>0,path
        if extension=='png':
            header=path.read_bytes()[:26]
            assert header[:8]==b'\x89PNG\r\n\x1a\n'
            actual=struct.unpack('>II',header[16:24])
            assert actual==(resolution,resolution),(path,actual)
            files.append({'file':path.name,'pixels':list(actual),'bytes':path.stat().st_size})
        else:
            path.write_text('\n'.join(line.rstrip() for line in path.read_text(encoding='utf-8').splitlines())+'\n',encoding='utf-8')
            document=ET.parse(path)
            polygons=len(document.findall('.//{http://www.w3.org/2000/svg}polygon'))
            assert polygons==faces,(path,polygons,faces)
            files.append({'file':path.name,'polygons':polygons,'bytes':path.stat().st_size})
    results.append({'material':material_name,'uv_layer':'UVMap','parts':len(selected),
                    'faces':faces,'resolution':resolution,'files':files})
after=hashlib.sha256(NATIVE.read_bytes()).hexdigest()
assert before==after,'The source blend must remain unchanged'
text='''РАЗВЁРТКА RMB93 — СОХРАНЁННАЯ МОДЕЛЬ

Источник: Jam_RMB93_v02.blend, активный UV0 финальных редактируемых деталей.
PNG и SVG экспортированы непосредственно Blender из сохранённой модели.
Показаны реальные треугольники развёртки, включая мелкие фаски и внутренние
поверхности. SVG можно редактировать как вектор; это не концептуальная схема.

UV_Body.png / UV_Body.svg — общий атлас Body, 4096 × 4096.
Все детали материала Body используют одну общую развёртку и один атлас.

UV_Counter.png / UV_Counter.svg — отдельный атлас Counter, 512 × 512.
Он соответствует поверхности дисплея. DigitAtlas_M 1024 × 1024 содержит
ячейки цифр для шейдера и не является отдельной геометрической развёрткой.

Материалов во время игры: два — Body и Counter. Развёртки сохраняют UV0
модели, без дополнительной перепаковки или изменения масштаба островов.
В PNG прозрачный фон; линии и слабая заливка помогают читать треугольники.
PNG имеют точный размер соответствующих основных атласов.

Повторный экспорт: Blender --background --python source/export_uv_layout.py
Экспорт не сохраняет изменения в .blend и не меняет модель или игровые экспорты.
'''
(ROOT/'UV_LAYOUTS.txt').write_text(text,encoding='utf-8')
print('UV_LAYOUT_EXPORT '+json.dumps({'native_sha256':before,'native_unchanged':True,'layouts':results}))
