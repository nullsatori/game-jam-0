# Doors, keys, locks

## Что получает игрок

Распашная дверь, которая открывается **от себя** (в сторону от того, кто
толкнул), останавливается о тело (не давит, встаёт приоткрытой), три
независимых замка: засов изнутри, «служебный режим» (снимается рубильником),
замок под ключ (ключ поворачивается отдельным нажатием, потом дверь
открывается). Ключ висит на крючке и берётся F. Распахнутая или закрытая
створка тянет дым через проём ([GasSystem](GasSystem.md#дым-как-система-v12-этап-7c)).

## Классы и файлы

| Файл | Что |
|---|---|
| `MKDoor.h/.cpp` | `AMKDoor` |
| `MKLockRules.h` | `MKResolveDoorVerdict` — порядок замков: засов → служебный → ключ |
| `MKMechanismMotion.h` | `MKAdvanceAlpha`, `MKDoorSwingSign`, `MKRouteCrossesDoorway` |
| `MKKeyring.h/.cpp` | `UMKKeyringComponent` (набор имён ключей), `MKHolderHasKey` |
| `MKKeyPickup.h/.cpp` | `AMKKeyPickup` |

## Как поставить дверь

- **Система координат:** origin актора — ось петель на уровне пола; полотно
  идёт вдоль локальной **+Y** от петель, закрытая дверь смотрит в **+X**.
  «Внутри» (откуда снимается засов) — сторона −X. Для стены вдоль мировой X
  поставить yaw −90.
- `Leaf` — меш полотна. `SM_MK_Door_Wood` (121 × 19 × 204) сделан вдоль своей
  X: компоненту Leaf нужен relative yaw 90 и сдвиг так, чтобы край был в
  петлях (см. `Scripts/build_test_room.py`, станция 1). Рамка
  `SM_MK_DoorFrame_Wood` (132 × 34 × 221) — отдельный StaticMeshActor.
- Ключевые свойства: `OpenAngleDeg` (95), `SwingSeconds` (0.9),
  `RequiredKeyId` (FName, `None` = без замка), `KeyRequiredReason`,
  `bLocked` (служебный, снимает `AMKControlSwitch::DoorsToUnlock`),
  `bBoltedInside`, `bCloseAndLockBehind` + `LockBehindDistanceCm`,
  `NavLink` (smart link для ИИ, необязателен).
- Ключ: `AMKKeyPickup`, `KeyId` = тот же FName, меш (например
  `SM_MK_Yard_KeyTag`, висит **ниже** своего pivot на 0.6–19 см). У того, кто
  берёт, должен быть `UMKKeyringComponent` (у `AOSZeroGPawn` есть).

## Зависимости и конфиг

AIModule, NavigationSystem (путь ИИ, nav link). Конфиг не нужен.

## Ограничения и что вырезано

- `HandleAnchor` (`UMKFrameAnchorComponent`, рамка устройства бодикама) вырезан
  (`MKDoor.h:61`, `MKDoor.cpp:42, 78`), include `MKDeviceHUD.h` и
  `MKEnemyCharacter.h` — тоже (`MKDoor.cpp:10`).
- ИИ, который сам открывает дверь: в MindKore это был `AMKEnemyCharacter`,
  здесь — любой `APawn` с `AAIController`, чей путь проходит через проём
  (`MKDoor.cpp:416`). В стартере ИИ нет — проверено только для игрока.
- Ключи не тратятся и не сохраняются (нет сейва).

## Лицензия

Меши двери и рамки, ключа — собственный арт MindKore (`/Game/MindKore/Art/Admin`),
замыкание ссылок проверено: только `/Game/MindKore` и `/Engine`.
# Шлюз импортированного корабля

На `Lvl_OS_Ship` створки `D8_CLOSED_Leaf` и `D9_CLOSED_Leaf` подключены
к существующим `MKDoor` как `Airlock_D8` (внешняя) и `Airlock_D9` (внутренняя).
Навести на створку в пределах досягаемости и нажать F: открыть/закрыть.
Текущая привязка — `IA_ZG_Use` → `F`, подсказка `[F]`.
Меши и материалы сохранены; декоративные полосы следуют за створкой.
Исходные статические створки/полосы скрыты и имеют `NoCollision`.
Размещение воспроизводится `Scripts/setup_airlock_doors.py` (повторный запуск
отказывается создавать дубликаты). Обе двери без замка; воздух задаётся зонами.
