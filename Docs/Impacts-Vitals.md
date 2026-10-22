# Impacts and vitals

## Что получает игрок

Одно попадание — один путь: трасса → классификация (поверхность, зона тела,
реакция, множитель урона) → приёмник сам решает, что с ним делать (стекло
бьётся, манекен теряет здоровье и вздрагивает, игрок платит здоровьем и
бронёй). Попадание в голову сильнее, бронепластина ослабляет.

## Классы и файлы

| Файл | Что |
|---|---|
| `MKImpactTypes.h/.cpp` | `FMKResolvedImpact`, `FMKImpactFeedback`, `UMKImpactDataAsset`, зоны, `MKSelectResolvedHit`, поверхности |
| `MKImpactResolver.h/.cpp` | `MKImpactResolver::ResolveFeedback` (таблица поверхность/зона) |
| `MKImpactDelivery.h/.cpp` | `MKResolveImpactFromHit`, `MKDeliverResolvedImpact` (+ `UAISense_Damage`) |
| `MKImpactReceiver.h` | `IMKImpactReceiver::ReceiveResolvedImpact` |
| `MKHitZoneComponent.h/.cpp` | коробка-зона (Overlap на `Shot`) |
| `MKImpactTarget.h/.cpp`, `MKImpactDummy.h/.cpp` | мишень со здоровьем; манекен из кубов и зон |
| `MKVitals.h/.cpp` | `UMKVitalsComponent` (здоровье H, броня A), `MKApplyIncomingDamage` |

## Как пользоваться

- Стрелять: multi-trace по `ECC_GameTraceChannel2` (Shot), `bTraceComplex`,
  `bReturnPhysicalMaterial`; выбрать попадание `MKSelectResolvedHit`;
  `MKResolveImpactFromHit`; заполнить `ShotId` (новый `FGuid` на выстрел),
  `Kind`, `SourceActor`, `DamageAmount = база × DamageMultiplier`,
  `ImpulseNewtonSeconds`, `bHasPhysicsImpulse`; отдать
  `MKDeliverResolvedImpact`. Пример — `UOSWeaponsComponent::TraceShot`
  (`OSWeapons.cpp:702`).
- Свой приёмник: реализовать `IMKImpactReceiver` (можно в Blueprint).
  Не вызывать `Execute_ReceiveResolvedImpact` напрямую.
- Таблица отклика — `DA_Impact_Default` (звуки попаданий по телу, кровь,
  на поверхность — встроенный fallback). Декаль по умолчанию в стартере —
  `M_Decal_Default`.
- `UMKVitalsComponent`: `MaxHealth` 100, `MaxArmor` 50, `StartingArmor` 0,
  `AbsorbFraction` 0.6; `ApplyIncoming(Damage, Kind, bBlocked, EventId)`,
  `Restore(Health, Armor)`, `Revive()`; при 0 здоровья — предупреждение в лог и
  событие `OnDepleted`. Что дальше, решает слушатель: в джеме это
  `AOSShipGameMode` (возрождение в каюте); без слушателя здоровье просто
  держится на нуле.

## Обязательный конфиг (`Config/DefaultEngine.ini`)

- Канал объектов `Projectile` = `ECC_GameTraceChannel1`, канал трасс `Shot` =
  `ECC_GameTraceChannel2` (по умолчанию Block), профили `Projectile`,
  `MKCharacterBody`, правка `Trigger` (игнор Projectile).
- Физповерхности: SurfaceType1..6 = `MK_Metal`, `MK_Concrete`, `MK_Flesh`,
  `MK_Armor`, `MK_Glass`, `MK_Wood` (физматериалы `PM_MK_*`). Порядок важен:
  `MKSurfaceKindFromPhysical` читает именно эти номера.

## Ограничения и что вырезано

- `MKVitals`: `MKBodyResponseComponent.h` → `MKImpactHistory.h`
  (`MKVitals.h:5`), `MKMeleeDefence.h` вырезан, однострочное
  `MKBlockAppliesTo` скопировано (`MKVitals.cpp:5, 14`).
- `MKHitZoneRig` (зоны на скелете) не перенесён — нужен только врагам.
- **Отличие от MindKore** (`OSWeapons.cpp:729`): у `AMKImpactDummy` нет
  ничего, что блокирует `Shot`, поэтому по правилу MindKore выстрел уходил в
  стену за ним. В `TraceShot` ближайшая зона в таком случае считается
  блокирующим попаданием.
- Попадания по стенам из движковых кубов — поверхность Default (у кубов нет
  `PM_MK_*`), звука удара о твёрдое нет: в MindKore нет чистого CC0-звука для
  этого.

## Лицензия

Звуки попаданий по телу из `DA_Impact_Default` (`SW_MK_Hit_*`) нарезаны из
CC0 и одного CC BY 4.0 донора — см. [CREDITS](../CREDITS.md). Декали,
физматериалы, кровь (`NS_MK_HeadshotBlood`) — собственные MindKore.
