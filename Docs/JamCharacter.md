# JamCharacter (пешка, режим, HUD)

## Что получает игрок

Пешка от первого лица без рук: камера на высоте глаз, ходьба, прыжок,
присед, взаимодействие, переноска, стволы невесомой пешки, замедление времени.
HUD — точка прицела, действие под ней, строка состояния.

## Классы и файлы

| Файл | Что |
|---|---|
| `JamCharacter.h/.cpp` | `AJamCharacter : ACharacter, IMKImpactReceiver` |
| `JamGameMode.h/.cpp` | `AJamGameMode` (pawn + HUD), `AJamHUD` |
| `Config/DefaultInput.ini` | `EnhancedPlayerInput` / `EnhancedInputComponent` по умолчанию |

## Устройство

- Компоненты: `Camera` (z = 64 от центра капсулы, `bUsePawnControlRotation`),
  `Interaction`, `Carry`, `Keyring`, `Vitals`.
- Ввод собирается в C++ (`BuildInput`): `UInputAction` и
  `UInputMappingContext` создаются через `NewObject`, ассетов ввода нет.
  Контекст добавляется в `PawnClientRestart`. Раскладка — в
  [README](../README.md#управление).
- Присед — движковый (`bCanCrouch`, `CrouchedHalfHeight` 40 при капсуле
  34 × 88). В воздухе капсула сжимается к центру, поэтому присед после
  отрыва поднимает ноги на 48 см (crouch-jump). Прыжок из приседа движок не
  даёт. `JumpZVelocity` по умолчанию (420 → ~90 см), `AirControl` 0.35.
- Стволы — `UJamWeaponsComponent` (`Guns`, см.
  [RecoilWeapons](RecoilWeapons.md)): пистолет, дробовик, снапа, все открыты;
  1 / 2 / 3, колесо, R; ЛКМ — выстрел (пистолет держать — автоогонь), с
  предметом в руках — бросок. `Fire()` стреляет из `Camera` по
  `GetControlRotation()` (а не из кэша камеры: так выстрел верен и в кадр
  телепорта) и добавляет в скорость долю отдачи `RecoilFraction` 0.1 (на
  земле — только вдоль земли). Трасса и попадание — см.
  [Impacts-Vitals](Impacts-Vitals.md).
- Попадание в пешку → `Vitals->ApplyIncoming`.
- G переключает тег `MK.Crowbar` (лог `Jam: ... got/dropped the crowbar tag`).
- `AJamHUD`: Canvas-текст движковым шрифтом.

## Ограничения

- Рук и моделей оружия нет, анимаций нет. На корабле эта пешка не
  используется (там ходит `AJamZeroGPawn`, см. [Ship-Blockout](Ship-Blockout.md)).
- Смерти нет (здоровье держится на 0).

## Лицензия

Звуки выстрела — CC0 (см. [CREDITS](../CREDITS.md)).
