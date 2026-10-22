# Разработка, сборка и инструменты

## Открыть и собрать

1. Установить UE 5.8, Visual Studio с C++ и [Git LFS](https://git-lfs.com).
   Клонировать репозиторий и выполнить в PowerShell `.\Scripts\setup_git.ps1`
   (что он настраивает — в разделе [Git LFS](#git-lfs)). Без него LFS выдаёт
   блокируемые `.uasset`/`.umap` только для чтения: редактор их не сохранит,
   а скрипты упадут на `jam_build.save`.
2. Открыть `MKJamStarter.uproject`. Редактор предложит собрать модуль —
   согласиться. Можно собрать и из PowerShell:

   ```powershell
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MKJamStarterEditor Win64 Development -Project="<путь>\MKJamStarter.uproject" -WaitMutex
   ```

Карты `Lvl_Jam_ZeroG`, `Lvl_Jam_Range` и `Lvl_Jam_TestRoom` собираются с нуля
скриптами `Scripts/build_zerog.py`, `build_range.py` и `build_test_room.py`:

```
UnrealEditor-Cmd.exe <путь>\MKJamStarter.uproject -ExecutePythonScript="<путь>/Scripts/build_zerog.py" -unattended -nop4 -nosplash -nullrhi
```

`Lvl_Jam_Ship` с 18.10 правится руками в редакторе. `Scripts/build_ship.py`
пересоздаёт карту с нуля и сотрёт эту работу — не запускать. Добавлять в неё
можно только скриптами, которые трогают лишь свои акторы (как
`Scripts/jam_flyers.py`). Перед правкой карты — `git pull`: `.umap` двоичный и не
сливается.

Материалы плоти и живой стены скрипты карт берут готовыми. Граф пересобирается
только с флагом `rebuild=True` (`jam_flesh.materials`, `jam_living_wall.material`)
в отдельном запуске редактора: опустошать граф загруженного мастера нельзя
(assert `!IsRooted`).

Пробы и кадры — [Probes](Probes.md).

Арт планетоида и живой стены делают скрипты Blender (`Scripts/blender/*.py`,
`blender.exe -b --python <скрипт> -- <папка для FBX>`), FBX импортирует
`Scripts/jam_import_meshes.py`; в репозитории только `.uasset`.

Модуль называется `MindKore`: так перенесённый код работает без правок.
Новый код — файлы `Jam*.h/.cpp` в `Source/MindKore`.

## Как собрать сдачу

Win64 Development: в ней работают консоль `jam.*` и линии `DrawDebug` (см.
пробелы). Из PowerShell, при закрытом редакторе этого checkout, после сборки
редактора из раздела «Открыть и собрать» (cook работает через него):

```powershell
$p = "<путь>\MKJamStarter.uproject"
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MKJamStarter Win64 Development "-Project=$p" -WaitMutex -NoHotReloadFromIDE
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$p" -noP4 -platform=Win64 -clientconfig=Development -skipbuild -nocompileeditor -cook -stage -pak -nodebuginfo -map=Lvl_Jam_Ship+Lvl_Jam_Range+Lvl_Jam_ZeroG+Lvl_Jam_TestRoom "-stagingdirectory=<папка вне репозитория>" -unattended -utf8output
```

Сдаётся папка `<папка>\Windows` целиком, запуск — `MKJamStarter.exe`
(стартовая карта `Lvl_Jam_Ship`). Упаковку в репозиторий не класть.

Замер 19.10 на машине разработки: `Build.bat` — ~55 с, `BuildCookRun` — 27 с
(полный cook 1135 пакетов за 9 с: шейдеры уже в DDC). Первый cook на этой машине,
с компиляцией шейдеров, шёл 1 мин 32 с; на машине без DDC дольше. Размер —
694 МБ (exe 324 МБ, контент 258 МБ); без `-nodebuginfo` ещё 374 МБ PDB.
Shipping — ~500 МБ без PDB.

Проверено в упакованной Development: все четыре карты грузятся без
`SkipPackage` и `Failed to find object`; `jam.Ship.Probe` и `jam.Hull.Probe`
проходят те же шаги, что в редакторе; проектор скафандра (light function)
рисует надписи; объёмный дым (Heterogeneous Volumes) рисуется.

Известные пробелы:

- **Shipping** стартует и 60 с работает без падения (что карта загружена, видно
  лишь косвенно, по памяти процесса), но лога в нём нет, а
  кадры `jam.Hull.Stills` через `-ExecCmds` не появились: пробы в Shipping не
  проверить. В Shipping нет `DrawDebug*` (`UE_ENABLE_DEBUG_DRAWING`): пропадают
  трос сцепки, линия крюка и просчёт траектории по Shift. Поэтому сдавать
  Development, пока эти линии не сделаны мешами (как линии стяжки).
- Материалы, которые C++ грузит по мягкому пути (`TSoftObjectPtr`, `LoadObject`),
  попадают в cook только через `DirectoriesToAlwaysCook` в
  `Config/DefaultGame.ini`. Новый такой путь — добавить туда его папку, иначе в
  упаковке `SkipPackage ... does not exist`, хотя в редакторе всё работает.
  Ассеты из `ConstructorHelpers::FObjectFinder` записи не требуют.
- `jam.Flyer.Probe <Habitat>` в упаковке находит стаю только по имени актора:
  метки (label) есть лишь в редакторе.

## MCP-плагины

Для работы ИИ-ассистента с редактором (Claude Code, Codex, Cursor) нужны два
Editor-плагина. В git их нет (обе папки в `.gitignore`; другие плагины в `Plugins/` git видит), в `.uproject` они
помечены `Optional`: без них проект открывается и собирается как обычно.
Каждый ставит их сам, ровно этих версий:

1. **UE_MCP_Bridge** из npm-пакета `ue-mcp`, версия `1.4.8-beta.2`. Нужен
   Node.js. Из корня репозитория, при закрытом редакторе:

   ```powershell
   npx -y ue-mcp@1.4.8-beta.2 deploy MKJamStarter.uproject
   ```

2. **VibeUE**, снимок `dc051be7c49b08e613ca7a2196fe9f39c9b52119` из
   [kevinpbuckley/VibeUE](https://github.com/kevinpbuckley/VibeUE):

   ```powershell
   git clone https://github.com/kevinpbuckley/VibeUE.git Plugins/VibeUE
   git -C Plugins/VibeUE checkout dc051be7c49b08e613ca7a2196fe9f39c9b52119
   ```

3. Собрать проект (команда из раздела «Открыть и собрать») и открыть редактор.

Настройка клиента уже в репозитории:

- `.mcp.json` регистрирует `ue-mcp@1.4.8-beta.2` и `unreal-mcp` —
  встроенный MCP-сервер редактора `http://127.0.0.1:8000/mcp`, на нём
  работают инструменты VibeUE. Порт 8000 один на машину: два открытых
  редактора с MCP его не поделят, ответит только один из них.
- Путь к проекту в `.mcp.json` относительный, поэтому клиент запускать из
  корня репозитория. Иначе — свой абсолютный путь к `.uproject` в локальной
  настройке клиента (для Claude Code: `claude mcp add --scope local ue-mcp -- npx -y
  ue-mcp@1.4.8-beta.2 <путь>/MKJamStarter.uproject`).
- `ue-mcp.yml` — общая настройка сервера: `server.mode: in-process`, сервер
  живёт вместе с клиентом и не подключается к чужому редактору. Личные
  правки — в `ue-mcp.local.yml`, он не коммитится.

Обновление: поменять версию в команде `deploy`, в `.mcp.json` и здесь, снова
выполнить `deploy` и пересобрать.

## Git LFS

- Все двоичные файлы (`.uasset`, `.umap`, `.fbx`, `.wav`, `.png` и т. д.)
  идут через LFS, список типов — в `.gitattributes`. Новый арт кладётся в
  `Content/JamArt/`. Новый двоичный тип сначала добавить в `.gitattributes`.
- `.uasset` и `.umap` блокируемые. Перед правкой —
  `git lfs lock <файл>`, после push — `git lfs unlock <файл>`. Кто что держит:
  `git lfs locks`. Push с правкой чужого заблокированного файла отклоняется.
- Квота LFS общая на аккаунт и почти занята, поэтому текстуры не больше 2K,
  исходники (`.blend`, `.psd`, `.spp` и т. п.) в репозиторий не класть:
  они в `.gitignore`. Исходники лежат в `ArtSource/` (папка целиком в `.gitignore`), в репо идёт
  экспорт (`.fbx`, `.png`).
- Настройка клона — `Scripts/setup_git.ps1`, один раз на клон (настройки
  общие для всех его worktree): `git lfs install` (фильтры и pre-push хук,
  без хука push уходит без LFS-объектов), `lfs.setlockablereadonly false`,
  `lfs.locksverify true`, `git lfs pull` и проверка. В `.lfsconfig` эти
  ключи не задать: LFS их там игнорирует.
- Проверка `Scripts/check_lfs.sh` (Git Bash) и её копия в CI
  (`.github/workflows/lfs.yml`, на каждый push и PR) падают, если файл из
  `.gitattributes` лёг в обычный git, двоичный файл без правила LFS или
  закоммичен исходник. Исправление для первого случая:
  `git rm --cached <файл>` и снова `git add <файл>`.
- Облачные сессии Claude не достают до сервера LFS (`lfs.github.com`
  закрыт): они видят только указатели и пушат только код. Бинарники
  коммитятся и пушатся с ПК.
