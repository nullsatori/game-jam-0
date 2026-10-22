# Дым Рыси: живой воксельный газ (дизайн, 22.10)

## Коротко для владельца

Сейчас облако дробовика — это 8 неподвижных гауссовых шаров, слитых в один
полупрозрачный ком (экстинкция 0.018/см: насквозь видно всё, теней внутри
нет), с ровным «своим светом» 0.12, который и даёт молочную вуаль; форма
замирает через 0.15 с и дальше только равномерно гаснет; облако не знает о
полу — выстрел в пол утаскивает его центр под плиты, и в кадре остаётся
плоский срез. «Лоурез» в нём тоже фиктивный: дизеринг считается по сетке,
повёрнутой относительно вокселей самого рендера, а рендер идёт в полразрешения.

Предлагаю не «больше частиц», а честный газ: маленькая CPU-симуляция
несжимаемого дыма (32³ ячеек по 4 см на облако: давление, завихрённость,
всплытие в воздухе, баллистический разлёт в вакууме, пол и стены как
препятствия), которая каждый кадр пишет плотность и жар в крошечную текстуру;
объёмный рендер (те же Heterogeneous Volumes, свет и самозатенение движка)
только квантует её в 3 полосы с 3D-дизерингом по вокселям и выжигает
воксели-угольки. Дым рождается плотной горячей пробкой, разворачивается в
гриб и кольцо, стелется по полу, рвётся на клочья с тонких мест; пули, игрок,
летуны и горячие гильзы его режут и стирают; гильзы на полу дымят. Дым кожуха
— те же цвета и полосы, извилистые подсвеченные струйки. Бюджет GPU ≤ 0.8 мс
(дешевле нынешнего), CPU ≤ 0.5 мс на живое облако (не больше двух живых).
Чёткость: плотность кусочно-постоянна по вокселям (4 см = 7–10 px на 2 м),
кэш освещения и тени на том же вокселе, джиттер марша выключен; полное
разрешение HV — только как измеренная опция.

---

## Что вышло (22.10)

Реализовано (ветка `claude/project-thread-d9qnn2`), описание и числа — в
[RecoilWeapons-Fx](RecoilWeapons-Fx.md#живой-дым-2210). Отличия от дизайна ниже:

- **Главная причина «молока» была другой.** Материал облака был Unlit, а HV в 5.8
  читают экстинкцию только у не-Unlit материала, и пин Subsurface Color
  компилируется только при shading model Subsurface. Значит, экстинкции у старого
  облака не было вовсе (пункты 2–3 диагноза ниже — следствие). Плюс единицы:
  объём интегрирует по вокселю, а не по сантиметру.
- **Препрошейдинга нет** (`r.HeterogeneousVolumes.Preshading 0` в 5.8): материал
  считается на каждом шаге луча, кубы задаёт сам материал по индексу вокселя.
  Имена переменных проверены: `Jitter`, `DownsampleFactor`, `LightingCache`,
  `CLOD`; у компонента `LightingDownsampleFactor`, `StepFactor`, `ShadowStepFactor`.
- **Свет.** Лампы тестовой комнаты почти не освещают объём (её яркость —
  отражённый свет Lumen, а HV получают только прямой). Добавлен окружающий свет
  как рассеяние с тенью от газа сверху и вокруг. Тень считается на CPU в `Pack`
  (атлас: B — тень, A — завихрённость, флага твёрдого нет).
- **Выброс.** Слаг 12 × 5 см на сетке 4 см терял импульс за 0.2 с и катился
  шаром. Сделаны шар после взрыва (22 × 16 см), струя `EmitSeconds`, раздувание
  в решении давления (`SwellPerSecond` / `SwellSeconds`), турбулентность от жара,
  MacCormack для плотности.
- **Вакуум.** Собственная радиальная скорость сетки размазывается самоадвекцией.
  Сетка формирует слаг 0.35 с (не 1.2), дальше бокс растёт со скоростью разлёта
  слага (до 1/с), вперёд — 0.3 скорости, струи и окружающего света нет.
- **Цена.** 32³ стоили 4.9 мс на шаг. Сделано: сетка дробовика 24 × 5.33 см,
  СВ-98 22 × 4.45 см, Gauss-Seidel 4, шаг только в коробке вокруг газа,
  60 Гц первые 0.5 с, потом 30 Гц, шаги в фоновой задаче. GPU на полном разрешении —
  0.96 мс на три облака: `DownsampleFactor 1` оставлен.
- Не сделано: сдвиг сетки «пол в 2 ячейках от низа» (бокс повёрнут); отдельный
  `os.Fx.SmokeFullRes` — вместо него обычная `r.HeterogeneousVolumes.DownsampleFactor`;
  кадры летунов в облаке.

## Правка v3 (22.10, подача)

После v2 дизайн: физика и масштаб пикселя верны, но от первого лица облако
слабое и маленькое. Только подача, без новых систем:

- **Облако возвращается к стрелку.** На полу, кроме диска, — второй горячий клуб
  на ближней к стрелку стороне всплеска (`BackRollCm` 420 см/с назад и вверх), а
  всё облако получает дрейф назад 90 см/с (воздух его гасит, `AirDragSeconds`):
  внутри сетки откат съедали давление и сопротивление. Газ через 0.5 / 1 / 2 / 4 с
  в 260 / 219 / 162 / 131 см от глаза (v2: 279 / 284 / 293 / 300) — к 1–2 с вуаль
  в середине кадра, в неё можно войти. Всплытие 500 → 160, жар 0.8 → 0.5 с: гриб
  не уходит под потолок (центр газа 61 → 117 см над полом за 0.5–2 с).
- **Контраст.** Тень ключевого света ×2.4 (`KeyShadow` 1.2), его яркость ×1.75
  (`KeyStrength` 0.014): светлая кромка, тёмное брюхо. Тень облака на полу
  (`r.HeterogeneousVolumes.Shadows 1`) — +0.71 мс (итого 1.22 мс на три облака,
  в бюджете), но на кадрах комнаты тени не видно: тонкое облако и тусклые
  стационарные лампы. Не включено.
- **Клочья.** `Sharpen` 0.6 → 0.75. Угольки — только самые горячие и редкие
  (шар из угольков на 0.2 с читался сеткой).
- **Кожух.** Вращение 20 см/с, клочков вдвое больше (32 на выстрел, 24/с тления)
  при вдвое меньшей непрозрачности (0.18), подъём начинается только когда ствол
  отпускает клочок. `MaxPuffs` 320. `os.Rmb93.Cost` — 5.0 мкс в среднем.
- **Вакуум.** Рост бокса не больше 0.35/с, эрозия в вакууме ×0.35: на 2 с — тонкая
  вуаль на чёрном (пропускание 0.95), к 4 с нет.

Пропускание через центр (комната): 0.36 / 0.55 / 0.57 / 0.68 при 0.2 / 1 / 2 / 4 с
(цель 0.5 / 0.65 / 0.9 — к 4 с плотнее цели, потому что облако теперь ближе к
глазу). 30 / 60 / 144 FPS: газ через 2 с на 117.2 / 116.4 / 117.4 см над полом,
масса 6050 / 5881 / 6411. GPU 0.49 + 0.02 мс, игровой поток 0.1 мс в среднем.
Листы — три ряда «до / v2 / v3».

## Правка v2 (22.10)

Владелец после первой версии: «дым слишком плотный, слишком лоу резовый», затем
«дым должен дорого смотреться, уровень фичи» и «дым из отверстий должен плавно
обволакивать оружие». План со стороны дизайна (пункты A–E) и что из него вышло:

- **A. Пиксель экрана, а не воксель сетки.** Сетка читается трилинейно (фильтр
  текстуры в срезе + lerp между срезами: 8 выборок `Load` стоили 3.8 мс), а
  квантуется материалом на свой куб: `exp2(floor(log2(max(1 см, глаз × K))))`,
  K — ~5 px при 1080p и FOV 134 (`PixelPerCm` = 5 / 1920 × 4.71). Куб — 1 см до
  ~80 см от глаза, 2 см до 1.6 м, 4 см до 3.3 м (5–10 px). Дизеринг Байера — по
  `floor(p / cell)` в пространстве облака. 4 полосы с очень тонкой нижней (0.1,
  0.35, 0.65, 1): край — вуаль.
- **B. Масса и клочья.** Экстинкция дробовика 0.14 → 0.007 1/см (пистолет 0.009,
  СВ-98 0.007), шар взрыва 28 × 22 см. Рисуемая плотность — газ минус 0.6 его
  размытия (5 ячеек, `Sharpen`): подушка уходит, клочья остаются. Эрозия с
  0.5 с. Пропускание по лучу глаза через центр газа (новое `T` в строках
  стенда): 0.50 / 0.62 / 0.71 при 0.2 / 1 / 4 с (цель 0.5 / 0.75 / 0.95 —
  к 4 с облако плотнее цели).
- **C. Струя, потом сетка.** Канал трассируется на 3 м. Первые ~0.15 с газ —
  растянутые карточки от дула до попадания; сетка рождается в точке попадания
  (на полу — плоский диск, который давление раскатывает по полу) или в 60 см
  впереди. Бокс дробовика 32 × 5 см = 1.6 м. Всплытие 500 см/с²: гриб
  поднимается на ~50 см за 2 с (от 43 до 98 см над полом).
- **D. Ключевой свет.** Каждый кадр — самый яркий свет у облака (лампы, солнце,
  вспышка) по освещённости, сглажено τ 0.25 с, не больше `KeyMax` 3: его
  направление уходит в атлас (тень по 6 шагам до 10 ячеек, B), цвет — в
  материал. Окружающий свет 0.02 (A — его тень). Светлая кромка и тёмное брюхо
  теперь от лампы, а не от отражённого Lumen.
- **E. Дым кожуха обволакивает ствол.** Клочки живут в пространстве ствола
  ~1.5 с (`ClingSeconds`): ползут назад вдоль кожуха 3 см/с, вращаются вокруг
  канала 12 см/с, почти без броска (0.3 см), проявляются за 0.15 с, вытянуты
  ×2–3 вдоль ствола; затем их берёт мир (воздух или вакуум). Дым дула на стенде
  кожуха убирается (иначе закрывает кадр).

Не сделано из пожеланий: тень облака на пол (HV в 5.8 отбрасывают тень только
с `r.HeterogeneousVolumes.Shadows 1` — не мерил), преломление за струёй, сама
сетка облака на 32³ по-прежнему 5 см (мельче — только пиксель материала).

Цена: GPU — при полном разрешении 0.53 + 0.02 мс на три облака дробовика (после
`r.HeterogeneousVolumes.StepSize=4` в `DefaultEngine.ini`: шаг по умолчанию —
вокселем бокса — стоил 1.6–1.9 мс); при `DownsampleFactor 2` 0.28 + 0.09. CPU:
шаг 32³ в фоне 1.4–3.5 мс, игровой поток 0.14–0.30 мс в среднем, пики до 25 мс
(ожидание задачи под офскрин-рендером). Частоты 30 / 60 / 144: газ через 2 с на
98 / 90 / 90 см над полом, масса 5316 / 5276 / 5128 (30 FPS выше на 8 %).

Кадры: `Docs/Img/Smoke/sheet_room_firstperson.png`, `sheet_room_witness.png`,
`sheet_roomspace_firstperson.png`, `sheet_room_second_shot.png` — три ряда
(main / первая версия / v2); `sheet_rmb93_ports.png` — кожух до и после.

## 1. Diagnosis: why the cloud reads flat, milky, cheap

All references are to the current `jam-smoke-world` state.

1. **No internal structure to light.** `VOLUME_HLSL` sums up to 8 gaussians
   (`d += weight * exp(-dot(q,q)/(P[i].w²))`, then `saturate(d)`,
   `Scripts/os_muzzle_fx.py:245-254`). For the shotgun, 8 clumps of radius
   ≤ 26 cm (`SmokeEndCm 34 × 0.75`, capped `0.2 × CloudCm`) thrown ≤ 26 cm
   apart overlap almost entirely, so `saturate(d)` is 1 over most of the box:
   one soft ball. Noise (`n - 0.22 - 0.4·Tear`, `×3`) only nibbles its edge.
2. **Extinction too low to self-shadow.** `Shotgun.CloudExtinction = 0.018`
   (`OSMuzzleFx.cpp:139`) on the densest band: τ through 60 cm of full band
   ≈ 1.1, through a typical 30 cm ≈ 0.5. Nothing inside is dark; lit from a
   lamp the whole volume in-scatters about equally → a uniformly translucent
   veil. The earlier "×1/3" cut (RecoilWeapons-Fx.md, «Правки после ревью»)
   fixed the white blow-out by removing the only thing that gives a volume
   form: optical thickness.
3. **Flat ambient glow = milk.** Emissive `albedo × band × Ambient 0.12`
   (`os_muzzle_fx.py:368`) is added to every lit voxel regardless of shadow:
   a constant offset that flattens exactly the contrast the renderer makes.
   Against the beige floor the darkish albedo (0.16, 0.18, 0.22) plus flat glow
   is a grey wash.
4. **Frozen shape, uniform fade.** `Throw = 1 − e^(−Age/0.12)`,
   `Grow = 1 − e^(−Age/0.15)` (`OSMuzzleFx.cpp:539-540`): after ~0.4 s nothing
   inside the cloud moves; the 3D noise has no time term (`q3 = p/7 + S*17`,
   `.py:257`); the whole volume is multiplied by one `Fade` (`.cpp:544`, applied
   as `F` in `.py:292`) → the cloud dims evenly instead of thinning from its
   wisps. The only motion is a 3 cm/s drift of the centre (`SmokeDriftCm`).
5. **No floor, no buoyancy for clouds.** `TickClouds` moves the centre by
   `Carried + Velocity` with no trace (`.cpp:524`); the throw of 280 cm/s along
   the bore (`.cpp:450`, τ 0.4 s → ~1.1 m) drives the centre *into the floor*
   when firing at it. The lower half of the 130 cm box is inside the slab; the
   visible part is a plane-cut disc "spread over the floor". Lift exists only
   for port-smoke cards (`PortPuffs`, `.cpp:331`), never for clouds.
6. **The low-res identity is not actually on the render's voxels.** HV
   *preshades* the Volume material into the component's `VolumeResolution`
   grid (26³, box-aligned) and marches that grid. The material dithers on its
   own rotated grid `G` (`.py:230-235`); its "voxel centre" samples land at
   arbitrary phases of the real voxels, so the Bayer steps become per-voxel
   noise, then the half-res march (`r.HeterogeneousVolumes.DownsampleFactor 2`),
   bilinear upsample and DLAA average it → soft. The lighting cache is also
   downsampled (component `LightingDownsampleFactor`, default 2), so even the
   little shadow there is spans 2 voxels.
7. **Nothing interacts.** Rounds and the pawn only *subtract* density
   (tunnel masks, `.py:279-289`); spent cases (`UpdateCycleEvents`,
   `OSWeaponParts.cpp:1040-1052`) are inert; the flash light is the only
   "event" the cloud shows.

## 2. Look target and the feature set

**Target:** a dense, grey-warm slug of powder gas leaves the muzzle, hits the
floor, splashes into a flat rolling disc, curls up at its rim into a ragged
ring that mushrooms 40–60 cm, then hangs, slowly churning, and dies by
crumbling: holes open in the thin parts, lumps stay longest. Lit by the room
and the flash with real dark undersides. Every surface is a stack of 4 cm
cubes in 3 density steps with a 3D Bayer checker between them; the first
half-second the core holds flickering orange ember-voxels. Spent shells drop
smoking. In vacuum the same slug just flies apart ballistically, thinning,
with no curl or rise, and freezes.

Ranked by impact / cost:

| # | Feature | Impact | Cost |
|---|---|---|---|
| A | **CPU grid sim** (incompressible in air, ballistic in vacuum; buoyancy, vorticity confinement, floor/wall solids) feeding HV from a texture — gives living motion, mushrooming, floor pooling, honest vacuum, all interactions with one mechanism | very high | CPU ≤ 0.5 ms wall per active cloud (ParallelFor), GPU cheaper than now |
| B | **Lighting & contrast**: dense bands with extinction up to 0.14/cm, albedo up, ambient ≈ 0, warm core tint, ember voxels, shadow cache at voxel res | high | ~0, march ends earlier (denser) |
| C | **Dissipation by erosion** in the material: per-voxel thresholds, thin voxels die first; churn fringe on moving edges | high | ~0 |
| D | **Interactions as impulses**: rounds, shotgun shove, pawn pass, flyers, **hot shells** (trail + cut inside clouds, smoking on the floor as cards) | medium-high | small CPU |
| E | **Port smoke** matched to the cloud: same palette/bands, sinuous rising wisps with domain-warped rags | medium | ~0 |
| F | **Crispness**: box-aligned voxel quantization, nearest texel loads, jitter off, lighting cache 1:1; full-res HV only as a measured switch | medium | 0 / +0.9 ms if full-res |

Everything stays maths and C++: no textures from outside (the grid texture is
written at runtime), no Niagara.

## 3. Implementation

### 3.1 The grid sim (`Source/MindKore/OSSmokeSim.h/.cpp`, new)

Cloud space (the HV box frame), N³ cells of `CellCm`, origin at the box's
min corner. Fields: `Rho`, `Heat`, `Ux, Uy, Uz` (cm/s), scratch `P, Div, Tmp`,
`Solid` (uint8). N = 32 for the shotgun: 32 768 cells, 7 floats + 1 byte
≈ 950 KB per cloud.

```cpp
/** A small smoke grid in a cloud's space: incompressible in air, free-flying in a vacuum. Cells of CellCm, N a side. */
struct FOSSmokeSim
{
	int32 N = 32;
	float CellCm = 4.0f;
	bool bAir = true;
	/** Unit direction of the local gravity, cloud space (zero: none). */
	FVector3f Down = FVector3f::ZeroVector;
	/** Solids, cloud space (cm): a cell whose centre is behind a plane is solid. At most 2 (the floor, a wall). */
	TArray<FPlane4f> Planes;
	TArray<float> Rho, Heat, Ux, Uy, Uz, P, Div, Tmp;
	TArray<uint8> Solid;

	struct FParams
	{
		float Buoyancy = 260.0f;    // cm/s^2 per unit heat, against Down (air)
		float DragSeconds = 0.35f;  // the air's hold on the gas's own motion
		float Vorticity = 0.35f;    // confinement epsilon (dimensionless, x CellCm)
		float HeatSeconds = 0.5f;   // heat dies (embers out, buoyancy stops)
		float DecayPerSecond = 0.06f; // density loss (the rest is erosion in the material)
		int32 Jacobi = 12;
	};

	int32 Index(int32 X, int32 Y, int32 Z) const { return (Z * N + Y) * N + X; }
	FVector3f CellCentre(int32 X, int32 Y, int32 Z) const { return (FVector3f(X, Y, Z) + 0.5f) * CellCm - 0.5f * N * CellCm; }
	float Sample(const TArray<float>& F, const FVector3f& Cells) const; // trilinear, clamped to [0.5, N-1.5]

	void Reset(int32 InN, float InCellCm, bool bInAir, const FVector3f& InDown);
	void RebuildSolids();
	/** Gas at Centre (cm): an ellipsoid Radii (cm) of density Rho, heat Heat, moving at Velocity (cm/s) plus Radial outward. */
	void Inject(const FVector3f& Centre, const FVector3f& Radii, float InRho, float InHeat, const FVector3f& Velocity, float Radial);
	/** A push along a line (Origin + t Dir): Along cm/s down the line, Push cm/s out of it, within RadiusCm (gaussian). */
	void Impulse(const FVector3f& Origin, const FVector3f& Dir, float RadiusCm, float Along, float Push);
	void Step(float Dt, const FParams& K);
	/** Move the grid by whole cells (the box follows the gas): fields shift, what leaves is lost. */
	void Shift(const FIntVector& Cells);
	/** Density centroid and mean radial speed (cells / cm/s) for the box follow and the vacuum expansion. */
	FVector3f Centroid(float& OutMass) const;
	float MeanRadialSpeed() const;
	/** The atlas: 8 x (N/8) tiles of N x N, RGBA8: R density, G heat, B churn (|curl| / 40 /s), A solid. */
	void Pack(TArray<FColor>& Out) const;
};
```

`Step` (all loops `ParallelFor(N, [&](int32 Z) {...})` over z-slices; the
Jacobi solve red-black so slices are independent):

```cpp
void FOSSmokeSim::Step(float Dt, const FParams& K)
{
	const float Inv = 1.0f / CellCm;
	// 1. Forces. Air: hot gas rises against Down, the air damps the gas's own motion. Vacuum: nothing acts.
	if (bAir)
	{
		const float Drag = FMath::Exp(-Dt / K.DragSeconds);
		ParallelFor(N, [&](int32 Z) { for Y, X: i = Index(X,Y,Z);
			Ux[i] = (Ux[i] - Down.X * K.Buoyancy * Heat[i] * Dt) * Drag; /* same Y, Z */ });
	}
	// 2. Vorticity confinement (Fedkiw): omega = curl U (central differences, /2CellCm), eta = grad|omega|,
	//    U += eps * CellCm * normalize(eta) x omega * Dt. Keeps the small curls the coarse grid would smear.
	//    In vacuum eps is halved (no pressure to feed them).
	// 3. Advect (semi-Lagrangian, one backward Euler step, all fields, into Tmp then swap):
	//    p = cell - U(cell) * Dt / CellCm; F'(cell) = Sample(F, p). Solid cells keep Rho = Heat = U = 0.
	// 4. Project (air only): Div = 0.5 * Inv * (Ux[x+1]-Ux[x-1] + Uy[y+1]-Uy[y-1] + Uz[z+1]-Uz[z-1]);
	//    P = 0; K.Jacobi times: P[i] = (sum of P over the 6 neighbours, a solid or outside neighbour contributing P[i], - Div[i] * CellCm^2) / 6;
	//    U -= 0.5 * Inv * grad P; the normal component is zeroed on faces against a solid cell.
	//    Open box faces: P = 0 outside (gas may leave).
	// 5. Dissipate: Rho *= exp(-Dt * K.DecayPerSecond); Heat *= exp(-Dt / K.HeatSeconds);
	//    the outer 2 cells: Rho *= 0.5 (a soft open boundary, nothing drawn at the box faces).
}
```

Written out in full in the implementation; the formulas above are the whole
method (Stam's stable fluids with vorticity confinement, no diffusion term).
Trilinear `Sample` clamps to the grid. Semi-Lagrangian advection is stable at
any `Dt`; `Dt` is the frame's, clamped to ≤ 1/30 s.

**Vacuum branch** (`bAir == false`): steps 2 (ε/2), 3, 5 only. Velocity is
advected by itself (free-streaming gas): the slug flies apart and thins. After
`ActiveVacuumSeconds` (1.2 s) the sim freezes and the *box scales*: the
expansion of a frozen free-streaming field is exactly a uniform scale,
`s(t) = 1 + ExpandRate · (t − t_freeze)`, `ExpandRate = MeanRadialSpeed() /
(0.5 · N · CellCm)` clamped to 0.05–0.6 /s; the component's scale is
`CellCm · s`, and the material gets `Thin = 1/s³` (density conservation) so
the far cloud thins honestly instead of fading on a timer.

**Air branch**: all steps for `ActiveAirSeconds` (3.0 s), then frozen; the
whole box keeps rising at the mean vertical velocity at the freeze, decaying
τ 2 s; the material erodes it. Any impulse (round, pawn, flyer, shell) re-wakes
the sim for `WakeSeconds` 1.0 s.

**Solids.** At spawn, two traces from the muzzle on `ECC_Visibility` (debris
ignores it, so shells are not hit): along the bore for `CloudCm`, and along the
local gravity (`OSGravityAt`) for `CloudCm`. Each hit becomes a plane in cloud
space; `RebuildSolids` marks cells behind them. A slug fired at the floor
splashes radially along the slab and the projection rolls its rim up: the real
"ground-shot" look. Also: if the floor plane is inside the box, the grid is
shifted so the floor sits at cell 2 from the bottom (no wasted voxels under
the slab).

**Box follow.** Each tick, if `Centroid` is > 1.5 cells from the grid centre,
`Shift` by the integer offset and move `FCloud::Centre` by `Basis · offset ·
CellCm`. The HV component is re-placed every tick anyway.

**Box orientation.** `Basis = Bore · FQuat(yaw 23°) · FQuat(roll 17°)` — a fixed
skew, so no voxel axis lies along the view down the bore (the "cross" streaks
of 19.10 were view-aligned columns; a skewed grid cannot make them). The sim
is orientation-agnostic: `Down` is gravity in cloud space.

### 3.2 Feeding the renderer: the grid texture

`UTexture2D::CreateTransient(8·N, (N/8)·N, PF_B8G8R8A8)` per cloud (shotgun:
256×128, 128 KB), `Filter = TF_Nearest`, `SRGB = false`, `NeverStream`,
updated each tick with `UpdateTextureRegions(0, 1, &Region, 8·N·4, 4, Data,
cleanup)` — runtime-safe, render-thread copy, no RHI recreation. The atlas is
8 tiles across × N/8 down, slice Z at tile (Z % 8, Z / 8). The material loads
texels with `Grid.Load(int3(px, 0))`: no sampler, no filtering, so the value
is the voxel's. (A `UVolumeTexture` via `RHIUpdateTexture3D` would also work
in 5.8, but the material needs a volume-texture asset for the parameter's
default and Python has no clean factory for it; the atlas avoids both.)

The HV component's `VolumeResolution = N`, so preshading samples the material
once per sim voxel at its centre and the sparse-voxel march draws
piecewise-constant cubes. Set on the component: `LightingDownsampleFactor =
1`, `ShadowStepFactor = 1`, `StepFactor = 1` (5.8 names; if a property is
missing, the matching `r.HeterogeneousVolumes.*` cvar).

Cost: the Volume material drops from 8 gaussians + 5 noise octaves per voxel
to one texel load + a few ALU: preshading ≈ 32³ trivial voxels. Upload 128 KB
per active cloud per frame.

### 3.3 `M_OS_MuzzleSmokeVolume` — new `VOLUME_HLSL`

Inputs: `W` world pos, `C` centre, `BX BY BZ` basis, `H` half cm, `N` cells,
`Grid` (TextureObject parameter, 2D), `S` seed, `T` age, `L` life, `Th` thin
(vacuum 1/s³), `Er` erode strength, `E` eye (xyz local, w clear cm), `Tm`
game time. Output float4: x extinction share 0..1, y warm share, z ember, w
fringe mask.

```hlsl
const float M[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
float3 d = W - C;
float3 l = float3(dot(d, BX), dot(d, BY), dot(d, BZ));
float3 u = l / (2 * H) + 0.5;
if (any(u < 0) || any(u >= 1)) return 0;
int n = (int)N;
int3 v = int3(floor(u * n));                       // the sim's voxel: the only thing the eye ever sees
int2 px = int2(v.x + (v.z % 8) * n, v.y + (v.z / 8) * n);
float4 g = Grid.Load(int3(px, 0));                 // r density, g heat, b churn, a solid
if (g.a > 0.5) return 0;
// 3D ordered dither on the voxel grid and a per-voxel hash (its own erosion threshold, its own ember phase).
float th = (M[((v.x + v.z) & 3) + 4 * ((v.y + 2 * v.z) & 3)] + 0.5) / 16;
float hv = frac(sin(dot(float3(v) + S * 31, float3(127.1, 311.7, 74.7))) * 43758.5453);
// Erosion: a floor that rises over the second half of the life, higher for voxels with a high hash:
// thin voxels fail first, holes open, lumps outlast the rest. No uniform fade.
float erode = Er * saturate((T / L - 0.45) / 0.55) * (0.35 + 0.65 * hv);
float dens = g.r * Th - erode;
// 3 bands (thin / mid / dense), the dither deciding between neighbouring bands per voxel.
float band = floor(saturate(dens) * 3 + th);
if (band < 1) return 0;
// Eye clear, as before.
band *= smoothstep(E.w * 0.4, E.w, length(l - E.xyz)) >= 0.5 ? 1 : 0;
// Embers: the hottest voxels in the fresh core glow and flicker out one by one (24 Hz hash).
float flick = frac(sin(dot(float3(v), float3(12.9898, 78.233, 37.719)) + floor(Tm * 24) * 0.618) * 43758.5453);
float ember = (g.g > 0.62 + 0.3 * th) ? saturate(g.g - 0.5) * 2 * (0.4 + 0.6 * flick) : 0;
// Fringe only on thin voxels that are moving (churn): the crumbling rim glitters, still smoke does not.
float fringe = (band == 1 && g.b > 0.25) ? lerp(1, 0.4, fmod(abs(v.y), 2)) : 0;
return float4(band / 3, saturate(g.g * 1.4), ember, fringe);
```

Material wiring (`_volume` in `os_muzzle_fx.py`): delete the Puff/Wake
parameters; add `Grid` (`MaterialExpressionTextureObjectParameter`, default
`/Engine/EngineResources/DefaultTexture`), scalars `Cells 32`, `Age 0`, `Life
6`, `Thin 1`, `Erode 0.9`, `Time` from `MaterialExpressionTime`.
Outputs: `Extinction = x · DenseExtinction` (Subsurface pin); `BaseColor =
lerp(Colour, WarmColour, y)`; `Emissive = z · EmberColour + x · Colour ·
Ambient + w · x · Fringe · FringeColour`.
Values: `Colour (0.42, 0.42, 0.40)`, `WarmColour (0.50, 0.40, 0.30)`,
`EmberColour (2.2, 0.55, 0.10)`, `Ambient 0.02`, `Fringe 0.08`,
`FringeColour (0.35, 0.55, 0.80)` (cool; the warm alternate row comes from the
`lerp` in `fringe`). `DenseExtinction` per gun (3.6): bands give ⅓, ⅔, 1 of it.
The albedo is brighter than now on purpose: with τ ≈ 2–3 through a lump the
dark side comes from shadow, not from a dark albedo, and the owner's "no blown
white" is held by `Ambient 0.02` (the old 0.12 was the white).

### 3.4 `OSMuzzleFx` C++ changes

`FOSMuzzleFx` (replaces `CloudVoxels`, `CloudThrowCm`, `TunnelCm`,
`SmokeSpreadCm`-for-clouds; the card fields stay):

```cpp
/** The sim grid: cells a side and a cell's size, cm (the box is Cells x CellCm). */
int32 Cells = 24;  float CellCm = 3.0f;
/** The slug of gas at the shot: its radii along the bore and across, cm, its speed out of the bore and sideways, cm/s. */
float SlugLengthCm = 10.0f; float SlugRadiusCm = 4.0f; float SlugSpeedCm = 350.0f; float SlugRadialCm = 60.0f;
/** Extinction of the dense band, 1/cm (the bands are 1/3, 2/3, 1 of it). */
float DenseExtinction = 0.10f;
/** A round or a blast through a cloud: push out of its line and along it, cm/s, within RadiusCm. */
float RoundRadiusCm = 8.0f; float RoundPushCm = 120.0f; float RoundAlongCm = 200.0f;
float ShoveCm = 0.0f;  // kept
FOSSmokeSim::FParams Sim;  // Buoyancy, DragSeconds, Vorticity, HeatSeconds, DecayPerSecond, Jacobi
float ActiveAirSeconds = 3.0f; float ActiveVacuumSeconds = 1.2f; float Erode = 0.9f; float CloudSeconds = 6.0f;
```

`FCloud` gains `TUniquePtr<FOSSmokeSim> Sim; TObjectPtr<UTexture2D> Grid;
TArray<FColor> Atlas; float Awake; float Scale; FVector RiseVelocity;` and
loses `Velocity, DriftVelocity, PuffThrow, Wakes`. `FWake`/`AddWake` become
`Blast(Origin, Dir, RadiusCm, Along, Push, Except)` = `Sim->Impulse` on every
cloud whose box the line crosses (+ `Awake = WakeSeconds`).

`SpawnCloud`: `Sim->Reset(Cells, CellCm, bAir, Down)`; traces → `Planes`;
`Inject(muzzle in cloud space, (SlugLengthCm, SlugRadiusCm, SlugRadiusCm)
turned along the bore, Rho 1, Heat 1, Bore · SlugSpeedCm · (1 − SmokeBrake),
SlugRadialCm)`; for a brake (`SmokeBrake > 0`) two more slugs at the ports
along ±Y with `SlugSpeedCm · SmokeBrake / 2`. `Carried = GunVelocity` as now.
Create the transient texture once per slot, `Material->SetTextureParameterValue("Grid", Cloud.Grid)`.

`TickClouds` per living cloud:
1. `Age += Dt`; `bAir` re-read from `OSAirZoneAt(Centre)` (a cloud drifting
   out of an air zone turns ballistic: honest).
2. Active (`Age < ActiveSeconds || Awake > 0`): `Sim->Step(min(Dt, 1/30))`;
   box follow (`Shift`); `Pack(Atlas)`; `UpdateTextureRegions`. At most
   `MaxActiveSims` (2) clouds step per frame: the oldest beyond that freeze.
3. Frozen: vacuum → `Scale` grows, `Thin = 1/Scale³`; air → `Centre +=
   RiseVelocity · Dt`, `RiseVelocity *= e^(−Dt/2)`.
4. Pawn through the box: `Blast(Eye, EyeStep, 18, |EyeStep|/Dt · 0.5, 40)`.
   Flyers as now with `Blast`.
5. Params: `CloudCentre, CloudX/Y/Z, HalfCm = 0.5·Cells·CellCm·Scale, Cells,
   Age, Life, Thin, Erode, Eye`; transform `(Basis, Centre, CellCm·Scale)`.

`Shot`: `Blast(View, ViewForward, RoundRadiusCm, RoundAlongCm, RoundPushCm,
nullptr)` for the round (the tunnel and its ring now come out of the pressure
solve: a line impulse in an incompressible gas *is* a tunnel with a vortex
ring at its mouth); shotgun shove = `Impulse` along the bore in a 400 cm cone
scaled by distance, as now but into the sim.

### 3.5 Hot shells (`OSWeapons` + `OSMuzzleFx`)

`UOSMuzzleFxComponent::HotCase(AActor* Case)`; called from
`UpdateCycleEvents` right after `SpawnDebris(Shot.CaseMesh, …)` with
`Debris.Last().Get()`. `TArray<FHotCase>{ TWeakObjectPtr<AActor>, float Heat
= 1, float Carry }`, heat τ 1.4 s. Per tick, per case with `Heat > 0.15`:

- inside a cloud's box: `Sim->Inject(pos, (1.5 cell), Rho 0.25·Heat·Dt·30,
  Heat 0.5·Heat, CaseVelocity · 0.4, 0)` + `Impulse(pos, dir, 6 cm, |v|·0.3,
  |v|·0.2)` → the case drags a thin wisp and cuts a wake through the cloud,
  and wakes a frozen cloud;
- otherwise: card puffs via the existing `FPuff` path (`CasePuffs`), 8/s × Heat
  (carry fraction as the seep), `StartCm 0.8`, `EndCm 5`, `Seconds 1.6`,
  `Opacity 0.18`, `Lift` in air as `PortPuffs`, direction = case velocity · 0.2
  + random. A shell lying on the floor smokes for a second after the shot.

### 3.6 Port smoke of the shroud (`FOSPortSmoke`, `SMOKE_HLSL`)

`SMOKE_HLSL`: `BANDS 5 → 3`; add inputs `Tm` (time) and `Ag` (instance data 7,
0..1 "liveliness" = `1 − Age/Life`): after `pr` is built,
`pr += 0.16 * Ag * float2(sin(Tm * 1.9 + S * 9 + pr.y * 2.6), cos(Tm * 1.4 + S * 5 + pr.x * 2.6));`
— the rags of a puff keep moving while it lives. `Colour` → `(0.42, 0.42,
0.40)` = the cloud's, `Ambient 0.25 → 0.04`, `Fringe 0.5 → 0.25`, fringe colours
the cloud's pair. `SetNumCustomDataFloats(7 → 8)`.

C++ (`TickComponent` cards): a sinuous path and a wisp drawn out along its
rise: `Sway = (Bore.Y ⟂ rise) · 1.4 cm · sin(Age · 3.2 + Seed · 6.28) · (1 −
e^(−Age/0.3))` added to `World`; `Along = normalize(Direction · SpreadCm ·
Throw + RisenVelocity · 0.6)` where `RisenVelocity = Lift · τ · (1 −
e^(−Age/τ))` — a seep wisp stands up as it rises. Vacuum: no sway, no rise
(straight, as now).

RMB-93 values (`OSRmb93.cpp`): `ShotPuffs 8 → 10`, `ShotThrowCm 2.5 → 3.5`,
`SeepPerSecond 5 → 8`, `EndCm 6 → 9`, `Seconds 2.2 → 2.8`, `Opacity 0.1 →
0.22`, `RiseShare 0.05 → 0.12`, `AirDragSeconds 0.5 → 0.7`.

### 3.7 Values per gun

| | Pistol | **Shotgun / Рысь** | SV-98 |
|---|---|---|---|
| `Cells` × `CellCm` (box) | 20 × 3 (60) | **32 × 4 (128)** | 28 × 3.5 (98) |
| `SlugLengthCm` / `SlugRadiusCm` | 6 / 2.5 | **12 / 5** | 7 / 3 (+ port slugs) |
| `SlugSpeedCm` / `SlugRadialCm` | 300 / 40 | **420 / 90** | 380 / 50 |
| `DenseExtinction`, 1/cm | 0.12 | **0.14** | 0.10 |
| `Sim.Buoyancy` / `DragSeconds` | 220 / 0.3 | **260 / 0.35** | 240 / 0.3 |
| `Sim.Vorticity` / `HeatSeconds` | 0.3 / 0.35 | **0.4 / 0.5** | 0.35 / 0.4 |
| `Sim.DecayPerSecond` / `Jacobi` | 0.08 / 10 | **0.06 / 12** | 0.07 / 10 |
| `ActiveAirSeconds` / `ActiveVacuumSeconds` | 2.0 / 1.0 | **3.0 / 1.2** | 2.5 / 1.2 |
| `RoundRadiusCm` / `RoundPushCm` / `RoundAlongCm` | 6 / 100 / 160 | **10 / 140 / 220** | 8 / 120 / 260 |
| `ShoveCm` | — | **90** | — |
| `Erode` / `CloudSeconds` | 0.9 / 4 | **0.9 / 6** | 0.9 / 5 |
| `MaxClouds` / `MaxActiveSims` | 4 / 2 (component) | | |

### 3.8 Cost

- **GPU**: preshading 32³ × (1 load + ~40 ALU) ≪ today's 26³ × (8 gaussians +
  5 octaves). March: same screen area as now, but τ ≥ 2 in lumps ends rays
  earlier. Lighting cache at 32³ full res: tiny. Expect **0.35–0.7 ms** for
  three shotgun clouds at 1080p (now 0.55–0.76). Upload 128 KB × active clouds.
- **CPU** (game thread wall, `ParallelFor` over 32 slices): advect 4 fields
  ≈ 32k × 4 trilinear, projection 12 × 32k, curl/confinement 32k —
  ≈ 6–8 Mflop ≈ 2 ms scalar, **≈ 0.3–0.5 ms wall** on 8 threads per active
  cloud; ≤ 2 active → ≤ 1 ms worst case, 0 when nothing is fresh. Measure with
  a new `os.Fx.SmokeCost` (µs per step, mean/max, like `os.Rmb93.Cost`).

## 4. Crispness under DLAA + HV downsample

1. **Density is piecewise-constant on the render's own voxels** (3.2–3.3):
   there is no sub-voxel gradient left to blur. 4 cm at 2 m under FOV 134 is
   ≈ 9 px, 7 px at 2.5 m, 5 px at 3.5 m: the "6 px texel" rule of the cards.
2. `r.HeterogeneousVolumes.Jitter 0` — stable steps instead of per-frame noise
   that DLAA turns into mush; with the sparse-voxel march there is no stepping
   to hide. `r.HeterogeneousVolumes.Shadows.Resolution 512` stays;
   `LightingDownsampleFactor 1`, `ShadowStepFactor 1` on the component (voxel-
   exact self-shadow). Verify the exact 5.8 names with `r.HeterogeneousVolumes`
   tab-completion before committing them to `DefaultEngine.ini`.
3. **Resolution.** `DownsampleFactor 2` stays the default: the remaining
   softness is a 1-px bilinear edge on a 7–9 px cube, which DLAA on stable
   content sharpens over a few frames; the clouds move slowly (cm/s), so no
   temporal ghosting. Add `os.Fx.SmokeFullRes` (sets `DownsampleFactor 1`):
   measure it in the budget still; keep it only if three clouds stay ≤ 1.5 ms.
4. DLAA cannot exclude a pass; nothing to do there. Stable, quantized,
   high-contrast voxels converge sharp under it; the old soft gradients did not.
5. **Fallback** if preshading in 5.8 turns out to filter between voxels
   (cube edges still soft at `DownsampleFactor 1`): drop HV for the clouds
   and march the atlas ourselves in a Translucent box mesh material (Custom
   HLSL, 48 steps DDA through the voxels, 4 shadow taps toward the key light
   given as a parameter from C++, the flash light as a second point source):
   full-res, point-sampled, fully quantized, ≈ 0.4 ms per cloud at this box
   size. Same sim, same atlas, same bands — only the draw changes.

## 5. Verification

Stand (extend `os.Weapons.Smoke` with moments `room` and `roomspace`):

- **room**: `Lvl_OS_TestRoom` (lit beige, air, gravity), the RMB-93 armed as
  `os.Rmb93.Probe` arms it, pawn at rest, look pitched −25° at the floor
  2.5 m ahead (the owner's screenshot), one shot. First-person stills at
  **0.05, 0.2, 0.5, 1, 2, 4 s** (`OSSmoke_room_0050ms` …), a witness from the
  side (eye, muzzle, floor edge in frame) at 0.2, 0.5, 1, 2 s, then a second
  shot through the cloud at 1.5 s with frames 1, 3, 8, 15, 30, and the shells:
  a close witness on the floor at 0.6 and 1.4 s (a shell smoking). Log per
  still: `DescribeClouds` + sim mass, centroid, mean |U|, max heat.
- **roomspace**: the same shot in `Lvl_OS_ZeroG` facing black space (the
  `space` variant), same moments: the slug flies apart, thins, freezes, scales.
- **budget**: `os.Weapons.Smoke budget` as now (three shotgun clouds,
  `ProfileGPU`), plus `os.Fx.SmokeCost` after it; run twice: default and
  `os.Fx.SmokeFullRes 1`. Pass: HV pass + composite ≤ 1.5 ms; sim ≤ 0.5 ms
  mean per active cloud.
- Before/after sheets: run the same commands on `main` first (the stand code is
  new, so the owner's screenshot stands in for "before" on `room`; `budget`
  and `os.Weapons.Fx` exist on both). `-game -RenderOffscreen -ResX=1920
  -ResY=1080 -UseFixedTimeStep -FPS=60`, DLAA on (the plugin is on the owner's
  PC), so the stills show the real AA.
- What to look for, moment by moment: 0.05 s a dense dark slug with ember
  voxels right at the muzzle, the flash lighting it; 0.2 s the slug flattening
  on the floor, embers nearly out; 0.5 s a rolling disc with a raised rim,
  dark undersides against the lit top; 1 s the rim curled into a ragged ring
  rising, holes opening; 2 s a hanging torn cloud churning slowly, shells on
  the floor with a thread of smoke; 4 s crumbs and holes, lumps left. In space:
  a shell of gas expanding and thinning, no curl, frozen by 1.2 s, growing and
  fading by density, never by alpha.
- Regressions: `os.Rmb93.Probe` (puff counts, heat), `os.Weapons.Smoke
  foot cling burst sv98` (the old moments still read: tunnel and ring now from
  the sim), `os.ZeroG.Probe`, `os.Weapons.SwitchProbe`.

Docs: a new section «Живой дым (22.10)» in `Docs/RecoilWeapons-Fx.md` with
the owner's words and these numbers; the `FOSPortSmoke` paragraph in
`RecoilWeapons-Rmb93.md` updated; `os_muzzle_fx.py` header updated.
