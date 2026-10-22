"""Build the modeller handoff PDF from saved art, one-scene renders and contract."""
from pathlib import Path
import html
import json
from reportlab.pdfgen import canvas
from reportlab.lib import colors
from reportlab.lib.pagesizes import A3, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph, Table, TableStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.utils import ImageReader

BASE = Path(__file__).resolve().parents[1]
RENDERS = BASE / "Renders"
CONTRACT = json.loads((BASE / "asset_contract.json").read_text(encoding="utf-8"))
W, H = landscape(A3)
M = 40
INNER = W - M*2
INK = colors.HexColor("#202827")
MUTED = colors.HexColor("#626967")
AMBER = colors.HexColor("#a56829")
PAPER = colors.HexColor("#f3f2ee")
LINE = colors.HexColor("#d0d1c9")
PANEL = colors.HexColor("#e8e8e1")

pdfmetrics.registerFont(TTFont("Arial", "C:/Windows/Fonts/arial.ttf"))
pdfmetrics.registerFont(TTFont("ArialB", "C:/Windows/Fonts/arialbd.ttf"))
pdfmetrics.registerFont(TTFont("Mono", "C:/Windows/Fonts/consola.ttf"))
OUT = BASE / "RMB93_Modeller_Sheet_v01.pdf"
c = canvas.Canvas(str(OUT), pagesize=(W,H))
c.setTitle("РМБ 93 Лист для моделлера и контракт UE5")
c.setAuthor("MindKore / game-jam-0")
page_num = 0


def clean(s):
    return str(s).replace("−","-").replace("–","-").replace("—","-").replace("\u2011","-")


def text(s, x, top, size=12, font="Arial", color=INK):
    c.setFillColor(color)
    c.setFont(font,size)
    c.drawString(x,H-top-size*.8,clean(s))


def para(s, x, top, width, size=13, color=INK, bold=False, leading=None):
    style=ParagraphStyle("p",fontName="ArialB" if bold else "Arial",fontSize=size,
                         leading=leading or size*1.35,textColor=color,spaceAfter=0)
    obj=Paragraph(clean(s),style)
    _,height=obj.wrap(width,H)
    obj.drawOn(c,x,H-top-height)
    return top+height


def rect(x,top,width,height,fill=PANEL,stroke=None):
    c.setFillColor(fill)
    c.setStrokeColor(stroke or fill)
    c.rect(x,H-top-height,width,height,fill=1,stroke=bool(stroke))


def rule(top):
    c.setStrokeColor(LINE)
    c.setLineWidth(.6)
    c.line(M,H-top,W-M,H-top)


def page(title,sub):
    global page_num
    if page_num:
        c.showPage()
    page_num+=1
    rect(0,0,W,H,PAPER)
    text("MINDKORE   /   GAME-JAM-0",M,22,10,"ArialB",MUTED)
    text(title,M,48,27,"ArialB")
    para(sub,M,86,INNER,12,MUTED)
    rule(116)
    rule(H-39)
    text("RMB93  /  v01 rev.02  /  20.10.2026",M,H-26,9,color=MUTED)
    text("Внешний игровой ассет. Производственные UV и импорт UE - следующий этап.",M+240,H-26,9,color=MUTED)
    text(f"{page_num:02d} / 10",W-M-42,H-26,9,"ArialB",MUTED)


def image(path,x,top,width,height):
    source=ImageReader(str(path))
    iw,ih=source.getSize()
    scale=min(width/iw,height/ih)
    dw,dh=iw*scale,ih*scale
    c.drawImage(source,x+(width-dw)/2,H-top-height+(height-dh)/2,
                width=dw,height=dh,mask="auto")


def card_image(file,label,x,top,width,height,sub=None):
    rect(x,top,width,height,colors.HexColor("#e9e9e3"))
    text(label,x+13,top+12,11,"ArialB",MUTED)
    image(RENDERS/file,x+6,top+32,width-12,height-44-(16 if sub else 0))
    if sub:
        text(sub,x+13,top+height-18,9,color=MUTED)


def table(rows,widths,x,top,size=11.5):
    styles=[ParagraphStyle("cell",fontName="ArialB" if ri==0 else "Arial",
                          fontSize=size,leading=size*1.3,textColor=INK)
            for ri in range(len(rows))]
    cells=[[Paragraph(clean(html.escape(str(v))).replace("\n","<br/>"),styles[ri])
            for v in row] for ri,row in enumerate(rows)]
    t=Table(cells,colWidths=widths,hAlign="LEFT")
    t.setStyle(TableStyle([
        ("BACKGROUND",(0,0),(-1,0),PANEL),
        ("VALIGN",(0,0),(-1,-1),"TOP"),
        ("LEFTPADDING",(0,0),(-1,-1),10),("RIGHTPADDING",(0,0),(-1,-1),10),
        ("TOPPADDING",(0,0),(-1,-1),8),("BOTTOMPADDING",(0,0),(-1,-1),8),
        ("LINEBELOW",(0,0),(-1,-1),.45,LINE),
    ]))
    _,h=t.wrap(sum(widths),H)
    assert top+h < H-48, f"Table overflow on page {page_num}: {top+h}"
    t.drawOn(c,x,H-top-h)
    return top+h


# 01: accepted image is the material/design authority, not the proxy render.
page("РМБ 93  /  Лист для моделлера",
     "Принятый характер оружия, согласованные ракурсы и контракт PBR/эмиссив для UE 5.8.")
image(BASE/"Sheets/00_Approved_Receiver.png",M,132,INNER,386)
rule(530)
left=M
right=M+INNER*.54
para("Что сохраняем",left,550,INNER*.48,17,bold=True)
para("Тёмная сталь, отдельная пистолетная рукоять, сложенный верхний приклад, "
     "крупное рифление цевья. Слева в коробку утоплено небольшое окно "
     "с тонкой янтарной газоразрядной цифрой.",left,582,INNER*.48,14)
para("Крепёж расположен как на выбранном образце: небольшие пины и низкий плоский рычаг. "
     "Отдельной панели с винтами, шкалы и дисплея справа нет.",left,666,INNER*.48,13)
para("Как пользоваться пакетом",right,550,INNER*.46,17,bold=True)
para("Этот кадр задаёт материал и художественный характер. Следующие листы сняты "
     "с одного простого блокаута и задают форму, стороны и взаимное положение частей. "
     "Его упрощённые материалы не являются финальным текстурным пакетом.",right,582,INNER*.46,14)
para("Полный контракт: README.md. Имена и параметры: asset_contract.json. "
     "Исходник блокаута приложен в ArtSource/Weapons/RMB93/v01/blockout.",right,688,INNER*.46,12,MUTED)

# 02: consistent orthographic cameras, equal side-profile scales.
page("Основные проекции",
     "Один 3D-блокаут, закрытая поза, сложенный приклад. Стороны определяются от держащего оружие.")
gap=18
cw=(INNER-gap)/2
card_image("01_left.png","01  СЛЕВА  /  СЧЁТЧИК И РЫЧАГ",M,132,cw,235)
card_image("02_right.png","02  СПРАВА  /  БЕЗ СЧЁТЧИКА И РЫЧАГА",M+cw+gap,132,cw,235)
card_image("03_top.png","03  СВЕРХУ",M,381,cw,160)
card_image("04_bottom.png","04  СНИЗУ",M+cw+gap,381,cw,160)
card_image("05_front.png","05  СПЕРЕДИ",M,555,cw,222,
           "Верхний торец закрыт; нижний - видимое отверстие.")
card_image("06_rear.png","06  СЗАДИ",M+cw+gap,555,cw,222,
           "Неподвижная коробка и отдельная рукоять.")

# 03: four views of the same source. No generated-view mirroring.
page("Ракурсы в три четверти",
     "Силуэт и расположение окна проверять по одной сцене. Low-poly: крупные плоскости и экономные фаски.")
card_image("07_front_left.png","07  СПЕРЕДИ СЛЕВА",M,132,cw,310)
card_image("08_rear_left.png","08  СЗАДИ СЛЕВА",M+cw+gap,132,cw,310)
card_image("09_front_right.png","09  СПЕРЕДИ СПРАВА",M,458,cw,310)
card_image("10_rear_right.png","10  СЗАДИ СПРАВА",M+cw+gap,458,cw,310)

# 04: appearance and distinct empty/off states, not fabricated texture masks.
page("Газоразрядный счётчик / эффект старых часов",
     "Узкая светлая сердцевина, оранжевый разряд и тёплый ореол; слои тёмных электродов под стеклом.")
image(BASE/"Sheets/04_Nixie_Art_Target_rev02.png",M,132,INNER,375)
para("Сверху: художественный ориентир свечения и стекла. Снизу: состояния из 3D-блокаута; "
     "его материалы иллюстративные. Окно неподвижно, слева, над рычагом.",M,512,INNER,11.5,MUTED)
sw=(INNER-3*14)/4
for j,(state,caption) in enumerate([("0","ПУСТО  /  СВЕТИТСЯ 0"),("4","ПРИМЕР  /  4"),("6","ТЕКУЩАЯ ЁМКОСТЬ  /  6"),("OFF","OFF  /  ЭМИССИВ = 0")]):
    card_image(f"12_counter_{state}.png",caption,M+j*(sw+14),545,sw,222)

# 05: concise production geometry contract.
page("Геометрия и сборка",
     "Цели цифрового ассета. Блокаут является пространственным ориентиром; финальный low-poly, UV и bake выполняет моделлер.")
y=table([
    ["Экспорт","Состав / привязка","Слот"],
    ["SM_Jam_RMB93_Body","Коробка, рукоять, верхняя труба, сложенный приклад, кромка окна","0 Body"],
    ["SM_Jam_RMB93_Action","Нижняя труба + цевьё; отдельная визуальная подвижная группа","0 Body"],
    ["SM_Jam_RMB93_Counter","Утопленная лицевая поверхность; parent Body; UV0 0-1","0 Counter"],
    ["SM_Jam_RMB93_World","Собранная закрытая поза, те же UV и карты","0 Body\n1 Counter"],
],[260,400,110],M,140,12)
para("Бюджет",M+802,146,INNER-802,18,bold=True)
para("LOD0: 6-9 тыс. треугольников<br/>Предел: 12 тыс. суммарно<br/>LOD1: ≤5 тыс.<br/>LOD2: ≤2 тыс.<br/><br/>Два материала. Nanite Off.<br/>Крупные цилиндры: 10-12 граней.",M+802,184,INNER-802,14)
base=max(y+28,430)
para("Оси, масштаб и origin",M,base,cw,17,bold=True)
para("После импорта: +X к дулу, +Y вправо, +Z вверх; счётчик на -Y. "
     "Единицы: сантиметры. Общий origin на контакте пальца со спуском. "
     "Длина сложенного силуэта около 66 см, высота около 17 см; "
     "пропорция 4:1 по новому фото. Масштаб уточнить посадкой в руки.",M,base+35,cw,14)
para("Blender-блокаут: левая сторона +Y. Преобразование сторонности при FBX проверить "
     "на контрольной надписи LEFT и локаторе Muzzle.",M,base+151,cw,12,MUTED)
para("UV и поверхности",M+cw+gap,base,cw,17,bold=True)
para("UV0 0-1, без UDIM. Body/Action делят один набор. Коробку не зеркалить. "
     "Dilation 16 px и gap 32 px на 2048. Триангулировать до bake, согласовать "
     "hard edges и MikkTSpace. Counter имеет собственную локальную UV.",M+cw+gap,base+35,cw,14)
rule(690)
para("Интеграция",M,707,150,14,bold=True)
para("В проекте старая помпа: ActionTravelCm = -6, другие слоты и точки событий. "
     "Новые FBX требуют адаптации движения, рук и чтения Muzzle. "
     "Материалы и игровое поведение этим брифом не заменены.",M+170,704,INNER-170,13)

# 06: exact package table, data/color distinction and packed mask semantics.
page("Контракт PBR текстур",
     "Все карты одного набора используют окончательную UV и общий bake. PNG, 8 bit/channel, RGB, без alpha.")
rows=[["Суффикс T_Jam_RMB93_","Размер","RGB","sRGB","Compression / Sampler"]]
for t in CONTRACT["textures"]:
    suffix=t["name"].replace("T_Jam_RMB93_","")
    meanings={
      "BC":"Base Color без света / AO / свечения",
      "N":"Tangent normal DirectX Y-; Flip Green Off",
      "ORM":"R AO / G Roughness / B Metallic",
      "Masks":"R открытый металл / G грязь / B бордовый акцент",
      "Counter_BC":"Тёмная поверхность без активной цифры",
      "Counter_N":"Tangent normal DirectX Y-",
      "Counter_ORM":"R AO / G Roughness / B Metallic",
      "Counter_Masks":"R область свечения / G ghost wires / B грязь стекла",
      "DigitAtlas_M":"R сердцевина / G оранжевый разряд / B ореол"}
    rows.append([suffix,str(t["size"][0])+"²",meanings[suffix],
                 "On" if t["srgb"] else "Off",t["compression"]+" / "+t["sampler"]])
y=table(rows,[230,68,460,65,INNER-823],M,137,11.5)
para("Линейные данные",M,y+23,cw,16,bold=True)
para("AO: 1 открыто. Roughness: 1 шероховато. Metallic: 1 металл. "
     "Маски: белое = полное влияние. Корпусные маски обязательны в поставке; "
     "базовый шейдер Body читает BC/N/ORM.",M,y+54,cw,13)
para("Mips и импорт",M+cw+gap,y+23,cw,16,bold=True)
para("Mipmaps On, Clamp. Атлас: SimpleAverage, bilinear, LOD Bias 0, "
     "резидентен для first-person. Остальные карты могут стримиться. "
     "Проверить каналы и компрессию в cooked Windows build.",M+cw+gap,y+54,cw,13)
para("Шероховатость как старт подбора: сталь 0.45-0.65; полимер 0.55-0.75; "
     "резина 0.80-0.95; стекло 0.18-0.30. Не запекать янтарный свет в BC.",
     M,745,INNER,11.5,MUTED)

# 07: atlas mapping and material parameters.
page("Атлас цифр и параметры",
     "Газоразрядный проволочный рисунок, не семисегментный шрифт. Индексы и каналы одинаковы в PNG, JSON и материале.")
gx,gy,cell=M,145,96
for i,label in enumerate(CONTRACT["counter"]["atlas"]["cells"]):
    x=gx+(i%4)*cell
    top=gy+(i//4)*cell
    rect(x,top,cell-3,cell-3,colors.HexColor("#242a29"))
    text(str(i),x+9,top+8,10,"Arial",colors.HexColor("#999d97"))
    s=label if label!="RESERVED" else "RES"
    text(s,x+26 if len(s)<3 else x+17,top+36,26 if len(s)<3 else 16,
         "ArialB",colors.HexColor("#d2a465") if i<10 else colors.HexColor("#8a918a"))
para("1024²  /  4×4  /  ячейка 256²",gx,545,400,15,bold=True)
para("16 px чёрного бордюра. Контент 16...239 px. "
     "R = ядро 4-6 px, G = разряд 10-16 px, B = ореол 24-40 px. "
     "OFF и reserved полностью чёрные. Ghost-контуры хранятся отдельно.",gx,577,400,13)
rx=M+424
param_rows=[["Параметр","Старт / смысл"]]
for name,val in [
 ("SP_DigitIndex","4 preview; в игре из Ammo; 10 blank"),
 ("SP_CounterEnabled","1 On; 0 выключает Emissive"),
 ("VP_EmissiveColor","linear RGB (1.0, 0.32, 0.055)"),
 ("SP_EmissiveIntensity","5.0; подбирать при игровой экспозиции"),
 ("VP_DischargeColor","linear RGB (1.0, 0.10, 0.006)"),
 ("SP_DischargeStrength","0.55; оболочка разряда G"),
 ("VP_HaloColor","linear RGB (1.0, 0.045, 0.003)"),
 ("SP_HaloStrength","0.12; мягкий ореол B"),
 ("SP_GhostStrength","0.06; Base Color, не Emissive")]:
    param_rows.append([name,val])
table(param_rows,[242,INNER-424-242],rx,145,11)
para("Counter: Opaque / Default Lit / One Sided",rx,584,INNER-424,14,bold=True)
para("Каждому оружию свой Dynamic Material Instance. "
     "Цифра не зашита в Base Color. Прозрачный отдельный слой стекла и "
     "point light не требуются.",rx,616,INNER-424,12)
rect(M,674,INNER,103,colors.HexColor("#e5e5dd"))
text("i = clamp(floor(Index + 0.5), 0, 15);  cell = (i % 4, floor(i / 4))",M+16,690,12,"Mono")
text("uv = (cell * 256 + 16.5 + saturate(UV0) * 223.0) / 1024.0",M+16,716,12,"Mono")
text("Emissive = Aperture * Enabled * Intensity * (R*CoreColor + G*GasColor*GasGain + B*HaloColor*HaloGain)",M+16,742,10.4,"Mono")

# 08: data semantics, per-instance updates and visual acceptance.
page("Поведение и проверка счётчика",
     "Источник истины - FJamWeapon::Ammo. Текущая ёмкость проекта 6. Атлас 0-9 не меняет баланс.")
table([
 ["Событие","Что показывает окно"],
 ["Доставание / восстановление","Текущее Ammo записано до показа меша"],
 ["Успешный выстрел","Новое Ammo сразу после его уменьшения"],
 ["Пустой щелчок","0 остаётся 0"],
 ["Вставка патрона","Новое число в момент зачёта патрона, не в начале reload"],
 ["Прерывание reload / смена оружия","После возвращения - сохранённое Ammo"],
 ["Пусто","Index 0, Enabled 1"],
 ["OFF / данные недоступны / Ammo вне 0-9","Index 10, Enabled 0; ошибка данных диагностируется"],
],[340,INNER-340],M,140,13)
para("Читаемость",M,550,cw,17,bold=True)
para("Кадр 1920×1080, обычная поза: цифра ≥24 px высотой, активный штрих ≥2 px. "
     "Проверить 0-9 и OFF, mip0-3, 4 без зеркалирования, различимость 6/9. "
     "Это требования к будущей модели и материалу.",M,586,cw,14)
para("Игровая проверка",M+cw+gap,550,cw,17,bold=True)
para("Два оружия показывают разные значения без связи. Выстрел, reload по одному патрону, "
     "прерывание и переключение сохраняют правильное число. "
     "Проверить сохранённые assets после открытия и cooked Windows.",M+cw+gap,586,cw,14)
rect(M,712,INNER,65,PANEL)
para("Ноль светится. OFF не светится. Bloom выключен - цифра читается. "
     "Bloom включён - контур и оранжевая оболочка остаются различимы. "
     "Переход без случайных цифр; не добавлять +1 за патронник.",M+16,726,INNER-32,13,bold=True)

# 09: source inventory, honest production status and official references.
page("Сдача моделлеру и следующий этап",
     "В репозитории сохранены листы, полный контракт, JSON и исходник пространственного блокаута.")
para("В этом пакете",M,145,cw,18,bold=True)
y=para("• Принятый крупный план и два исходных фото.<br/>"
       "• Шесть ортографических видов и четыре 3/4 из одной сцены.<br/>"
       "• Контекст счётчика и состояния 0 / 4 / 6 / OFF.<br/>"
       "• .blend блокаута и воспроизводимый скрипт.<br/>"
       "• README.md, asset_contract.json, схема атласа.<br/>"
       "• История генерации и отчёт проверки файлов.",
       M,185,cw,14,leading=22)
para("Требуется от моделлера",M+cw+gap,145,cw,18,bold=True)
para("• Финальный DCC-исходник и четыре FBX с LOD.<br/>"
     "• Девять PNG по таблице и редактируемые исходники.<br/>"
     "• Manifest: bounds, pivots, sockets, slots, tris, карты.<br/>"
     "• Wireframe, checker, UV, крайние позы Action.<br/>"
     "• Скриншоты first-person / ADS / reload после интеграции.<br/>"
     "• Проверка всех цифр и двух независимых экземпляров.",
     M+cw+gap,185,cw,14,leading=22)
rule(382)
para("Назначения",M,405,INNER,17,bold=True)
text("ArtSource/Weapons/RMB93/v01/   ->   Meshes / Textures / Previews",M,441,13,"Mono")
text("/Game/JamArt/{Meshes,Textures,Materials}/Weapons/RMB93",M,469,13,"Mono")
para("Бинарные файлы - Git LFS. Старые игровые ассеты остаются до приёмки замены. "
     "В этом брифе нет производственных UV, готовых PBR-карт и проверенного импорта в UE.",
     M,506,INNER,13)
para("Документация Epic",M,562,INNER,17,bold=True)
links=[
 ("Texture masks","https://dev.epicgames.com/documentation/en-us/unreal-engine/using-texture-masks-in-unreal-engine"),
 ("Texture Asset Editor","https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-asset-editor-in-unreal-engine"),
 ("Emissive Material Input","https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-emissive-material-input-in-unreal-engine"),
 ("Instanced Materials","https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-materials-in-unreal-engine"),
 ("Coordinate System","https://dev.epicgames.com/documentation/en-us/unreal-engine/coordinate-system-and-spaces-in-unreal-engine")]
for i,(label,url) in enumerate(links):
    yy=602+i*28
    text(label,M,yy,13,"ArialB",AMBER)
    c.linkURL(url,(M,H-yy-17,M+310,H-yy+2),relative=0)
    text("dev.epicgames.com",M+320,yy,11,color=MUTED)
para("ORM, параметры, бюджеты и раскладка атласа определены этим контрактом. "
     "Исходное фото - пользовательский ориентир, не текстурный донор.",
     M,755,INNER,11,MUTED)
# 10: user profile is the silhouette authority for this revision.
page("Пропорции / новый боковой референс",
     "Главный ориентир длины и высоты. Основная модель остаётся со сложенным прикладом.")
image(BASE/"References/RMB93_Proportions_User_Reference.png",M,132,INNER,385)
card_image("02_right.png","ИСПРАВЛЕННЫЙ ПРОФИЛЬ  /  СЛОЖЕННЫЙ ПРИКЛАД",M,532,INNER,184)
para("Длина / высота ≈4:1. Уменьшены коробка, рукоять и толщина цевья; "
     "открытая передняя часть занимает около 38% длины. "
     "Сравнивать силуэт без растягивания картинки по одной оси.",M,733,INNER,13)
c.save()

# A vector layout diagram: intentionally contains labels, not importable masks.
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1120" height="1180" viewBox="0 0 1120 1180">',
     '<rect width="1120" height="1180" fill="#f3f2ee"/>',
     '<text x="48" y="42" font-family="Arial" font-size="22" fill="#202827">DigitAtlas layout - 1024 x 1024 / 4 x 4 / cells 256</text>']
for i,label in enumerate(CONTRACT["counter"]["atlas"]["cells"]):
    x=48+(i%4)*256
    y=70+(i//4)*256
    svg.append(f'<rect x="{x}" y="{y}" width="256" height="256" fill="#202827" stroke="#f3f2ee"/>')
    svg.append(f'<rect x="{x+16}" y="{y+16}" width="224" height="224" fill="none" stroke="#8a918a" stroke-dasharray="5 6"/>')
    svg.append(f'<text x="{x+26}" y="{y+42}" font-family="Arial" font-size="18" fill="#a7ada7">index {i}</text>')
    svg.append(f'<text x="{x+55}" y="{y+148}" font-family="Arial" font-size="{70 if i<10 else 25}" fill="#d2a465">{label}</text>')
svg += ['<text x="48" y="1132" font-family="Arial" font-size="18" fill="#202827">16 px black gutter. R core / G discharge / B halo. OFF + RESERVED = black.</text>',
        '<text x="48" y="1162" font-family="Arial" font-size="16" fill="#626967">LAYOUT DIAGRAM ONLY - do not import as a mask texture.</text>','</svg>']
(BASE/"DigitAtlas_Layout.svg").write_text("\n".join(svg),encoding="utf-8")
print(str(OUT))
print("Pages:",page_num)
