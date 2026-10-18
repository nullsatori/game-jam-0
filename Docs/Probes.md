# Пробы и стенды

Все проверки игры — консольные команды `jam.*` в C++ (`Source/MindKore/Jam*.cpp`).
Они ведут пешку сами и пишут результат в `LogMindKore`. Описание каждой команды —
строка помощи рядом с её регистрацией в коде.

## Как запускать

Без окна (только логика, NullRHI):

```powershell
$a = '"<путь>/MKJamStarter.uproject" /Game/Jam/Maps/Lvl_Jam_ZeroG -game -nullrhi -nosound -unattended -nop4 -nosplash -log=Probe.log -ExecCmds="jam.ZeroG.Probe"'
Start-Process -WindowStyle Hidden "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -ArgumentList $a
```

Через 40–60 с процесс останавливают, результат — строки `LogMindKore` в
`Saved/Logs/Probe.log`.

- Запускать из PowerShell, а не из Git Bash: MSYS переписывает пути
  `/Game/...` в `C:/Program Files/Git/Game/...`, и движок показывает модальный
  диалог.
- `-unattended` обязателен: без него ошибка открывает окно.
- Кадры (`Shot`, `HighResShot`) требуют настоящего рендера: вместо `-nullrhi` —
  `-RenderOffscreen -ResX=1280 -ResY=720`, исполняемый файл `UnrealEditor.exe`.
  Кадры ложатся в `Saved/Screenshots/WindowsEditor/`.
- Запись звука в фоновом окне — тишина. Нужен ключ
  `-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0`.
- NullRHI — доказательство логики, не вида: тайминги рендера и кадры из него
  ничего не значат.

## Логика (NullRHI)

| Команда | Карта | Что проверяет |
|---|---|---|
| `jam.ZeroG.Probe` | `Lvl_Jam_ZeroG` | толчок, отдача пистолета и СВ-98, сцепка с грузом 800 кг, трос, стекло. Эталон: 340 / 1040 см/с, остаток взгляда 2.76° |
| `jam.ZeroG.Grapple` | `Lvl_Jam_ZeroG` | крюк: за неподвижный блок, за лёгкий предмет, крен 45° |
| `jam.Guns.RmbProbe` | `Lvl_Jam_ZeroG` | правило ПКМ: прицел со стволом, захват → «без оружия», руки без оружия, ствол во время захвата |
| `jam.Weapons.SwitchProbe` | `Lvl_Jam_ZeroG` | смена оружия с передумыванием, убрать/достать, выстрел во время доставания отклонён, после — проходит |
| `jam.Weapons.PoseTrace` | `Lvl_Jam_ZeroG` | пружины позы и кривые деталей каждые 10 мс через цикл и перезарядку |
| `jam.Weapons.MagTrace` | `Lvl_Jam_ZeroG` | поза магазинов каждые 1/60 с через перезарядку |
| `jam.Wall.Probe [Name]` | `Lvl_Jam_ZeroG`, `Lvl_Jam_Range` | живая стена: пистолет не открывает, дробовик открывает проход, время закрытия, пролёт пешки |
| `jam.Wall.Seal` | любая со стенами | целые стены не пропускают ни пешку, ни сферу 15 см |
| `jam.Flyer.Probe [all] [Habitat]` | `Lvl_Jam_ZeroG`, `Lvl_Jam_Ship` (`Planetoid`) | летун: блуждание, зрение, аггро, укусы, деаггро, смерть, труп |
| `jam.Ship.Probe` | `Lvl_Jam_Ship` | каюта, скафандр, шлюз, невесомость, планетоид, лист обшивки, детали |
| `jam.Hull.Probe [Index]` | `Lvl_Jam_Ship` | ремонт обшивки: стяжка, мусор, сварка, прихватка, шов, резка, держаться, смерть и возрождение |
| `jam.Ship.Caves [Crater] [Plug]` | `Lvl_Jam_Ship` | мембрана кратера и пробка пещеры под дробовиком |
| `jam.Ship.Lap`, `jam.Ship.JetExit`, `jam.Ship.Land` | `Lvl_Jam_Ship` | ходьба вокруг планетоида, реактивный выход из его гравитации, посадка |
| `jam.Ship.Where [Tag]` | любая | где акторы с тегом (по умолчанию `Junk.Foreign`) |

## Кадры и звук (нужен рендер)

| Команда | Что снимает |
|---|---|
| `jam.Weapons.Shots`, `jam.Weapons.Cycles`, `jam.Weapons.Reloads`, `jam.Weapons.Frames` | стволы: покой, выстрел, механизмы, перезарядки, кадры через 33 мс |
| `jam.Weapons.MagFrames` | сброс и вставка магазинов USP и СВ-98 на 60 к/с |
| `jam.Weapons.SwitchFrames` | убрать и достать каждый ствол, кадры через 33 мс |
| `jam.Weapons.Fx [space]` | вспышка покадрово и дым за 3 с |
| `jam.Weapons.Smoke [space]` | объёмный дым: облако, второй выстрел сквозь него, пролёт; затем `ProfileGPU` |
| `jam.Scope.Shots`, `jam.Scope.AimFrames`, `jam.Scope.AimStills` | оптика СВ-98, подъём к глазу всех стволов, подбор глаза и поля зрения |
| `jam.Wall.Shots [Name]` | живая стена до и после дробовика |
| `jam.Flyer.Shots`, `jam.Flyer.BiteShot`, `jam.Flyer.CorpseShot` | летун, кровь на визоре, труп на планетоиде |
| `jam.Ship.Shots`, `jam.Ship.PlanetShots`, `jam.Ship.CaveShots` | корабль, планетоид, кратеры и пещеры |
| `jam.Flyer.Clip [Seconds]` | ролик атаки со звуком (видео + мастер), `Saved/Clips` |
| `jam.Flyer.MixCheck` | выстрел и попадание по летуну отдельно, для сравнения громкости |
| `jam.Audio.LimiterProbe [Gain]` | цепочка мастер-сабмикса и запись выхода под очередь выстрелов |

Позу можно заморозить для кадров: `jam.Weapons.PoseAt`, `ReloadPoseAt`,
`StowPoseAt`, `DrawPoseAt` — секунды от начала фазы, `-1` — живая игра.
`jam.Scope.Trace 1` пишет пружины прицела каждый тик
(`Scripts/plot_scope_aim.py` строит графики).

## Какие пробы гонять

Только те, которые правка может сломать: смена оружия — `SwitchProbe` и
`RmbProbe`; отдача и движение — `ZeroG.Probe`; стены — `Wall.Probe`; корабль и
обшивка — `Ship.Probe` и `Hull.Probe`. Для правок только арта хватит кадров.
Шаг «сварено снова» в `jam.Hull.Probe` плавает от прогона к прогону: стяжка
иногда прижимает лист неровно (Z 20.13 вместо 20).
