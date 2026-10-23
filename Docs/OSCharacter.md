# OSCharacter (пешка, режим, HUD)

## Статус: legacy, в игре не используется

Ни одна карта эту пешку не запускает. `Config/DefaultEngine.ini`
(`GlobalDefaultGameMode=/Script/MindKore.OSShipGameMode`) и все скрипты сборки
карт (`build_ship.py`, `build_range.py`, `build_test_room.py`,
`build_zerog.py`) ставят `AOSShipGameMode`, а он спавнит `AOSZeroGPawn`;
в `Content/OS/Maps/*.umap` ссылок на `AOSGameMode`/`AOSCharacter` нет.
Наземная ходьба — тот же `AOSZeroGPawn` в зоне гравитации (см.
[ZeroG.md](ZeroG.md)). Код остаётся в сборке: `AOSGameMode` ставит его как
`DefaultPawnClass` (`OSGameMode.cpp:25`), а
`AOSZeroGPawn::UpdateBulletTimePresentation` — копия его метода. Удаление
не сделано (Review-2026-10-19 №35). Ниже — что этот код делает, если
поставить `AOSGameMode` вручную.

## Что получает игрок

Пешка от первого лица без рук: камера на высоте глаз, ходьба, прыжок,
присед, взаимодействие, переноска, стволы невесомой пешки, замедление времени.
HUD — точка прицела, действие под ней, строка состояния.

## Классы и файлы

| Файл | Что |
|---|---|
| `OSCharacter.h/.cpp` | `AOSCharacter : ACharacter, IMKImpactReceiver` |
| `OSGameMode.h/.cpp` | `AOSGameMode` (pawn + HUD), `AOSHUD` |
| `Config/DefaultInput.ini` | `EnhancedPlayerInput` / `EnhancedInputComponent` по умолчанию |

## Устройство

- Компоненты: `Camera` (z = 64 от центра капсулы, `bUsePawnControlRotation`),
  `Interaction`, `Carry`, `Keyring`, `Vitals`.
- Ввод собирается в C++ (`BuildInput`): `UInputAction` и
  `UInputMappingContext` создаются через `NewObject`, ассетов ввода нет.
  Контекст добавляется в `PawnClientRestart`. Раскладка своя (таблица в
  README — для `AOSZeroGPawn`): WASD, мышь, Space — прыжок, C / Left Ctrl —
  присед, E — взаимодействие, ЛКМ — выстрел или бросок, ПКМ / F — положить,
  Q (держать) — замедление, G — лом, R / цифры / колесо — стволы.
- Присед — движковый (`bCanCrouch`, `CrouchedHalfHeight` 40 при капсуле
  34 × 88). В воздухе капсула сжимается к центру, поэтому присед после
  отрыва поднимает ноги на 48 см (crouch-jump). Прыжок из приседа движок не
  даёт. `JumpZVelocity` по умолчанию (420 → ~90 см), `AirControl` 0.35.
- Стволы — `UOSWeaponsComponent` (`Guns`, см.
  [RecoilWeapons](RecoilWeapons.md)): пистолет, дробовик, снапа, все открыты
  (`bStartWithAllWeapons` по умолчанию true; `AOSZeroGPawn` его выключает);
  цифры 1.. по списку стволов, колесо, R; ЛКМ — выстрел (огонь при зажатой ЛКМ — только у ствола с `bAutomatic`, таких нет), с
  предметом в руках — бросок. `Fire()` стреляет из `Camera` по
  `GetControlRotation()` (а не из кэша камеры: так выстрел верен и в кадр
  телепорта) и добавляет в скорость долю отдачи `RecoilFraction` 0.1 (на
  земле — только вдоль земли). Трасса и попадание — см.
  [Impacts-Vitals](Impacts-Vitals.md).
- Попадание в пешку → `Vitals->ApplyIncoming`.
- G переключает тег `MK.Crowbar` (лог `OS: ... got/dropped the crowbar tag`).
- `AOSHUD`: Canvas-текст движковым шрифтом.

## Ограничения

- Рук и моделей оружия нет, анимаций нет. Ни на одной карте не используется
  (см. «Статус»).
- Смерти нет (здоровье держится на 0).

## Лицензия

Звуки выстрела — CC0 (см. [CREDITS](../CREDITS.md)).
