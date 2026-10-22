"""Create a modeller handoff from actual saved renders, then hash/package it."""
from pathlib import Path
import json,hashlib,zipfile,shutil
from PIL import Image
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A3,landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.utils import ImageReader
from pypdf import PdfReader
R=Path(__file__).resolve().parents[1];REPO=R.parents[3]
stats=json.loads((R/'validation.json').read_text())
OUT=REPO/'Docs/Art/RMB93/v02';OUT.mkdir(parents=True,exist_ok=True)
pdfmetrics.registerFont(TTFont('Arial','C:/Windows/Fonts/arial.ttf'))
pdfmetrics.registerFont(TTFont('ArialB','C:/Windows/Fonts/arialbd.ttf'))
W,H=landscape(A3);pdf=R/'RMB93_Modeller_Sheet_v02.pdf';c=canvas.Canvas(str(pdf),pagesize=(W,H));c.setTitle('MindKore RMB93 v02 — actual model handoff')
def text(x,y,string,size=12,color='#c7c7bc',bold=False):
    c.setFillColor(color);c.setFont('ArialB' if bold else 'Arial',size);c.drawString(x,y,string)
def page(title,number):
    c.setFillColor('#171c19');c.rect(0,0,W,H,fill=1,stroke=0)
    text(34,H-42,'MINDKORE  /  RMB93',11,'#c18d56',True);text(34,H-78,title,25,'#ebe9dd',True)
    text(34,22,'v02 · Blender / GLB / FBX · реальные кадры · статус UE указан отдельно',10)
    text(W-65,22,f'{number:02}',11,'#c18d56')
def picture(name,x,y,w,h,label=None,crop=False):
    p=R/name if (R/name).is_file() else R/'renders'/name
    with Image.open(p) as im:iw,ih=im.size
    k=(max if crop else min)(w/iw,h/ih);dw,dh=iw*k,ih*k
    c.saveState();clip=c.beginPath();clip.rect(x,y,w,h);c.clipPath(clip,stroke=0)
    c.drawImage(ImageReader(str(p)),x+(w-dw)/2,y+(h-dh)/2,dw,dh,mask='auto')
    c.restoreState()
    if label:text(x+5,y+7,label,11,'#e0dacc')
page('Редактируемая игровая модель',1)
picture('11_hero.png',34,120,W-68,H-225)
text(34,93,f"{stats['triangles_total']:,} треугольников · {stats['parts']} редактируемых деталей · 5 экспортных сборок · 2 материала".replace(',', ' '),14,'#ebe9dd')
text(34,66,'Короткий силуэт принят пользователем. Счётчик перенесён высоко назад; прицелы и опора переработаны.',12)
c.showPage()
page('Ортографические проекции',2)
for name,x,y,label in [('05_left.png',34,440,'Слева'),('06_right.png',W/2+10,440,'Справа'),('07_top.png',34,95,'Сверху'),('08_bottom.png',W/2+10,95,'Снизу')]:picture(name,x,y,W/2-45,310,label)
c.showPage()
page('Торцы и объём',3)
picture('09_front.png',34,130,290,590,'Спереди',True);picture('10_rear.png',334,130,290,590,'Сзади',True);picture('11_hero.png',634,130,W-668,590,'Передняя четверть')
text(34,70,'В .blend сохранены отдельные детали. В GLB они собраны по назначению для удобного просмотра.',12)
c.showPage()
page('ADS и задний индикатор',4)
picture('03_ads.png',34,190,W*.53-42,520,'Перспективная камера; финальный игровой FOV проверяется в UE')
picture('17_live_counter_3_equal_height.png',W*.53+8,190,W*.47-42,520,'WebGL: слои катодов и одинаковая высота цифр',True)
text(34,132,'Мушка и целик сохранены в принятом тонком силуэте. Опора получила сужения и выборку сверху.',13)
text(34,103,'Счётчик остаётся на Body. Цифры читаются независимо от положения Action; загрузочный доступ свободен.',12)
text(34,74,'Проволочный контур / газовая оболочка / слабый ореол — три отдельных канала атласа.',12)
c.showPage()
page('Движение и доступ к загрузке',5)
picture('12_pump_forward.png',34,235,W/2-45,465,'Action: демонстрационное переднее положение')
picture('13_load_access.png',W/2+10,235,W/2-45,465,'Открытый доступ: Stock + LoadingLid')
lines=['Action = нижний ствол + цевьё + передние кольца. Верхняя труба и коробка остаются на месте.',
       'Проверены 4 критичные пары деталей в 21 положении; пересечений этих пар не обнаружено.',
       'Поза загрузки служит для проверки пространства; полноценная анимация рук ещё не поставлена.',
       'InsertAccepted изменяет игровой боезапас один раз. Работа помпы не добавляет и не списывает патрон.',
       'Состояния: Ready → ReloadOpen → Insert → ReloadClose → Ready. Выход требует закрытия крышки.']
for i,line in enumerate(lines):text(34,180-i*25,line,12)
c.showPage()
page('Текстуры и эмиссия',6)
picture('counter/Counter_Digit_States.png',34,420,W-68,300)
rows=[('Body BC / N / ORM','4096²','sRGB / Linear / Linear'),('Body Masks','1024²','R wear · G dirt · B reserved (0)'),('Counter BC / N / ORM / Masks','512²','Masks: R aperture · G inactive wires · B glass dirt'),('DigitAtlas_M','1024² / 4×4','R hot wire · G gas · B halo; 0–9, OFF=10')]
for i,(a,b,d) in enumerate(rows):
    y=350-i*39;text(34,y,a,13,'#ebe9dd',True);text(370,y,b,12);text(520,y,d,12)
text(34,157,'N: DirectX Y− для UE. ORM: AO / Roughness / Metallic. Все девять карт — 8-bit RGB PNG.',12)
text(34,128,'AtlasUV = (cell × 256 + 16.5 + saturate(LocalUV) × 223) / 1024',12,'#c18d56')
text(34,99,'UE: общий distance/illumination atlas и материал из ArtSource/Instruments/Nixie; Docs/Nixie.md.',12)
text(34,70,'Полный контракт анимации и статус проверок: ArtSource/Weapons/RMB93/v02/README.md.',12)
c.showPage()
page('Материал хвата: смола и прессованное волокно',7)
picture('19_live_foreend.png',34,400,W-68,330,'Цевьё · одинаковый шаг восьми прорезей')
picture('20_live_grip.png',34,85,W*.55,310,'Рукоятка · корпус и более плотная накладка')
for i,line in enumerate(['Цевьё: тёмный прессованный композит.', 'Рукоятка: плотная смола и сухая накладка.', 'Настоящее углубление, узкий тёмный шов.', 'Износ цвета и блеска в местах касания.', 'Кадры из WebGL; освещение сохранено.']):
    text(W*.59,320-i*34,line,12)
c.showPage()
page('Приклад: открытая рамка и затыльник',8)
picture('21_live_stock_folded.png',34,370,W-68,365,'Сложен · профилированные боковины и дальний затыльник')
picture('24_live_stock_deployed.png',34,75,W*.54,285,'Развёрнут назад · затыльник обращён к плечу')
for i,line in enumerate(['Цельная открытая рамка; шарнир сохранён.', 'Крупная накладка возле шарнира удалена.', 'Затыльник расположен на дальнем конце.', 'Stock остаётся отдельной сборкой.', 'Открытая скоба свободно проходит над трубами.', 'Проверены доступ и разворот: по 21 положению.']):
    text(W*.59,300-i*30,line,12)
c.showPage()
page('Затыльник: стальная основа и формованная резина',9)
picture('25_live_buttpad.png',34,180,W-68,555,'WebGL · контактная поверхность разложенного приклада')
text(34,132,'Скруглённая накладка, три неглубокие канавки на каждой стороне, видимый посадочный уступ.',13)
text(34,103,'Базовая резина: графит, Metallic 0, roughness 0.76-0.84; локальный контакт меняет блеск.',12)
text(34,74,'Кнопка «Затыльник» следует текущей позе. Посадка на плечо персонажа ещё не проверена.',12)
c.showPage()
page('Спусковой узел: форма и следы эксплуатации',10)
picture('26_live_controls.png',34,180,W-68,555,'WebGL · левый рычаг, скоба и спуск')
text(34,132,'Сужающийся рычаг с посадочным швом, скруглённая скоба и плавная поверхность спуска.',13)
text(34,103,'Гранж: локальные потёртости воронения, короткие направленные риски, тёмная посадка рычага.',12)
text(34,74,'Фактура и маска износа запечены в Body. Внутренний механизм и анимация пальца не моделировались.',12)
c.showPage()
page('Покрытия: характер обработки и локальный износ',11)
picture('15_live_left.png',34,300,W-68,430,'WebGL · общий вид с обновлёнными PBR-картами')
for i,line in enumerate(['Износ распределён по коробке, трубам, прикладу, цевью, рукоятке и спусковому узлу.',
                        'Направленные потёртости, локальные царапины и загрязнение швов; без сплошной ржавчины.',
                        'Body Masks: R — износ; G — загрязнение и контактное затемнение; B — 0.',
                        'Геометрия рукоятки: 888 треугольников, пять деталей. Два игровых материала сохранены.',
                        'Девять исходных покрытий запечены в Body + Counter; дополнительные игровые слоты не нужны.',
                        'Текущая оценка и ограничения: ART_REVIEW_PREMIUM_2026-10-21.txt.']):
    text(34,245-i*33,line,12)
c.showPage()
page('Один материал в двух вариантах света',12)
picture('20_live_grip.png',34,430,W/2-45,285,'Рукоятка · Neutral')
picture('28_live_grip_hard.png',W/2+10,430,W/2-45,285,'Рукоятка · Hard')
picture('17_live_counter_3_equal_height.png',34,110,W/2-45,285,'Счётчик · Neutral')
picture('27_live_counter_hard.png',W/2+10,110,W/2-45,285,'Счётчик · Hard')
text(34,70,'Экспозиция и материалы не меняются. Микрофактура, блики и разделение стыков проверяются светом.',12)
c.showPage()
page('Коробка: единая формованная боковина',13)
picture('31_live_receiver.png',34,190,W-68,520,'WebGL · штампованная плоскость и исходное расположение органов управления')
text(34,132,'Неглубокая выемка сформирована в самой коробке; отдельной накладной панели нет.',13)
text(34,103,'Тонкий уступ, спокойный сатин и небольшое заводское клеймо; износ ограничен кромками.',12)
text(34,74,'Коробка: 992 треугольника. Общий бюджет: 11 846. Силуэт, прицелы и расположение рычага сохранены.',12)
pages=13
if (R/'renders/29_ue_hip.png').exists() and (R/'renders/30_ue_ads.png').exists():
    c.showPage();page('Проверка штатной камеры UE',14)
    picture('29_ue_hip.png',34,310,W/2-45,410,'HIP · игровая камера')
    picture('30_ue_ads.png',W/2+10,310,W/2-45,410,'ADS · игровая камера')
    for i,line in enumerate(['Отдельные preview-ассеты, временная привязка в PIE; исходные игровые ассеты не заменены.',
                            'Настоящая камера проекта, FirstPerson FOV и штатная постобработка.',
                            'UE-счётчик: общий Nixie-материал, резкий distance-контур, газовое свечение и слои катодов.',
                            'Это проверка вида и читаемости. Перезарядка, хват рук и cooked-версия не приняты.']):
        text(34,240-i*34,line,12)
    pages=14
if (R/'renders/35_ue_nixie_detail.png').exists() and (R/'renders/36_ue_nixie_instances.png').exists():
    c.showPage();pages+=1;page('Nixie — общий приборный материал',pages)
    picture('35_ue_nixie_detail.png',34,330,W/2-45,390,'UE · диагностический крупный план')
    picture('36_ue_nixie_instances.png',W/2+10,330,W/2-45,390,'UE · два независимых экземпляра')
    for i,line in enumerate(['Свечение следует контурам цифры; локальные участки разряда слегка мерцают независимо.',
                            'Стекло сохраняет отражение, через которое читаются проволочные слои.',
                            'BP_Jam_NixieDisplay + JamNixieCounterComponent: цифра, питание, отдельный MID каждого прибора.',
                            'ArtSource/Instruments/Nixie — общий шейдер и сборщик. Docs/Nixie.md — подключение и проверки.',
                            'Виртуальная глубина в непрозрачном материале; это художественная имитация газоразрядного прибора.']):
        text(34,270-i*34,line,12)
c.showPage();page('UV-развёртки и комплект рядом с моделью',pages+1)
c.setFillColor('#d8d8d0');c.rect(34,205,W/2-45,510,fill=1,stroke=0);c.rect(W/2+10,205,W/2-45,510,fill=1,stroke=0)
picture('UV_Body.png',34,205,W/2-45,510)
picture('UV_Counter.png',W/2+10,205,W/2-45,510)
text(39,190,'Body · общая развёртка деталей материала Body',11)
text(W/2+15,190,'Counter · собственные UV 0–1',11)
for i,line in enumerate(['PNG и SVG экспортированы из сохранённого Jam_RMB93_v02.blend, без изменения модели.',
                        'Body: BC / N / ORM 4096², Masks 1024². Counter: четыре карты 512², DigitAtlas 1024².',
                        'BC — sRGB. N, ORM, Masks и атлас — Linear; Normal DirectX, ORM = AO / Roughness / Metallic.',
                        'README.md — описание и сборки; UV_LAYOUTS.txt — развёртки; counter/SHADER_HOOKUP.md — цифры.']):
    text(34,166-i*29,line,12)
pages+=1
c.showPage();c.save()
assert len(PdfReader(pdf).pages)==pages
shutil.copy2(pdf,OUT/pdf.name)
report=json.loads((R/'roundtrip_validation.json').read_text())
assert report['status']=='pass'
print('PDF_CREATED',pdf)
# Package excludes dependencies, logs, caches, previous comparison pictures.
files=[p for p in R.rglob('*') if p.is_file() and not any(x in p.parts for x in ('node_modules','__pycache__')) and p.suffix not in ('.log','.pyc','.blend1','.blend@')]
files.extend(p for p in OUT.iterdir() if p.is_file() and p.name!='file_manifest.json')
# Shared instrument feature is a dependency of the UE counter, not duplicated
# weapon-specific shader code. Preserve repository-relative paths in the ZIP.
for root in (REPO/'ArtSource/Instruments/Nixie', REPO/'Content/JamArt/Materials/Instruments/Nixie'):
    if root.exists():
        files.extend(p for p in root.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.log'))
for relative in ('Source/MindKore/JamNixieCounter.h','Source/MindKore/JamNixieCounter.cpp','Docs/Nixie.md'):
    if (REPO/relative).is_file(): files.append(REPO/relative)
files=sorted(set(files))
manifest=[{'path':p.relative_to(REPO).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files]
(OUT/'file_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
zpath=REPO/'output/art/RMB93_Model_v02.zip';zpath.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(zpath,'w',zipfile.ZIP_DEFLATED,6) as z:
    for p in files:z.write(p,p.relative_to(REPO))
    z.write(OUT/'file_manifest.json',(OUT/'file_manifest.json').relative_to(REPO))
with zipfile.ZipFile(zpath) as z:assert z.testzip() is None
print('PACKAGE_CREATED',zpath,len(files))
