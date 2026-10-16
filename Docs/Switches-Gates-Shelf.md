# Switches, gates, shelf (и свет, лифт, бытовые приборы)

## Что получает игрок

Рубильник на стене действует на явно указанные объекты: открывает роллету,
снимает служебный замок с дверей, двигает стеллаж на рельсах, гасит/включает
свет, вызывает лифт. Ручка перекидывается, лампа горит, пока рубильнику
есть что дать. Роллета не давит: если под ней тело — едет обратно. Стеллаж
не едет, пока в его пути кто-то стоит.

## Классы и файлы

| Файл | Что |
|---|---|
| `MKControlSwitch.h/.cpp` | `AMKControlSwitch`, `MKCurrentThrough` |
| `MKGate.h/.cpp` | `AMKGate` — панель, едущая на `OpenOffsetCm` |
| `MKShelfBlock.h/.cpp` | `AMKShelfBlock` — блок стеллажей между двумя позициями |
| `MKLift.h/.cpp` | `AMKLift` — кабина с дверью-`AMKGate` и доступом по ключу |
| `MKFlickerLightActor.h/.cpp` | `AMKFlickerLightActor` — мерцающая лампа, `SetLit` |
| `MKAppliance.h/.cpp` | `AMKAppliance` — прибор с состояниями (микроволновка, унитаз) |

## Как поставить

- **Рубильник:** меши `SM_MK_Switch_Body/Handle/Lamp` в компоненты
  `Body/Handle/Lamp`; `LampLitMaterial` = `M_Pilot_Amber`, `LampDarkMaterial` =
  `M_MK_Switch_LensDark`. Задняя грань Body в x = 0, смотрит в **+X**: спиной
  к стене. Цели: `Gates`, `DoorsToUnlock`, `ShelfBlocks`, `Lift`, `Lights`.
  `bOneShot` (по умолчанию true — сработал один раз), `bToggleGates`
  (открыть/закрыть попеременно), `bMomentary` (кнопка), `ActionLabel`.
- **Роллета `AMKGate`:** меш в `Panel` (`SM_MK_Fac_Shutter` 150 × 170 или
  `SM_MK_FL_GrilleLeaf130` 130 × 220). `OpenOffsetCm` в локальных осях
  (вверх `(0,0,230)` или вбок), `TravelSeconds`, `bStartOpen`. Закрытая
  позиция — относительная позиция Panel на BeginPlay.
- **Стеллаж `AMKShelfBlock`:** сам ничего не рисует — меши стеллажей
  (`SM_MK_ArchiveRack_90` 90 × 45 × 212 + `SM_MK_ArchiveFolders_A`)
  ставятся отдельными StaticMeshActor с mobility **Movable** и
  **прикрепляются** к нему. `TravelOffsetCm`, `TravelSeconds`,
  `FootprintCenterCm` / `FootprintHalfExtentCm` (локально, по габаритам
  стеллажей), `ObstructionPaddingCm`.
- **Свет:** `AMKFlickerLightActor` (`BaseIntensity`, `FlickerIntensity`,
  `AttenuationRadius`, `EmissiveFixtureActor` — актор, чьи материалы с
  параметром `Brightness` мерцают вместе со светом). Положить в `Lights`
  рубильника (тогда `bOneShot = false`).
- **Приборы:** `BP_MK_Microwave`, `BP_MK_Toilet` просто ставятся в уровень.

## Зависимости и конфиг

Engine, AIModule (шум). Конфиг не нужен.

## Ограничения и что вырезано

Код перенесён без изменений. Лифт в тестовой комнате не стоит (нужна кабина).
Звук щелчка (`UseSound`) не назначен.

## Лицензия

Меши рубильника, роллеты, решётки, стеллажей, лампы-подвеса и
`BP_MK_Microwave`/`BP_MK_Toilet` — собственный арт MindKore. Звуки приборов —
CC0 (см. [CREDITS](../CREDITS.md)).
