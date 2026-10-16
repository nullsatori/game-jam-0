# ZeroG (невесомость)

## Что получает игрок

Космос, мусор, корабль. Тело не тормозит: скорость сохраняется, пока её
что-то не изменит. Поворачивает только мышь: удары, захват и трос двигают
тело, но никогда не поворачивают ни его, ни взгляд. Обзор — FOV 105.
Двигаться можно так:

- **двигатели** (WASD, Space/Ctrl — относительно взгляда) — основное точное
  движение: разгоняют вдоль направления ввода до 5 м/с вдоль него и не
  трогают остальные составляющие скорости (толчок или отдачу они не гасят);
  кислород не тратят;
- **стабилизатор** (X) — плавно гасит скорость до нуля;

- **оттолкнуться** (ПКМ, сразу по нажатию) от того, что в 1.8 м перед глазами: от стены летишь
  сам, лёгкий хлам улетает от тебя, тяжёлый почти не двигается, а ты летишь;
- **схватить** (E) любое свободное тело: точка захвата тянется в 1.5 м перед
  камерой жёсткой задемпфированной пружиной между телами — тяжёлая деталь
  тянет тебя так же, как ты её (импульс сохраняется);
- **бросить** то, что держишь (ЛКМ): предмет получает импульс, ты — такой же
  обратный;
- **отдача выстрела** (ЛКМ с пустыми руками): от 15 см/с (пистолет) до 4 м/с
  (снапа) назад — см. [RecoilWeapons](RecoilWeapons.md); кислород не тратит;
- **прицел** (держать Left Shift): пунктиром — куда унесёт толчок (оранжевый,
  если есть опора в досягаемости) и отдача текущего ствола (жёлтый); с
  предметом в руках — куда полетит бросок (голубой).

Читаемость движения: вокруг камеры висит куб ~20 м мелкой пыли (300 точек),
она проплывает мимо, когда летишь; на HUD — ромб по направлению скорости
(проград) с м/с, или крест, когда летишь назад (ретроград).

Кислород — 180 с, тратит его только дыхание (1/с); двигатели, стабилизатор и
стволы — бесплатно (`ThrusterPerSecond`, `StabiliserPerSecond` = 0). Ноль — удушье 10 HP/с; смерть — уровень перезапускается через 3 с.
Баллон O2 (E) даёт +60 с.

## Управление

| Клавиша | Действие |
|---|---|
| мышь | обзор (тело не вращается) |
| ПКМ | оттолкнуться (по нажатию); с предметом — отпустить |
| ЛКМ | выстрел с отдачей (пистолет: держать — автоогонь); с предметом — бросок |
| Left Shift (держать) | показать путь толчка / отдачи / броска |
| T | трос на тело в 6 м / отцепить (см. [Tether-Prediction](Tether-Prediction.md)) |
| 1 / 2 / 3, колесо | пистолет / дробовик / снапа |
| R | перезарядка |
| E | схватить свободное тело / отпустить; баллон O2 и интерактив — их действие |
| F | отпустить |
| WASD, Space / Left Ctrl | двигатели (вперёд-вбок, вверх-вниз по взгляду) |
| X (держать) | стабилизатор: плавное торможение до нуля |
| Q (держать) | замедление времени |
| G | тег лома `MK.Crowbar` |

## Классы и файлы

| Файл | Что |
|---|---|
| `JamZeroGPawn.h/.cpp` | `AJamZeroGPawn : APawn, IMKImpactReceiver`; консольная команда `jam.ZeroG.Probe` |
| `JamOxygen.h/.cpp` | `UJamOxygenComponent` (кислород, удушье, смерть, рестарт), `AJamOxygenCanister` |
| `JamGameMode.h/.cpp` | `AJamZeroGGameMode` (пешка, HUD, дрейф хлама), `AJamZeroGHUD` |
| `Scripts/build_zerog.py` | сборка `/Game/Jam/Maps/Lvl_Jam_ZeroG` |

## Числа (все UPROPERTY, см/кг/с)

Импульсы — в кг·см/с: изменение скорости = импульс / масса.

| Свойство | Значение | Смысл |
|---|---|---|
| `CameraFOV` | 105 | горизонтальный угол обзора |
| `DustCount` / `DustCubeCm` / `DustSpeckCm` | 300 / 2000 / 2 | пыль вокруг камеры; материал `M_Jam_Dust` (unlit) |
| `BodyMassKg` | 100 | масса тела (override) |
| `BodyFriction` / `BodyRestitution` | 0.2 / 0.3 | физматериал тела, создаётся в BeginPlay |
| `MaxSpeedCm` | 1200 | потолок скорости (0 — без потолка) |
| `GrabReachCm` | 250 | дальность захвата (Visibility от камеры) |
| `MaxGrabMassKg` | 5000 | отказ только для абсурдной массы (в лог) |
| `GrabStiffness` / `GrabDamping` | 20000 / 3000 | пружина и демпфер сустава захвата (линейный drive Chaos в режиме силы; mass conditioning выключен — иначе импульс не сохраняется) |
| `HoldDistanceCm` / `bHoldInFront` | 150 / true | точка захвата держится на этом расстоянии перед камерой (цель drive обновляется каждый тик); выкл — где схватил |
| `ThrowImpulse` | 8000 | импульс броска: предмету +, себе − |
| `MaxThrowSpeedCm` | 1100 | лёгкий предмет не быстрее — импульс уменьшается |
| `PushReachCm` | 180 | дальность толчка (трасса Visibility от камеры) |
| `PushImpulse` | 30000 | от статики: 300 см/с; телу массы m даётся +импульс, себе −импульс |
| `PushCooldownSeconds` | 0.35 | |
| `Weapons` | 3 ствола | см. [RecoilWeapons](RecoilWeapons.md) |
| `ThrustAccelCm` | 600 | см/с² вдоль направления ввода |
| `ThrustMaxSpeedCm` | 500 | выше этой скорости вдоль направления ввода двигатель не добавляет (и не тормозит) |
| `StabiliserAccelCm` / `StabiliserRate` | 300 / 2 | торможение = min(300, скорость × 2) см/с² — мягкий конец у нуля |
| `bThrustersEnabled` | true | выключить двигатели совсем |
| `UJamOxygenComponent::CapacitySeconds` | 180 | |
| `BreathingPerSecond` / `ThrusterPerSecond` / `StabiliserPerSecond` | 1 / 0 / 0 | |
| `SuffocationDamagePerSecond` | 10 | платится раз в секунду через `UMKVitalsComponent` |
| `RestartDelaySeconds` | 3 | после смерти — `APlayerController::RestartLevel` |
| `AJamOxygenCanister::RefillSeconds` | 60 | |
| `AJamZeroGGameMode::InitialDriftMaxCm` / `InitialSpinMaxDeg` / `DriftSeed` | 20 / 30 / 1337 | стартовый дрейф хлама |

Тело — сфера радиусом 40 см, симулируется, гравитация тела выключена,
демпфирование 0, CCD включён, вращение заблокировано (`bLock*Rotation`;
смоук: `body rotation R(0)` после троса и удара). Тряска камеры при ударе —
только смещение объектива, без поворота.

## Лут и док

Детали корабля (тег `Ship.Part`) доставляются в `AJamDockZone` у ракеты —
см. [DockZone](DockZone.md). HUD показывает «Детали: n/N».

## Столкновения

Удар — это событие, а не только физика. Файлы `JamCollisionImpact.h/.cpp`:
`JamDeliverCollisionImpact` и `UJamCollisionImpactComponent` (режим
`AJamZeroGGameMode` вешает его на каждый `AMKPhysicsProp` на старте).

- Скорость удара = контактный импульс / своя масса (изменение скорости по
  нормали, отскок включён: лобовые 1000 см/с при restitution 0.3 читаются
  как ~1300).
- Выше `MinSpeedCm` (300) **и** `MinImpulse` (5000 кг·см/с) удар уходит в
  `MKResolveImpactFromHit` + `MKDeliverResolvedImpact` как Blunt: стекло
  бьётся (радиус `BluntBreakRadiusCm`), манекен и пешка получают урон
  `импульс × DamagePerImpulse` (0.0005). Физический импульс не добавляется —
  столкновение уже толкнуло оба тела. `CooldownSeconds` 0.15 — одно событие
  на тело.
- Пешка (`CrashImpact` — те же настройки для её ударов): от
  `FeedbackMinSpeedCm` (250) — звук `SC_MK_Hit_Armor` (громкость по
  скорости) и тряска камеры (`ShakeCmPer1000` 4 см, `ShakeSeconds` 0.25);
  от `CrashDamageMinSpeedCm` (600) — урон себе
  `(скорость − 600) × CrashDamagePerCmS` (0.05).

Смоук:

```
ZeroG probe [glass]: pane 30000 cm2; light JamOxygenCanister_0 4 kg at 200 cm/s
ZeroG probe [glass]: after the light one 30000 cm2; heavy MKPhysicsProp_39 126 kg at 800 cm/s
Collision impact: MKPhysicsProp_39 -> MKGlassPane_0 at 888 cm/s, impulse 111780 kg cm/s, damage 55.9, delivered
ZeroG probe [glass]: after the heavy one 19644 cm2
Collision impact: JamZeroGPawn_0 -> StaticMeshActor_6 at 1300 cm/s, impulse 130000 kg cm/s, damage 65.0, no receiver
ZeroG crash: StaticMeshActor_6 at 1300 cm/s, self damage 35.0
```

Если в пешку прилетает тяжёлый предмет, урон приходит дважды: от удара
предмета (его компонент) и от собственного «краша» пешки, если её скорость
изменилась больше чем на 600 см/с.

## Как сделать невесомый уровень

1. WorldSettings: `bGlobalGravitySet = true`, `GlobalGravityZ = 0`,
   GameMode Override = `AJamZeroGGameMode`.
2. Хлам — `AMKPhysicsProp` (меш с простой коллизией, `MassKg`). Режим сам
   обнуляет демпфирование и даёт дрейф; скрипт карты тоже ставит damping 0.
3. Опоры, от которых отталкиваться, — любая статика, блокирующая Visibility.
4. `PlayerStart` — где угодно; пешка не падает.

Карта `Lvl_Jam_ZeroG`: закрытый объём 80 × 80 × 50 м вокруг начала
координат. Свет игровой (правило тёмных тестовых карт к ней не относится):
солнце 3 lux (оболочка не отбрасывает тени), низкий SkyLight, 6 точечных
ламп, PostProcessVolume с ручной экспозицией (без физической камеры, bias 0).
Цвета по роли (`/Game/Jam/Materials`, экземпляры `BasicShapeMaterial`): опоры
серые, хлам серо-коричневый, детали корабля оранжевые, баллоны O2 синие,
ракета светлая, док обведён зелёными рёбрами; 40 предметов хлама 5–500 кг (кубы, цилиндры,
бочки), 5 опорных блоков 4 × 4 × 4 м (первый — в 1 м перед стартом),
«ракета» — цилиндр 3 × 15 м на +X с надписью ROCKET, 4 баллона O2 возле
старта, стекло 200 × 150 в рамке (+Y), манекен (−Y) и ещё три манекена,
висящих в объёме (мишени). Всё с фиксированным
сидом.

## Проверка

`jam.ZeroG.Probe` — толчок от того, что перед камерой, затем выстрелы пистолета и снапы,
скорость пешки в лог до и после. Смоук `-game -nullrhi` на старте карты:

```
ZeroG mode: world gravity Z 0.0, 44 props set drifting (up to 20 cm/s)
ZeroG probe [before]: ... pawn velocity (0, 0, 0) = 0 cm/s, O2 180.0 s
ZeroG push: off StaticMeshActor_6 (static, 0 kg), impulse 30000 kg cm/s
ZeroG probe [after push]: ... pawn velocity (-300, 0, 0) = 300 cm/s
ZeroG weapon: Пистолет fired, 1 pellet(s), recoil 1500 kg cm/s, 11/60 left
ZeroG probe [after pistol]: ... pawn velocity (-315, 0, 0) = 315 cm/s
ZeroG weapon: Снапа fired, 1 pellet(s), recoil 40000 kg cm/s, 3/16 left
ZeroG probe [after sniper]: ... pawn velocity (-715, 0, 0) = 715 cm/s
ZeroG grab: MKPhysicsProp_1, 800 kg
ZeroG probe [coupled push +0.5 s]: pawn V(X=-31.69) cm/s, held ... 800 kg V(X=-33.54) cm/s (expected both near 33), momentum 30000 kg cm/s, joint stretch -0 cm
ZeroG probe [coupled push +1.0 s]: pawn V(X=-33.32) cm/s, held ... 800 kg V(X=-33.34) cm/s (expected both near 33), momentum 30000 kg cm/s
ZeroG probe [thrusters +0.5 s, forward]: velocity (300, 300, 0) = 424 cm/s
ZeroG probe [thrusters +1.0 s, forward]: velocity (500, 300, 0) = 583 cm/s
ZeroG probe [thrusters +2.0 s, stabiliser]: velocity (371, 223, 0) = 433 cm/s
ZeroG probe [thrusters +3.0 s, stabiliser]: velocity (115, 69, 0) = 134 cm/s
ZeroG probe [thrusters +4.5 s, stabiliser]: velocity (6, 3, 0) = 7 cm/s
```

Двигатели: X растёт по 600 см/с² до 500 и останавливается, боковая Y = 300
не тронута; стабилизатор — 300 см/с², у нуля мягче.

После выстрела проба возвращает пешку в начало, подтаскивает самое тяжёлое
тело, хватает его и отталкивается от опоры: суммарный импульс равен
`PushImpulse` (сохраняется), скорости сходятся к PushImpulse / (100 + M).

## Известные ограничения

- Вращение сустава захвата свободное — деталь может крутиться вокруг точки
  захвата. Проба меряет захват с `bHoldInFront = false`; удержание перед
  камерой — та же цель drive, но руками не проверено.
- Пыль, маркер скорости, свет и цвета — вид не проверен (смоук с NullRHI).
- `UMKCarryComponent` (наземная переноска) в невесомой пешке не используется:
  его физ-ручка не даёт реакции пешке.
- Медленный дрейф может «уснуть» в Chaos (пороги сна физматериала); если
  хлам замирает — поднять дрейф или настроить sleep у тел.
- Ощущение (вес, скорость, читаемость) не проверялось: смоук шёл с NullRHI.
