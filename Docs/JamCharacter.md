# JamCharacter (пешка, режим, HUD)

## Что получает игрок

Пешка от первого лица без рук: камера на высоте глаз, ходьба, прыжок,
присед, взаимодействие, переноска, тестовый выстрел, замедление времени.
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
- `Fire()`: см. [Impacts-Vitals](Impacts-Vitals.md). Луч из `Camera` по
  `GetControlRotation()` (а не из кэша камеры: так выстрел верен и в кадр
  телепорта). Звук — `SW_MK_Blast_Transient` + `SW_MK_Blast_Tail` 2D,
  декаль `M_Decal_Default` на 30 с. Свойства: `ShotRangeCm` 5000,
  `ShotDamage` 60, `ShotImpulseNewtonSeconds` 12, `ImpactData`.
- Попадание в пешку → `Vitals->ApplyIncoming`.
- G переключает тег `MK.Crowbar` (лог `Jam: ... got/dropped the crowbar tag`).
- `AJamHUD`: Canvas-текст движковым шрифтом.

## Ограничения

- Рук и оружия нет, отдачи и анимаций нет; выстрел — отладочный хитскан.
- Смерти нет (здоровье держится на 0).

## Лицензия

Звуки выстрела — CC0 (см. [CREDITS](../CREDITS.md)).
