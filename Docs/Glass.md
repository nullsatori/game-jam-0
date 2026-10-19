# Glass

## Что получает игрок

Стекло цело, пока в него не попали; потом трескается лучами от точки
попадания, выпадает кусками, а неподдержанные осколки осыпаются за
несколько шагов. Дыра — дыра для пули, взгляда и навмеша. Удар тела (Blunt:
тяжёлый быстрый предмет или пешка, `JamDeliverCollisionImpact`, см.
[ZeroG.md](ZeroG.md) «Столкновения») бьёт шире пули; удара ломом в стартере нет.

## Классы и файлы

| Файл | Что |
|---|---|
| `MKGlassPane.h/.cpp` | `AMKGlassPane` — лист стекла, приёмник `IMKImpactReceiver` |
| `MKGlassFracture.h/.cpp` | разбиение на выпуклые осколки (чистая геометрия) |
| `MKGlassMesh.h/.cpp` | сборка `UProceduralMeshComponent` из осколков |
| `MKGlassShardDebris.h/.cpp` | падающие осколки |
| `MKImpactHistory.h/.cpp` | `FMKImpactHistory` — защита от повторной доставки одного выстрела |

## Как поставить

- Origin актора — **центр листа**. Ширина вдоль локальной X, высота вдоль Z,
  толщина вдоль Y. Размер — свойствами `PaneWidthCm`, `PaneHeightCm`,
  `PaneThicknessCm`; **масштаб актора оставить 1**.
- Материалы: `IntactMaterial` = `MI_MK_Glass_Pane`, `CrackedMaterial` =
  `MI_MK_Glass_Cracked`, `DebrisMaterial` = `MI_MK_Glass_Shard`;
  `FractureEdgeMaterial` по умолчанию `M_MK_Glass_FractureEdge`.
- Радиусы: `BreakRadiusCm` (45), `BluntBreakRadiusCm` (60),
  `FractureRadiusCm` (130). `bStartBroken` — уже разбитое.
- Стрелять надо трассой с `bTraceComplex = true` (стекло ловит пулю
  треугольниками осколков), как в `UJamWeaponsComponent::TraceShot`
  (`JamWeapons.cpp:702`).

## Зависимости и конфиг

ProceduralMeshComponent, Niagara, AIModule. Канал `Shot` (см.
[Impacts-Vitals](Impacts-Vitals.md)).

## Ограничения и что вырезано

- **TEMPORARY:** эффект `BreakVfx` (`NS_MK_GlassBreak` ссылался на сторонний
  `/Game/A_Surface_Footstep`) и звуки `BreakSound` / `ShedSound` /
  `DebrisLandingSound` (кью из `Essential_Foosteps_SK`) по умолчанию пусты
  (`MKGlassPane.cpp:83, 94`). Стекло бьётся **без эффекта и звука**. Снять,
  когда в стартере появятся свои Niagara-система и звук стекла.
- `MKBodyResponseComponent.h` заменён на `MKImpactHistory.h` (из него
  использовался только `FMKImpactHistory`) (`MKGlassPane.h:5`).
- Окно `SM_MK_Window_150` не скопировано: в нём своя «ночная» плоскость
  стекла, которая перехватила бы выстрел.

## Лицензия

Материалы стекла — собственные MindKore.
