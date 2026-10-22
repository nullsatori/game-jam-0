# Bullet time

## Что получает игрок

Держишь V ([клавиши](Controls.md)) — мир замедляется до 0.2 за 0.1 с, в кадре пост-эффект, звук входа
и выхода. Запас — 3 реальные секунды на всю сессию (без перезарядки);
отпустил — время возвращается за 0.08 с.

## Классы и файлы

| Файл | Что |
|---|---|
| `MKTimeDomainState.h` | `FMKTimeDomainState` — чистая машина состояний, тратит только реальное время |
| `MKTimeDomainSubsystem.h/.cpp` | `UMKTimeDomainSubsystem` — единственный, кто пишет `TimeDilation` |
| `OSZeroGPawn.cpp` (`UpdateBulletTimePresentation`, `BulletTimePressed/Released`) | порт ~40 строк из `MKCameraBulletTime.cpp`: звук входа/выхода, MID (копия того же кода в legacy `OSCharacter.cpp`) |

## Как пользоваться

- `GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>()->SetRequested(Pawn, true/false)`
  — нажал/отпустил. Пешка должна быть локальной и под игроком.
- `RefillResource()` — вернуть 3 с (только в Idle). `NotifyDecisiveImpact` —
  досрочный выход по решающему попаданию владельца (в стартере не вызывается).
- Пост-эффект: `M_MK_BulletTime`, скалярные параметры `Amount` (= альфа
  замедления), `EntryAge`, `ArcVisible` (всегда 0), `Aspect`. В
  `AOSZeroGPawn` MID лежит в `Camera->PostProcessSettings.WeightedBlendables`,
  вес 1 пока альфа > 0.

## Зависимости и конфиг

Engine. Конфиг не нужен.

## Ограничения и что вырезано

- Из камеры MindKore не перенесены: микс `SMix_MK_BulletTime` (глушил
  эмбиенс) и дуга траектории револьвера (`ArcA..C`).
- Вид пост-эффекта и звук на стенде не проверялись (смоук шёл с NullRHI).

## Лицензия

`SW_MK_TimeEntry`, `SW_MK_TimeExit` — синтез MindKore, без доноров.
`M_MK_BulletTime` — собственный материал MindKore.
