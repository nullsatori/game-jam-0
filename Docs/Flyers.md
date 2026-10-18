# Flyers (летуны)

## Что получает игрок

Летающие хищники по мотивам скального наездника из Morrowind: птерозавр с
клювом и открытой пастью с мелкими зубами, загнутым назад гребнем, длинной
S-образной шеей, тощим телом и поджатыми лапами; широкие перепончатые крылья
на костяной «руке» — сверху тёмные, снизу красно-бурые с тёмными венами; очень
длинный хвост-хлыст с костяным лезвием на конце. Около 5 м от клюва до лезвия,
размах 3.5 м.

- **Стая живёт в своей зоне** (сфера или оболочка вокруг планетоида) и никогда
  не висит на месте: летит на крейсерской скорости к точкам впереди и вбок,
  поворачивает дугами (не больше 60° за раз — цель позади облетает, а не
  тормозит), покачивается вверх-вниз, треть отрезков — пикирование низко над
  поверхностью и быстрее. Обходит камни, обшивку и рельеф (сфера-сканирование
  вперёд по скорости), друг от друга держится на расстоянии.
- **Зрение:** до 45 м, конус ±70° от клюва, прямая линия до игрока (канал
  Visibility: планетоид, обшивка, мусор закрывают). Заметить может только
  игрока у своей зоны (радиус зоны + 5 м). Во время охоты конус не нужен —
  следит по линии видимости во все стороны.
- **Аггро:** визг, и летун кружит над игроком (9 м радиус, 5 м выше по
  «вверх» зоны: у планетоида — от центра), 1.5–3.5 с, потом пикирует туда, где
  игрок будет (упреждение до 0.8 с) на 18 м/с — пешка в невесомости летит
  максимум 12 м/с, маневровыми 5 м/с, уйти от пике нельзя, можно встретить
  выстрелом. Укус: 8 урона через систему попаданий MindKore и толчок
  45 000 кг·см/с (4.5 м/с на 100 кг) по направлению пике — в невесомости игрока
  отбрасывает, на планетоиде сбивает с шага. У игрока камеру встряхивает
  (тряска пешки от удара, 8 см) и на визор летит кровь — пятно MindKore
  (`M_MK_LensBlood`, как на линзе бодикама): ниже и сбоку от прицела, стекает
  и сходит за 3.5 с, два пятна сразу самое большее. Сам летун отскакивает и уходит
  вверх 1.2 с, потом снова кружит. Промах (подлетел ближе 6 м и проскочил мимо, или пике дольше 3 с) —
  тоже уход и новый заход.
- **Визг будит стаю:** блуждающие летуны в 25 м от того, кто начал охоту, тоже
  начинают охоту.
- **Деаггро:** игрок дальше радиуса зоны + 20 м от её центра, или игрока не
  видно 4 с (например, ушёл за планетоид или в пещеру). Летун возвращается к
  блужданию и 3 с не замечает игрока (выстрел будит сразу).
- **Стрелять можно:** попадают пули и дробь (сфера тела и сами меши: крылья,
  хвост, шея). 60 здоровья — 3 пули пистолета или почти любой выстрел дробовика
  в упор. Попадание толкает летуна (снапа отшвыривает), раненый, но спокойный —
  сразу в охоту, кружащий — сразу в пике. Звук и брызги попаданий — плоть
  (`PM_MK_Flesh` из материала).
- **Труп:** предсмертный крик, вращение размораживается, крылья и хвост
  обвисают; в невесомости тело кувыркается и дрейфует по инерции, а внутри
  гравитационной зоны (оболочка планетоида, 94.5 м) падает к центру, как пешка,
  и с вязким трением (линейное 0.4, вращение 6) докатывается и ложится на
  поверхность. Его можно толкать, хватать (E), бросать и стрелять по нему
  дальше. Живой летун проходит сквозь игрока (контакт — только укус), труп —
  твёрдый.

Звук (`Scripts/jam_flyer_sounds.py`, цепочка выстрелов `jam_space_shots.py`:
приглушено скафандром, тёмный трёхполосный хвост, низкий слой, громкость по
LUFS), всё 3D с затуханием:
- **визг** на аггро и на пике — три варианта `SW_Jam_Flyer_Screech_A/B/C`
  (−16 LUFS, высота ×0.92–1.08), слышно до 50 м: вдох из записи дыхания
  (Nox_Sound, CC0) выше и со спуском высоты («кри-и-и» вниз), подняты форманты
  1.2 и 2.6 кГц, перегруз до хрипа, дрожание ~32 Гц как у птицы; под ним то же
  дыхание на две октавы ниже (горло) и короткий саб;
- **предсмертный крик** `SW_Jam_Flyer_Death` — то же длиннее и с глубоким
  спуском, в конце мокрое бульканье (Gushy Impacts, TILE_SFX, CC0, октавой ниже);
- **взмах** — `SW_Jam_Flyer_Flap_A/B/C` (−24 LUFS) на каждом ударе крыла вниз
  (фаза взмаха π, в такт 2.2 Гц × темп состояния), громкость по размаху, в
  планировании (размах меньше трети) тишина; слышно до 25 м: выдох из той же
  записи октавой ниже (воздух) и мокрый шлепок двумя октавами ниже (кожа);
- **укус** — хруст `SC_MK_Hit_Limb` ×0.7–0.85 плюс звук попадания по
  поверхности игрока.

Это заглушки из уже лицензированных доноров проекта: настоящих птичьих криков
и взмахов крыльев (CC0 с Freesound) нет — скачивание файлов требует согласия
владельца в чате, оно не получено. Чтобы заменить: положить доноров, добавить
их в `jam_flyer_sounds.py` (`load` + `SOUNDS`) и `CREDITS.md`, перерендерить и
импортировать.

## Классы и файлы

| Файл | Что |
|---|---|
| `Source/MindKore/JamFlyer.h/.cpp` | `AJamFlyerHabitat` (зона, спавнит стаю на BeginPlay), `AJamFlyer` (`IMKImpactReceiver`); консольные `jam.Flyer.Probe`, `jam.Flyer.Shots`, `jam.Flyer.BiteShot`, `jam.Flyer.CorpseShot`; переменная `jam.Flyer.LogFlaps` |
| `Source/MindKore/JamVisorBlood.h/.cpp` | `UJamVisorBloodComponent`: кровь на визоре (`M_MK_LensBlood` первым в цепочке постобработки камеры, пока есть пятно); летун добавляет его пешке сам при первом укусе |
| `AJamZeroGPawn::KickCamera` | тряска камеры пешки (та же, что от удара) по запросу снаружи |
| `Scripts/jam_flyer_sounds.py` | звуки летуна; `Scripts/jam_import_audio.py` с `JAM_AUDIO_DEST=/Game/JamArt/Audio/Flyer` |
| `Scripts/blender/jam_flyer.py` | модель (Blender, headless): 4 части и атлас 256 px |
| `Scripts/jam_import_meshes.py` | импорт `SM_Jam_Flyer_*` в `/Game/JamArt/Meshes/Flyer` (общий импортёр) |
| `Scripts/jam_flyer_art.py` | текстура `T_Jam_Flyer_BC` (Nearest) и `MI_Jam_Flyer` (экземпляр `M_Jam_Gritty`) на всех частях |
| `Scripts/jam_flyers.py` | зоны в картах; его `place()` зовут `build_ship.py` и `build_zerog.py`, отдельно — переставляет зоны в обеих картах |

`AJamFlyer`: корень `Body` — симулируемая сфера 40 см, 20 кг, без гравитации и
затухания, вращение заблокировано, пока жив (тип PhysicsBody; игрок и
Visibility игнорируются). Полёт — ускорение к желаемой скорости (`AddForce`,
accel change), без навмеша. `Visual` поворачивается по скорости и
закладывается в вираж («подъёмная сила» = вверх зоны × 980 + боковое
ускорение). Части меша на суставах: `Torso` → `WingR`, `WingL` (то же крыло,
зеркало по Y), `Tail` → `TailTip`. Крылья машут (2.2 Гц, ±38°, в блуждании
чередуют планирование и взмахи, в уходе бьют чаще, в пике сложены назад на
45°), тело качается против взмаха, хвост и кончик качаются со сдвигом фазы и
заносятся в вираже. Части блокируют только канал Shot (сложная коллизия).

Состояния: `Wander` → (заметил / ранен / разбужен визгом) → `Circle` →
`Dive` → укус или промах → `Retreat` → `Circle` …; из охоты → `Wander` по
поводку или потере из виду; `Dead` — из любого.

## Числа

Все — свойства `AJamFlyer` (категории Flight, Hunt, Look) и `AJamFlyerHabitat`.

| Что | Значение |
|---|---|
| Крейсерская / кружение / пике | 7 / 10 / 18 м/с |
| Ускорение: блуждание / охота / пике | 9 / 28 / 36 м/с² |
| Смена цели блуждания | ближе 4 м или через 7 с; пикирование 30 % |
| Обход препятствий | сфера 60 см вперёд на 0.9 с полёта + 1.5 м |
| Зрение | 45 м, ±70°, потеря через 4 с |
| Кружение | радиус 9 м, 5 м над игроком, 1.5–3.5 с |
| Укус | ближе 1.9 м, 8 урона, 45 000 кг·см/с |
| Пике, уход | до 3 с, 1.2 с |
| Здоровье, масса | 60, 20 кг |
| Зона: поводок / заметить / визг будит | +20 м / +5 м / 25 м |
| Укус у игрока | тряска 8 см, кровь на визоре 0.9 размера (3.5 с) |
| Труп в гравитации | ускорение зоны (980 см/с² к центру), трение 0.4 / 6 |
| Звук: визг / взмах | 3 + 3 варианта, слышно до 50 / 25 м, −16 / −24 LUFS |

## Где они

| Карта | Зона | Центр, радиус | Стая |
|---|---|---|---|
| `Lvl_Jam_ZeroG` | `Flyers_Test` | (−1500, −2200, 0), 13 м — 26 м от старта | 3 |
| `Lvl_Jam_Ship` | `Flyers_Planetoid` | центр планетоида (0, 16000, −1000), оболочка 90–120 м (рельеф до 86.5 м) | 4 |
| `Lvl_Jam_Ship` | `Flyers_Belt` | (150, −6100, 100) над поясом обломков и топливом, 25 м | 3 |

Стартовая зона корабля безопасна: край зоны планетоида в 37 м от обшивки, пояса —
в 32 м; заметить могут только в 5 м от края зоны, а охота бросается в 20 м за
краем. На планетоиде летуны над поверхностью (зона от 90 м при рельефе до
86.5), но в охоте пикируют к игроку на земле.

Карты хранятся одним `.umap`: при конфликте на слиянии взять карту из основной
ветки и прогнать `Scripts/jam_flyers.py` (он только заменяет зоны
`AJamFlyerHabitat` и сохраняет обе карты).

## Как пересобрать модель

```
blender.exe -b --python Scripts/blender/jam_flyer.py -- <out_dir>
JAM_FBX_DIR=<out_dir> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_import_meshes.py" ...
JAM_FBX_DIR=<out_dir> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_flyer_art.py" ...
```

FBX, PNG и превью остаются в `<out_dir>`; в репозитории — только `.uasset`.
1652 + 1066 + 448 + 464 треугольников (тело, крыло, хвост, кончик).

## Проверка

`jam.Flyer.Probe [all] [Зона]` (логика, можно `-nullrhi`): берёт первого летуна
зоны (первой или по части имени/метки, например `Planetoid`), остальных
замораживает (кроме `all`; визг их всё равно будит — после пробы они охотятся),
ставит игрока за поводок, 4 с пишет блуждание, ставит игрока в 10 м перед
клювом, ждёт два укуса, прячет игрока за планетоид (если зона — оболочка),
снова показывается и уходит за поводок, стреляет из пистолета до смерти и
пишет труп: 4 с в невесомости; у планетоида игрок сначала стоит на земле под
летуном, его убивают в пике низко над поверхностью и 15 с смотрят, как труп
падает и ложится (строка `corpse landed` пишет сам летун). Пока проба идёт,
включён `jam.Flyer.LogFlaps`: в лог идёт каждый слышимый взмах (ближе 25 м).

`jam.Flyer.BiteShot` (окно): летун кусает игрока, кадры `Shot` через 0.2, 1.2 и
2.5 с — кровь на визоре. `jam.Flyer.CorpseShot` (окно, `Lvl_Jam_Ship`): летун
убит в 6 м над планетоидом, через 7 с игрок в 4 м от трупа, кадр.

Раунд 2, 19.10 (звук, отклик укуса, падение трупа), `Lvl_Jam_Ship`, `jam.Flyer.Probe Planetoid`:

```
Flyer JamFlyer_0: screech (aggro) - SW_Jam_Flyer_Screech_B at pitch 1.05, 9.4 m from the player
Flyer JamFlyer_0: screech (dive) - SW_Jam_Flyer_Screech_B at pitch 1.05, 11.2 m from the player
Flyer JamFlyer_0: bite feedback - camera shake 8 cm, visor blood 0.90 at (0.20, 0.62)
Flyer JamFlyer_0: flap - SW_Jam_Flyer_Flap_A, volume 0.80, pitch 0.97, circle, 8.0 m from the player
Flyer probe [shoot]: the player dropped on the planetoid under JamFlyer_0, 88.0 m from the centre, [1] Пистолет 12/60
Flyer JamFlyer_0: retreat -> dead (killed, drifting at 719 cm/s)
Flyer JamFlyer_0: death cry - SW_Jam_Flyer_Death
Flyer probe [corpse +1 s]: JamFlyer_0 dead, 671 cm/s, ... 83.6 m from home
Flyer probe [corpse +3 s]: JamFlyer_0 dead, 54 cm/s, spin 78 deg/s, 82.6 m from home
Flyer JamFlyer_0: corpse landed on StaticMeshActor_470 in JamGravityZone_2, 8.1 s after death, 40 cm above the ground
```

`Lvl_Jam_ZeroG`, тот же билд: весь цикл, визг на каждом аггро и пике, отклик
на каждом укусе, 21 слышимый взмах; труп дрейфует без гравитации (400 см/с
постоянно с +2 до +4 с).

`Lvl_Jam_Ship` (18.10, `jam.Flyer.Probe Planetoid`), весь цикл:

```
Flyer probe [wander 0]: JamFlyer_0 wander, 616 cm/s, ... 99.7 m from home, 213.7 m from the player
Flyer probe [sight]: the player 10 m ahead of JamFlyer_0
Flyer JamFlyer_0: wander -> circle (aggro: saw the player 8.9 m away, 1 deg off the nose)
Flyer JamFlyer_0: circle -> dive (dive from 12.0 m)
Flyer JamFlyer_0: bite 1 on JamZeroGPawn_0 at 1631 cm/s - damage 8, shove 45000 kg cm/s, delivered; the player moved 0 cm/s before
Flyer probe [hunt 4.0 s]: JamFlyer_0 retreat, 751 cm/s, ... player 450 cm/s, HP 92
Flyer JamFlyer_0: bite 2 on JamZeroGPawn_0 at 1763 cm/s - damage 8, ...; the player moved 786 cm/s before
Flyer probe [hide]: the player on the far side of the planetoid, 88.0 m from the centre (inside the leash)
Flyer JamFlyer_0: circle -> wander (de-aggro: lost sight of the player for 4.0 s)
Flyer JamFlyer_0: wander -> circle (aggro: saw the player 8.7 m away, 1 deg off the nose)
Flyer probe [leave]: the player moved 148.0 m from the habitat's centre (radius 120 m + leash 20 m)
Flyer JamFlyer_0: circle -> wander (de-aggro: the player left the habitat, 148 m from its centre, leash at 140 m)
Flyer JamFlyer_0: hit in Body (EMKSurfaceKind::Flesh) by JamZeroGPawn_0 - damage 25.0, health 35/60
Flyer JamFlyer_0: wander -> circle (aggro: shot)
Flyer JamFlyer_0: hit in Body (EMKSurfaceKind::Flesh) by JamZeroGPawn_0 - damage 25.0, health 10/60
Flyer JamFlyer_0: circle -> dive (dive from 8.7 m)
Flyer JamFlyer_0: hit in Body (EMKSurfaceKind::Flesh) by JamZeroGPawn_0 - damage 25.0, health 0/60
Flyer JamFlyer_0: dive -> dead (killed, drifting at 1048 cm/s)
Flyer probe [corpse +4 s]: JamFlyer_0 dead, 909 cm/s, spin 202 deg/s, 131.2 m from home
```

`Lvl_Jam_ZeroG` (18.10, `jam.Flyer.Probe`): визг будит стаю; первый укус
отбросил игрока на 4.5 м/с за поводок (13 + 20 м от центра), и охота кончилась
сама — второго укуса проба не дождалась («fewer than two bites in 15 s»), это
поведение, а не сбой:

```
Flyer JamFlyer_0: wander -> circle (aggro: saw the player 8.9 m away, 2 deg off the nose)
Flyer JamFlyer_1: wander -> circle (aggro: woken by JamFlyer_0's screech)
Flyer JamFlyer_0: bite 1 on JamZeroGPawn_0 at 1629 cm/s - damage 8, shove 45000 kg cm/s, delivered; the player moved 0 cm/s before
Flyer probe [hunt 3.0 s]: JamFlyer_0 retreat, 844 cm/s, ... player 450 cm/s, HP 92
Flyer JamFlyer_0: dive -> wander (de-aggro: the player left the habitat, 33 m from its centre, leash at 33 m)
Flyer JamFlyer_0: dive -> dead (killed, drifting at 1432 cm/s)
Flyer probe [corpse +2 s]: JamFlyer_0 dead, 584 cm/s, spin 705 deg/s, 31.3 m from home
```

Ложный промах из предыдущего прогона (`dive -> retreat (missed: overshot by 16.6 m)`
сразу после начала пике, когда летун ещё летел прочь) исправлен: «проскочил»
засчитывается, только если подлетел ближе 6 м; иначе ждёт 3 с пике. Настоящий
промах после правки пробой не пойман.

`jam.Flyer.Shots` (нужен настоящий RHI, окно): летуны перестают замечать
игрока, камера едет за первым летуном первой зоны — спереди сверху, снизу, сзади
сверху, издали — и смотрит на стаю; команда `Shot` каждые 3 с, файлы в
`Saved/Screenshots`.

Не проверено (раунд 2): звук никто не слушал — прогоны с `-nosound`, оценка
только по таблице `INDEX_flyer.txt` (полосы, LUFS). Пятно на визоре через 0.2 с
после укуса на кадре ещё не видно (видно на 1.2 и 2.5 с) — похоже, так
материал MindKore выращивает пятно; не разбирался.

Не проверено: потеря из виду в `Lvl_Jam_ZeroG` (там негде спрятаться — проба это
пишет), стая `Flyers_Belt` пробой не гонялась (тот же код, другая зона), звук
не слушали (прогоны с `-nosound`).
