# Рысь (RMB-93): арт и табло — работа GPT 21.10

Модель Рыси, её текстуры и газоразрядное табло патронов сделал GPT 20–21.10 в
локальной копии владельца (`Documents\MKJamStarter`), без коммитов. Эта страница
говорит, что из этого есть, где лежит и как пересобрать. Код ствола и его
анимации — [RecoilWeapons](RecoilWeapons.md); табло как система —
[Nixie](Nixie.md); подбор на тестовой карте — [ShipMode](ShipMode.md).

## Что сделано

- **Модель v02** по принятому короткому концепту «Рыси» (пропорции
  художественные, не техническая копия): Body, Action (цевьё с нижним стволом и
  перфорированным кожухом, ход вперёд 5.5 см), Stock, LoadingLid, Counter;
  11 846 треугольников, 47 редактируемых деталей. Собрана скриптами Blender из
  `ArtSource/Weapons/RMB93/v02/source/` (`build_model.py` и проходы
  `*_geometry.py`, `production_finish.py`, запекание `bake_export.py`).
- **Текстуры:** Body BC / N / ORM 4096², Masks 1024²; Counter 512²; атлас цифр
  1024². N — DirectX. Исходные фактуры металла и композита сгенерированы
  (`image_gen`), это не PBR-сканы.
- **В проекте:** `Content/JamArt/Weapons/RMB93` — `SM_Jam_RMB93_ViewBody`
  (табло в слоте материала 1), `SM_Jam_RMB93_Action`, `SM_Jam_RMB93_World`,
  `M_Jam_RMB93_Body` и три текстуры. Импорт —
  [`Scripts/jam_rmb93_assets.py`](../Scripts/jam_rmb93_assets.py) (берёт FBX и
  PNG из `ArtSource/Weapons/RMB93/v02/exports` и `textures`).
- **Табло Nixie:** общий материал `M_Jam_Nixie`, `MI_Jam_Nixie_RMB93`, пример
  `BP_Jam_NixieDisplay`, компонент `UJamNixieCounterComponent`. Шейдер — HLSL
  `ArtSource/Instruments/Nixie/UE_Nixie.usf`, материал собирает
  `build_ue_nixie.py`, атлас цифр — `build_sdf.py`. Подробно — [Nixie](Nixie.md).
- **Брифы и ревью:** лист моделлера и контракт ассета v01
  ([Art/RMB93/v01](Art/RMB93/v01/README.md)), арт-ревью и ревью табло v02
  (`Docs/Art/RMB93/v02/*.txt`). Последняя оценка модели — 8.0/10, табло v4 —
  8.1/10; это оценки GPT, не принятие владельцем.
- **Просмотрщик** в браузере: `ArtSource/Weapons/RMB93/v02/viewer` (three.js,
  `serve.py`, только 127.0.0.1) — орбита, ADS, цифры, ход помпы, позы приклада
  и крышки. Как и всё в `ArtSource/`, только на ПК владельца.

## Что в git, а что нет

`ArtSource/` целиком в `.gitignore` ([Development](Development.md), «Git LFS»):
скрипты Blender, HLSL табло и его сборщики, `.blend`, FBX, PNG-текстуры,
рендеры, просмотрщик и проверки GPT лежат только на ПК владельца в
`Documents\MKJamStarter\ArtSource`. В git — то, что нужно игре и чтению: ассеты
в `Content/`, импорт `Scripts/jam_rmb93_assets.py` и `jam_nixie_test_room.py`,
код компонента, эта страница, [Nixie](Nixie.md) и брифы с ревью в
[Docs/Art/RMB93](Art/RMB93/v01/README.md) (только текст; листы моделлера в PDF,
рендеры и сгенерированные картинки — там же на ПК). Ссылки на `ArtSource/` в
этих страницах ведут в ту папку. Без неё ассеты из `Content/` работают, но
пересобрать модель, текстуры или материал табло можно только на том ПК.

Шейдер табло v5 (стопка из 10 катодов, переходы по событию) записан поверх
работы GPT в той же копии и в UE ещё не собран: в `Content` лежит материал v4,
а код компонента уже пишет метки v5 и без них работает как v4 (см. [Nixie](Nixie.md)).

## Пересобрать

1. Модель и текстуры (Blender, на ПК с `ArtSource`): скрипты
   `ArtSource/Weapons/RMB93/v02/source/` перезаписывают только свои результаты
   v02 — ручную доработку в `.blend` сначала скопировать. Порядок и проверки —
   README модели `ArtSource/Weapons/RMB93/v02/README.md`.
2. Импорт в UE: `jam_rmb93_assets.build()` в редакторе. Уже импортированные
   ассеты он не трогает, для повторного импорта их сначала удалить.
3. Табло: `ArtSource/Instruments/Nixie/build_sdf.py`, затем
   `build_ue_nixie.py` в редакторе; проверка — `jam.Nixie.Probe`.
4. Тестовая карта: `Scripts/build_test_room.py` (вызывает
   `jam_nixie_test_room.py`); проверка подбора — `jam.Nixie.WeaponProbe`
   ([Probes](Probes.md)).
