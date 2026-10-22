# Fastener (вскрытие ломом)

## Что получает игрок

Доски поперёк проёма, решётка, крышка. Без лома — «Вскрыть: нечем вскрыть».
С ломом — F, и крепление исчезает (или крышка отваливается на пол), проём
свободен. Замок двери ломом не открывается — только крепления.

## Классы и файлы

`MKFastener.h/.cpp` — `AMKFastener`, `MKResolveFastenerOffer`,
`MKFastenerCrowbarTag()`.

## Как поставить

- `Blocker` — корневой меш (например `SM_MK_StoreBoards_Intact` 146 × 3 × 115;
  его габарит по Z начинается с 46 см — выравнивать по bounds). Блокирует всё
  и навмеш до вскрытия.
- `bBlockerFalls` — крышка падает физикой (с `LandSound`), иначе исчезает.
- `SpawnOnPry` (класс-наследник `AMKPhysicsProp`) + `SpawnTransforms` — что
  выпадает (доски). В тестовой комнате не задано: нужен Blueprint-наследник
  `AMKPhysicsProp` с мешем доски.
- `OnPried` — событие для того, что крепление закрывало.

## Лом = тег

У MindKore «лом в руках» — слот рук. Здесь это **тег актора `MK.Crowbar`**
на том, кто взаимодействует (`MKFastenerCrowbarTag()`). `AOSZeroGPawn`
переключает его клавишей G (`ToggleCrowbar`, отладочный тумблер — подбора
лома нет); в игре его надо выдавать при подборе лома
(`Tags.AddUnique(MKFastenerCrowbarTag())`).

## Зависимости и конфиг

Engine, AIModule (шум). Тег `MK.Crowbar`.

## Ограничения и что вырезано

- `MKResolveFastenerOffer(bPried, bHasHands, Slot, bHasCrowbar)` →
  `MKResolveFastenerOffer(bPried, bHasCrowbar)` (`MKFastener.h:5, 40`).
  Отказы `RefuseHandsBusy` и `RefuseToolNotInHand` остались в enum, но правило
  их больше не возвращает; проверка «лом ещё не поднят» (`IsToolReady`) тоже
  ушла (`MKFastener.cpp:30`). `MKHandsBusyText()` заменён локальным текстом
  (`MKFastener.cpp:47`).
- Можно вскрывать с предметом в руках.

## Лицензия

`SM_MK_StoreBoards_Intact` — собственный арт MindKore.
