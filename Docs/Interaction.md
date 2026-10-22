# Interaction

## Что получает игрок

Смотришь на предмет — под прицелом появляется одно действие («Открыть»,
«Взять ключ», «Вскрыть»). Если предмет отказывает, рядом причина
(«Заперто на ключ»). F выполняет ровно то, что написано (подсказка `[F]`;
клавиши — [Controls](Controls.md)). Никаких меню.

## Классы и файлы

| Файл | Что |
|---|---|
| `Source/MindKore/MKInteractable.h/.cpp` | `IMKInteractable` (`QueryInteraction`, `CompleteInteraction`), `FMKInteractionOffer`, `MKReportMechanismNoise` (шум для AI hearing) |
| `Source/MindKore/MKInteractionComponent.h/.cpp` | `UMKInteractionComponent` — «рука» игрока; `MKCreateHandTarget`, `MKCreatePartMesh`, `MKLookedAtComponent` |

## Как использовать

- Повесить `UMKInteractionComponent` на пешку игрока (у `AOSZeroGPawn` уже есть:
  F вызывает `Interact()`, если есть предложение, иначе толкает — `PushPressed`).
  Вызывать `Interact()` по кнопке. `CurrentOffer` / `Candidate` — что показывать в HUD.
- Свой интерактивный актор: наследовать `IMKInteractable` (только C++,
  интерфейс помечен `CannotImplementInterfaceInBlueprint`), вернуть
  `FMKInteractionOffer::Available(Text)` / `Refused(Text, Reason)` / `None()`.
  `QueryInteraction` вызывается каждый кадр — должен быть дешёвым.
- Предмет должен **блокировать канал Visibility**: луч взгляда идёт по
  Visibility от точки обзора контроллера длиной `ReachCm` (220), с запасом
  `LookSlackCm` (6 см, сфера) и удержанием `HoldSeconds` (0.25 с).
- Мелкий предмет без коллизии не найдётся: нужна простая коллизия меша
  (сложная трасса выключена).

## Зависимости и конфиг

Модули Engine, AIModule (`UAISense_Hearing`). Конфиг не нужен.

## Ограничения и что вырезано

- Вырезан `UMKDeviceMemory` (журнал бодикама: «попробовал — отказ», «уже
  делал — не показывать глагол») и include `MKDeviceHUD.h`
  (`MKInteractionComponent.cpp:9, 116, 121`). Флаг `bAlwaysSpelled` в offer
  остался, но в стартере его никто не читает.
- Только мгновенное действие: удержания (тянуть рычаг) нет.

## Лицензия

Код MindKore. Контента нет.
