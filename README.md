# MKJamStarter

Стартовый проект Unreal Engine 5.8 (C++) для геймджема. Системы взяты из
MindKore: взаимодействие «посмотрел — нажал E», двери с ключами и засовами,
рубильники, роллеты и стеллаж на рельсах, переноска и бросок физических
предметов, вскрытие досок ломом, бьющееся стекло, единый путь попаданий с
зонами и здоровьем, замедление времени. Тема джема сюда не зашита: это
нейтральная база.

## Открыть и собрать

1. Нужен UE 5.8 (`C:\Program Files\Epic Games\UE_5.8`) и Visual Studio с C++.
2. Собрать модуль редактора (из PowerShell):

   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MKJamStarterEditor Win64 Development -Project="<путь>\MKJamStarter.uproject" -WaitMutex
   ```

   Или ПКМ по `MKJamStarter.uproject` → Generate Visual Studio project files →
   собрать `MKJamStarterEditor` в Development Editor.
3. Открыть `MKJamStarter.uproject`. Стартовая карта —
   `/Game/Jam/Maps/Lvl_Jam_ZeroG` (невесомость, режим `AJamZeroGGameMode`
   задан в её WorldSettings). Наземная тестовая комната —
   `/Game/Jam/Maps/Lvl_Jam_TestRoom` (режим по умолчанию `AJamGameMode`).

Модуль называется `MindKore` (папка `Source/MindKore`), чтобы перенесённый код,
`MINDKORE_API`, `LogMindKore` и пути `/Game/MindKore/...` работали без правок.
Плагины: EnhancedInput, Niagara, PythonScriptPlugin и EditorScriptingUtilities
(только редактор, для скрипта карты). ProceduralMeshComponent включён в движке
по умолчанию.

## Управление

Невесомость (`Lvl_Jam_ZeroG`): мышь — обзор, ПКМ — оттолкнуться, ЛКМ —
выстрел с отдачей / бросок с отдачей (обе срабатывают на отпускании, пока
зажаты — рисуется путь), E — схватить / отпустить, T — трос, F — отпустить,
WASD и Space/Ctrl — слабые двигатели, X — стабилизатор, Q — замедление, G — лом.
Подробно — [ZeroG](Docs/ZeroG.md).

Наземная комната (`Lvl_Jam_TestRoom`):

| Клавиша | Действие |
|---|---|
| WASD, мышь | ходьба, обзор |
| Space | прыжок |
| C / Left Ctrl (держать) | присесть; нажатый в воздухе — поджать ноги (crouch-jump) |
| E | использовать то, что под прицелом (подсказка под точкой прицела) |
| ЛКМ | выстрел (тестовый хитскан); с предметом в руках — бросок |
| ПКМ / F | положить предмет |
| Q (держать) | замедление времени (3 с запаса, без перезарядки) |
| G | дать/забрать «лом» (тег `MK.Crowbar`) — нужен для вскрытия досок |

## Тестовая комната

`Lvl_Jam_TestRoom` (16 × 12 м, потолок 3 м, тёмная) собирается скриптом
`Scripts/build_test_room.py` — карту можно пересобрать с нуля:

```
UnrealEditor-Cmd.exe <путь>\MKJamStarter.uproject -ExecutePythonScript="<путь>/Scripts/build_test_room.py" -unattended -nop4 -nosplash -nullrhi
```

(после скрипта редактор остаётся открытым — закрыть его или вызвать
`unreal.SystemLibrary.quit_editor()`; вариант `-run=pythonscript` не
подходит: в коммандлете `EditorActorSubsystem` не спавнит акторов).

Перегородка на Y = 800 отделяет зал (старт игрока) от заднего коридора.
Станции подписаны табличками:

1. **DOOR + KEY** — дверь `AMKDoor` под ключ `jam_key`; ключ `AMKKeyPickup` на подставке.
2. **SWITCH + GATE** — рубильник `AMKControlSwitch` открывает/закрывает роллету `AMKGate`.
3. **SWITCH + SHELF** — второй рубильник двигает `AMKShelfBlock` из двух стеллажей, открывая проход.
4. **CARRY** — бочка 25 кг, доска 6 кг, ящик 15 кг, блок 60 кг (не поднять: «слишком тяжело»), уступ 130 см (бочка + прыжок с поджатием).
5. **PRY** — доски `AMKFastener` на лазе 100 × 110 см; без тега `MK.Crowbar` — «нечем вскрыть», G даёт тег.
6. **GLASS** — `AMKGlassPane` 150 × 140 в проёме перегородки; бьётся выстрелом.
7. **SHOOT** — `AMKImpactDummy` с зонами (голова, торс, бронепластина, руки, ноги).
8. **APPLIANCES** — `BP_MK_Microwave`, `BP_MK_Toilet` (дети `AMKAppliance`).

## Документация модулей

[Interaction](Docs/Interaction.md) ·
[Doors-Keys-Locks](Docs/Doors-Keys-Locks.md) ·
[Switches-Gates-Shelf](Docs/Switches-Gates-Shelf.md) ·
[Carry-Props](Docs/Carry-Props.md) ·
[Fastener](Docs/Fastener.md) ·
[Glass](Docs/Glass.md) ·
[Impacts-Vitals](Docs/Impacts-Vitals.md) ·
[BulletTime](Docs/BulletTime.md) ·
[JamCharacter](Docs/JamCharacter.md) ·
[ZeroG](Docs/ZeroG.md) ·
[Tether-Prediction](Docs/Tether-Prediction.md) ·
[DockZone](Docs/DockZone.md) ·
[CREDITS](CREDITS.md)

## Тяжёлый арт и Git LFS

Все двоичные файлы проекта (`.uasset`, `.umap`, `.fbx`, `.wav`, `.png` и т. д.,
полный список в `.gitattributes`) идут через Git LFS; код, конфиг и
документация — обычным git. Арт команды кладётся в `Content/JamArt/` (пути
пакетов `/Game/JamArt/`). Ассеты, закоммиченные до перехода на LFS, остались в
истории обычного git; их текущие версии уже в LFS.

**Один раз на машине.** Установить [Git LFS](https://git-lfs.com) (идёт с
Git for Windows) и выполнить `git lfs install`. После этого `git clone` и
`git pull` сами скачивают файлы LFS.

**Блокировки.** Все `.uasset` и `.umap` помечены `lockable`: после checkout
они только для чтения, пока их не заблокируешь.
Двоичный ассет нельзя слить, поэтому правит его один человек:

```
git lfs lock "Content/JamArt/Ship/SM_Ship_Hull.uasset"
# правка в редакторе, сохранение, commit, push
git lfs unlock "Content/JamArt/Ship/SM_Ship_Hull.uasset"
git lfs locks            # кто что держит
```

Если редактор пишет, что файл только для чтения, значит его не заблокировали.
На машине, где скрипты сами пересобирают карты, режим «только для чтения»
можно выключить для этого клона: `git config lfs.setlockablereadonly false`
(блокировки при этом остаются — их надо брать руками).
Можно поставить в UE плагин «Git LFS 2» (Project Settings → Source Control),
он блокирует сам при checkout ассета.

**Квота.** LFS на GitHub считается на аккаунт владельца репозитория: 10 ГиБ
хранения и трафик, который тратит каждый clone и pull. Аккаунт уже занят
другим проектом примерно на 8,7 ГиБ, поэтому:
- текстуры не больше 2K (лучше 1K), без 4K и несжатых исходников;
- исходники (`.blend`, `.psd`, `.spp`) в репозиторий не класть, только
  импортированные `.uasset` и при необходимости `.fbx`;
- перед push смотреть `git lfs ls-files -s`.

## Что не перенесено

Оружие и руки MindKore, враги и ИИ, реакция тела (ragdoll), камера бодикама и
её HUD, журнал устройства, Mover/ChaosMover, PhysicsControl, ветер. Никаких
сторонних паков (Fab, Bodycam, Footsteps и т. п.): эффект и звук разбития
стекла пока пустые (см. [Glass](Docs/Glass.md)).
