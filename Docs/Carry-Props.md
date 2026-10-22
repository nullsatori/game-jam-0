# Carry and props

## Что получает игрок

В игре — физические тела (`AMKPhysicsProp`, `AMKCart`): масса, удары, шум;
на предмет можно встать. Берёт их игровая пешка `AOSZeroGPawn` своим хватом
(ПКМ), а не этим модулем: `UMKCarryComponent` у неё нет, поэтому
`AMKPhysicsProp` на F ничего не предлагает (`QueryInteraction` без
`UMKCarryComponent` → `None()`, `MKPhysicsProp.cpp:38`). Хват — в
[ZeroG.md](ZeroG.md) («Хват»).

Перенос `UMKCarryComponent` висит только на legacy-пешке `AOSCharacter`
([OSCharacter.md](OSCharacter.md)): предмет берётся E и держится перед
камерой как настоящее тело — стукается о стены, отстаёт, если упёрся, сам
выпадает, если долго зажат. ЛКМ — бросок (скорость падает с массой), ПКМ/F —
положить. Тяжёлое (> 35 кг) не поднять, на чём стоишь — не поднять.

## Классы и файлы

| Файл | Что |
|---|---|
| `MKCarryComponent.h/.cpp` | `UMKCarryComponent` — «предмет в руках» (`UPhysicsHandleComponent`); стоит только на legacy `AOSCharacter` |
| `MKPhysicsPropComponent.h/.cpp` | `UMKPhysicsPropComponent` — контракт предмета: состояние, удары, шум |
| `MKPhysicsProp.h/.cpp` | `AMKPhysicsProp` — простой предмет: один симулируемый меш + `MassKg` |
| `MKCart.h/.cpp` | `AMKCart` — тележка с тормозом (толкается, не поднимается) |
| `MKPropMath.h` | бросок, натяжение, окно контакта |
| `MKPhysicsBridge.h/.cpp` | `ApplyPointImpulse` (Н·с → кг·см/с) |

## Как поставить

- `AMKPhysicsProp` в уровень, меш в `Body`, `MassKg` (масса задаётся, а не
  считается из объёма). Свойства `Prop`: `bCarryable`, `bThrowable`,
  `MinImpactSpeedCm`.
- **Меш обязан иметь простую коллизию**, иначе тело не симулируется и
  предмет не берётся (`SM_plywood_sheet` из MindKore её не имеет — поэтому
  не скопирован).
- Масштаб актора работает (в комнате доска и ящик — растянутые кубы).
- Настройки рук — `UMKCarryComponent::Hands` (`MaxMassKg` 35,
  `HoldClearanceCm`, жёсткость, `ThrowImpulseNewtonSeconds` 60,
  `MaxThrowSpeedCm` 1100).

## Зависимости и конфиг

Engine, PhysicsCore. Профиль `PhysicsActor`.

## Ограничения и что вырезано

- `IsStandingOn` читал пол у `AMKMoverPawn`/`UMKChaosMoverComponent`; здесь —
  `UCharacterMovementComponent::CurrentFloor` у `ACharacter`
  (`MKCarryComponent.cpp:7, 67`).
- `bHandsBusy` никто не пишет (владелец рук MindKore не перенесён).
- `MKPhysicsBridge::AngularDrive` вырезан — требовал плагин PhysicsControl
  (`MKPhysicsBridge.h:4`, `.cpp:4`).
- `AMKPhysicsProp` как получатель удара принимает только Blunt (решение
  MindKore), но пуля оружия стартера всё равно толкает любое симулируемое
  тело в точке попадания: `AddImpulseAtLocation(Direction * HitImpulse)` в
  `OSWeapons.cpp:765`.

## Лицензия

`SM_MK_Yard_Drum` — собственный арт MindKore.
