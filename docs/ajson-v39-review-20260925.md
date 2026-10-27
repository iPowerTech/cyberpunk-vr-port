# Разбор Ajson V39 для сюжетного прохождения

Проверено 25 сентября 2026. Это обзор исходников и истории, без переноса кода,
запуска чужого установщика или изменения установленного мода.

## Какая именно версия проверена

- Папка: `C:/Users/dariulone/Downloads/cyberpunk-vr-port-ajson-v39`.
- Источник ZIP из Zone.Identifier: `Ajson44/cyberpunk-vr-port`, ветка `ajson-v39`.
- Коммит ZIP и HEAD ветки: `78246284e54f5e2c053285627521eff799ce38ff`.
- Основа: upstream 0.1.6, `b4a74461b8f7964ad5231113075da44890117523`.
- Между ними 114 коммитов, затронуто 96 путей. Прочитаны все сообщения коммитов
  и списки файлов; подробно проверены конечные реализации взгляда, сюжетного
  ввода, взаимодействия с уликой и переходов remote-camera.
- Проверенные 622 файла ZIP вне `externals/` побайтно совпали с Git blobs этого
  коммита. Сравнивал именно скачанную версию, а не другой default branch.
- Наша база сравнения: `6860f525` плюс текущие незакоммиченные yaw-takeover,
  Bella VRCAM и identity-HUD изменения. Они не заменялись.

История и точные сообщения сохранены в `build/ajson-v39-review-20260925/commits.json`;
полный список с оценкой — ниже. Bare-копия репозитория и диагностические материалы
лежат в той же папке. Источник: https://github.com/Ajson44/cyberpunk-vr-port/tree/ajson-v39

## Главное для наших квестов

### 1. Проверка взгляда: полезный механизм, но не универсальный фикс

Файл `src/Hooks/LookAtRayFromHand.cpp` меняет Transform, который передаётся
`TargetingSystem::UpdateLookAtRay`. Именно этот игровой луч участвует в выборе
объекта/взаимодействия и поддерживаемых автором look-at проверках. Это отдельный
потребитель направления взгляда: корректная VR-картинка сама по себе не означает,
что этот запрос направлен туда же.

Изначально хук переносил луч на руку. Последующие фиксы возвращают его голове
в конкретных контекстах. В нашей ветке этого хука и пакета `CyberpunkVRPort_Interact`
нет, поэтому часть его исправлений устраняет последствия его собственного
hand-targeting. Нельзя автоматически считать все описанные им сбои нашими.

Итоговые правила V39, в порядке выполнения:

| Контекст | Что делает V39 |
|---|---|
| Нет опубликованного view или исходный луч дальше 1.5 м от него | Оставляет нативный луч |
| `q110_02_camera_scan_start > 0` и `q110_02_scanning_done <= 0` | HMD-позиция и ориентация; исключение действует даже с выключенным hand interaction |
| Hand interaction выключен, предыдущего исключения нет | Оставляет нативный луч |
| Пешая авторская сцена tier≥2, locomotion≠9 | Сохраняет полностью нативный луч, включая ориентацию |
| Braindance либо locomotion9 с tier≥2/workspot, без активного passenger interaction | Использует HMD view; эта ветка находится после предыдущего правила |
| Сканер/remote device, если не сработали предыдущие правила | Использует HMD view |
| Обычное взаимодействие | Выбирает руку и строит луч от ладони |

Конкретное исключение для камеры в Пасифике добавлено в
[`a7ddea59`](https://github.com/Ajson44/cyberpunk-vr-port/commit/a7ddea59e7b0761436aa5aa0fd62329bf57de076).
Скрипт **читает** два quest fact, но не записывает их. Native bridge выдаёт
разрешение на 750 мс; без обновления оно истекает. Квест завершает обычная игровая
проверка взгляда, а не запись «задание выполнено».

Важна история отменённого решения: `d83b566e` перенаправлял все пешие Tier2+
сцены на HMD, `8572c188` оставлял нативное начало луча, меняя направление.
Оба варианта оказались недостаточны. В `28a73447` автор вернул нативное внимание
для таких сцен: широкий override не исправил книгу и заблокировал подход/взгляд
у двери квартиры Джуди. Для Judy/workspot сохранился более узкий путь locomotion9.

**Что взять:** отдельный контекстный перехват квестового внимания и ограниченное
исключение q110. Существующие условия квеста должны продолжать проверяться игрой.
Не переносить весь hand-targeting ради одной проверки и не включать HMD для всех
катсцен одной проверкой `tier >= 2`.

**Что адаптировать перед переносом:**

- Фильтровать именно targeting user игрока. В V39 аргумент `userState` не
  используется, а принадлежность определяется расстоянием 1.5 м; это не доказательство
  идентичности игрока и потенциально затрагивает близкого NPC. Это риск по коду,
  не воспроизведённый в нашей игре дефект.
- Брать свежую согласованную пару origin/orientation независимо от решения VRIK.
  У нас `g_VRViewWorldPos/Rot` публикуются внутри `AnimPose.cpp`, а при cinematic
  suspend или `DeviceCamActive()` он возвращается раньше. Копирование зависимости
  V39 от этих полей может дать старый луч именно в сюжетной сцене.
- Наш `ReadMainAimPose` — полезная основа для обычного/внешнего MAIN, однако его
  текущая публикация исключает takeover и BD playback. Для них нужен их собственный
  свежий Locate/lens источник, а не ослабление проверки свежести.
- Сохранить текущие переключения клавиш, HMD origin/reset и debug gate.

Read-only проверка нашего `Cyberpunk2077.exe` нашла ровно одно совпадение его
23-байтной сигнатуры: RVA`0x3F90E9`, точка перехвата`0x3F90F7`. Это подтверждает
наличие нужного участка в установленной версии, но не заменяет проверку поведения
квестов. Ни один байт процесса/EXE при обзоре не изменялся.

### 2. Сюжетные действия сидя: высокий приоритет

`ba6e9c70`, `97efbfa6`, `0422abe0`, `01b82f4a` разделяют состояния
Passenger3 / Transition4 / Scene7, обычное вождение, passenger combat2 и
driver combat6. В 3/4/7 X сохраняется как нативное взаимодействие, включая
удержание, а перевод X→выход из машины выключается. Это важно для взять/оставить
турель и air-hypo Такэмуры.

В нашей `include/Hooks/VehicleButtons.hpp` специальная обработка есть для combat2;
прочим mounted-состояниям по-прежнему возвращается `exitOnX=true` и маска X/B.
Это конкретная разница, которую стоит исправлять и тестировать на 3/4/7, сохранив
наше уже проверенное поведение водителя и пассажирской стрельбы.

Для Delamain fork также допускает dialogue mode в ограниченных сюжетных
mounted-состояниях, даже если `choiceHubActive=0`. Навигация/подтверждение идут
через штатные клавиатурные Up/Down/F. Обычные сцены используют pad-ветку.
Его новые физические сочетания left grip + right stick + A переносить необязательно:
можно взять только выбор потребителя и контекста под нашу раскладку.

`SendInput` требует активного окна и зависит от клавиатурных привязок; это
пригодный проверенный fallback, но для нашей реализации стоит сначала оценить
существующий native action путь. У нас обычный A→X mirror при mounted сейчас
шире его конечной версии; V39 исключает водителя, чтобы A не включал лишнее действие.

### 3. Flathead и переключение удалённых камер

Полезные отдельные идеи:

- `a6191e32`: ключ Lua-кэша — EntityID, не равенство CET userdata. В нашей
  `ForceFPP/init.lua` ещё есть `devCache.obj ~= obj`; native `VRTakeoverEntity`
  уже умеет корректно отпускать lens при реальной смене ID. Стоит объединить
  существующий native release с устойчивой идентичностью Lua, не добавлять
  вторую конкурирующую систему сброса.
- `08f6e9a4` и предшествующая цепочка: ограниченный поиск компонента при takeover
  без найденной линзы, вместо постоянного возврата всех компонентов в C++ slow path.
  Рассмотреть на реальной сцене Flathead. У нас новый CameraDirector/MAIN handoff,
  поэтому старые ASM-фильтры нельзя механически вставлять поверх него.
- `35745096`: remote-device targeting тоже использует направление HMD; полезно
  для вентиляции Flathead, до которой нельзя физически дотянуться рукой.

Точный `cameraComponent` и native release по EntityID у нас уже есть; повторно
портировать эти части как новые фиксы не требуется.

### 4. Книга на офренде Джеки — отдельный state fix

`00e0aee4` пробовал `FocusClueStateChangeEvent`; документация прямо сообщает,
что этот вариант не сработал. Конечный код —
[`886aa90b`](https://github.com/Ajson44/cyberpunk-vr-port/commit/886aa90b395b75741e66823e3f9c0e333461bf6c).

Он узнаёт компонент с `q110_haitian_book_a` в имени и требует: scan complete,
progress≥1, доступная включённая улика, `inspected=false`, `autoInspect=false`
и явный grip. Метод GameObject вызывает штатный
`SetClueState(index, true, true, true, false)`. Grip потребляется до отпускания,
чтобы появившееся «Взять» не сработало тем же нажатием.

Это изменение состояния осмотра конкретной уже отсканированной улики, а не
общий gaze fix. В нашу ветку брать при подтверждении такого же симптома;
квестовые facts и выдачу предметов для этого менять не нужно. Старый markdown
ещё описывает неудачу V32, тогда как конечный redscript уже содержит следующий шаг.
Отдельного отчёта о подтверждении именно конечной версии книги я не нашёл.

### 5. AV/оружейные турели и выход из takeover

`eaf4b7d5` различает SurveillanceCamera, AV и SecurityTurret по управляемому
объекту. RT сохраняется для стрельбы; для weapon turret подавлен VR LT, потому
что у автора zoom разводил эффективные линзы глаз. Это ограничение его версии,
а не полный фикс turret zoom.

У нас AV/Basilisk camera/aim уже проходили отдельные проверки. Полезно сравнить
разделение владельцев ввода и переход назад к handheld weapon. Не переносить
выключение zoom и старые camera-offset значения без воспроизведения проблемы.

## Остальные изменения V39

| Блок | Что он сделал | Что это значит для нас |
|---|---|---|
| Hand interaction | Наведение ладонью, touch для устройств, grip для остальных объектов, выбор/фиксация руки | Самостоятельная большая функция с влиянием на квестовый targeting; переносить отдельно от gaze fix |
| Haptics | OpenXR vibration action, отдача и отклик взаимодействия | Полезная независимая опция, не фикс сюжетного прогресса |
| Physical crouch | Crouch по высоте головы с гистерезисом и блокировками | Дополнительный режим управления; сравнить с нашей текущей схемой |
| Physical quick melee | Жест оружием → штатный Q quick-melee, фильтры направления и контекста | Не то же самое, что уже имеющееся физическое melee; отдельно по желанию |
| Reload/support | Проверка близости support hand, уступка нативной перезарядке, разделение chord/reload | Можно выборочно сравнить с нашей более новой системой оружия |
| Cyberware | Оба grip+R3; конечный вариант отправляет E вместо LB+RB | Убирает конфликт shoulder combo, но раскладка и SendInput требуют адаптации |
| Grenade/quickslot | Конечный RB повторяет press/hold/release физической B; grip-jitter не обрывает уже начатое действие | Полезный принцип для удерживаемых действий; ранние 90 мс pulse не брать |
| Vehicles | Throttle latch и подавление тормозом, радио с удержанием, свет, ручник, horn у ступицы, телефон | Возможные отдельные улучшения, но не замена нашей свежей физики руля |
| Menu | R3+Menu → native pause; затем восстановлен plain Menu на splash | Наш overlay/dual-stick вход уже другой; этот chord не нужен для текущих фиксов |
| Body follow/recenter | Старые opt-in прототипы подстройки ходьбы и автоматического recenter | Наш native roomscale/CCT и hybrid rotation существенно новее; не заменять |
| Cameras/stereo | Старая стабилизация seated/Delamain, перепривязка MAIN, режимы диагностики и позы submit | Только как исторические данные; у нас новая pose transport/MAIN handoff архитектура |
| Installer | Manifest, хеши, backup/rollback, проверка game root, сохранение пользовательских файлов | Полезно для будущего дистрибутива; к прохождению непосредственно не относится |

В его ForceFPP всё ещё есть `GameplayRestriction.VehicleFPP` и checkbox
`hold the player in first person`, который мы специально убрали. Замена всего
пакета вернула бы уже устранённое ограничение сюжетных TPP-камер.

## Что реально подтверждено, а что только описано

Автор заявляет Quest 3 + Virtual Desktop OpenXR, прохождение начала игры,
Heist/Flathead/Delamain, Pacifica/GIM/I Walk the Line, A Little Help from My Friends
и следующей встречи с Такэмурой на Jig-Jig Street. Это его результаты; я их здесь
не воспроизводил. Подтверждённых DLC-тестов в изученных материалах нет.

- `Look at the stars` — успешный checkpoint в release notes, не найден отдельный
  универсальный патч под это название. Не путать описанную проверку с новым механизмом.
- `Riders on the Storm` в конце сцены с Панам/Джонни автор проходил во flatscreen;
  V39 прямо не заявляет исправление этого стопора.
- Projectile launcher: исправлен общий hold quickslot, но его aim/orientation/
  presentation по-прежнему не заявлены рабочими.
- Выход из обычного vehicle handheld combat/holster записан как расследование,
  не как готовый фикс. NPC weapon-threat reactions не реализованы.
- Blackwall/туман/часть cinematic stereo остаются в known issues.
- Многие `scripts/verify_*.ps1` проверяют наличие строк через `.Contains()` и
  неизменность файлов, иногда наличие DLL. Это не автоматические тесты прохождения
  игры и не замена проверке на сохранении.

## Предлагаемый порядок переноса

1. Контекстный квестовый targeting: q110 camera и Judy/workspot, с точной
   идентичностью игрока, свежим camera pose и сохранением native scripted attention.
2. Разделить X/exit/confirm в Passenger/Transition/Scene; Delamain dialogue fallback.
3. Стабильный EntityID в Lua-кэше takeover; воспроизвести Flathead camera switch
   и vent interaction с нашей текущей MAIN/VRCAM схемой.
4. Отдельно проверить и при необходимости перенести ручной осмотр книги.
5. Затем отдельные QoL: удержание quickslot, cyberware, haptics, quick melee.

Для первых трёх шагов нужны позитивные и отрицательные игровые проверки:
цель выполнена при правильном взгляде; взгляд в сторону не завершает цель;
уход из сцены убирает override; соседний NPC не меняет targeting; дверь Джуди,
пассажирский бой, обычный водитель, активный HUD/ImGui и переключение камеры
сохраняют свои действия. Старые camera, body-follow и input monolith не объединять
с текущими вслепую.

## Все 114 коммитов

Ниже сохранены точные названия коммитов и оценка применимости. Пометка
«сравнить» означает кандидат на адаптацию/проверку, а не готовый безопасный cherry-pick.

<!-- COMMIT_LEDGER -->

| № | Коммит | Точное название | Оценка для нашего порта |
|---|---|---|---|
| 1 | [e6ecf8d1](https://github.com/Ajson44/cyberpunk-vr-port/commit/e6ecf8d1439abf0901c17482e8f8a7e4ce8d4507) | Add interaction bridge and palm orientation support | Bridge ладоней/взаимодействия; зависимость hand-targeting, не самостоятельный gaze fix. |
| 2 | [3df00386](https://github.com/Ajson44/cyberpunk-vr-port/commit/3df003869ed6311d6cc08bcc366264826d60494b) | Port hand targeting and touch interaction | Порт hand-targeting из iPowerTech; большая новая функция, отдельно от сюжетки. |
| 3 | [49bc81fc](https://github.com/Ajson44/cyberpunk-vr-port/commit/49bc81fcd2595b9fa87f3424f78b0e420389fff9) | Add OpenXR firing and interaction haptics | Нативные OpenXR haptics; независимый QoL-кандидат. |
| 4 | [8de86256](https://github.com/Ajson44/cyberpunk-vr-port/commit/8de862561d21da1a32dec06e3c47ed54842820c1) | Add physical crouch from HMD height | Приседание по высоте HMD; опция управления, не сюжетный фикс. |
| 5 | [0d8db824](https://github.com/Ajson44/cyberpunk-vr-port/commit/0d8db82412989fb67725df4aeba7aa983e89b086) | Prepare selective build package and audit guard | Упаковка и проверка границ selective port. |
| 6 | [3cc83e50](https://github.com/Ajson44/cyberpunk-vr-port/commit/3cc83e502229625a69b3367796f95879dbf0390b) | Add conservative on-foot follow safety bridge | Сигнал безопасного состояния для старого body-follow прототипа. |
| 7 | [1fd1a0eb](https://github.com/Ajson44/cyberpunk-vr-port/commit/1fd1a0eb5418df8040eeadd63907aeede1e90e9b) | Add conservative body translation follow prototype | Прототип body translation; нашу native roomscale реализацию не заменять. |
| 8 | [f1b83745](https://github.com/Ajson44/cyberpunk-vr-port/commit/f1b8374589d4e3afcc166c34d815ee4eb2252a7d) | Add independent conservative auto recenter fallback | Автоматический recenter; не смешивать с нашей системой origin/roomscale. |
| 9 | [56b3454a](https://github.com/Ajson44/cyberpunk-vr-port/commit/56b3454ae788c22b2688ec85ee6117750b48ab0c) | Document body drift prototypes in release package | Документация прототипов перемещения/центрирования. |
| 10 | [195a6546](https://github.com/Ajson44/cyberpunk-vr-port/commit/195a654673487e961b5ebec13345b6cf88df6fc6) | Extend selective audit for body drift prototypes | Статическая проверка selective-port для этих прототипов. |
| 11 | [49da7f10](https://github.com/Ajson44/cyberpunk-vr-port/commit/49da7f102c2886ffd7013d2f8082b74b81beb82d) | Harden body follow gate against swimming and scenes | Ограничения body-follow в воде и сценах; идея гейтов уже учтена у нас иначе. |
| 12 | [50a2535d](https://github.com/Ajson44/cyberpunk-vr-port/commit/50a2535d09559300f35fa9f329917ff7fbc4325f) | Fix body follow startup gate and add diagnostics | Начальная готовность и диагностика body-follow; исторический прототип. |
| 13 | [5347deb5](https://github.com/Ajson44/cyberpunk-vr-port/commit/5347deb5b299dbbcb48bea4c75c122f42952da02) | Route body follow safety through locomotion bridge | Передача safety-состояния через locomotion bridge; сигнатуры у нас другие. |
| 14 | [efde8eb9](https://github.com/Ajson44/cyberpunk-vr-port/commit/efde8eb9eedd29d09eb4542c91ed4c4518dd9f42) | Allow body follow in free-roam scene tier one | Уточнение разрешённого Tier 1 для body-follow. |
| 15 | [0d78c783](https://github.com/Ajson44/cyberpunk-vr-port/commit/0d78c783fb3fe4b5d340641531635e26dc361d9b) | Defer weapon-draw recenter until safe | Отложенный weapon-draw recenter; не вводить поверх нашей текущей привязки. |
| 16 | [ee9edacf](https://github.com/Ajson44/cyberpunk-vr-port/commit/ee9edacfea2e42edc9ff770b3786055578dd33dc) | Add optional VR cyberware activation chord | Первый cyberware chord; итоговую маршрутизацию смотреть в 7323d077. |
| 17 | [bc988c86](https://github.com/Ajson44/cyberpunk-vr-port/commit/bc988c867f85e0319379fdf56d7cf05d364becb8) | Allow recenter after vehicle seating settles | Recenter после посадки; старый camera/anchor путь. |
| 18 | [5c2289de](https://github.com/Ajson44/cyberpunk-vr-port/commit/5c2289de56524ddacf5acb6d34563ddcaef4f66f) | Reseed seated VRIK origin and trace vehicle cameras | Пересев seated VRIK и camera trace; у нас уже другое pairing/reseed решение. |
| 19 | [8345c241](https://github.com/Ajson44/cyberpunk-vr-port/commit/8345c241e7138a965b5599925b8b916dc9957fec) | Surface head height control in VRIK tab | Поле высоты головы в старом VRIK интерфейсе. |
| 20 | [6cff697c](https://github.com/Ajson44/cyberpunk-vr-port/commit/6cff697c4e61a59569b60371a1c871b200335727) | Reset wheel interaction state across vehicle transitions | Сброс хвата руля при переходах; сравнить с уже обновлённым нашим рулём. |
| 21 | [3a56126a](https://github.com/Ajson44/cyberpunk-vr-port/commit/3a56126a14d59f3e112177f79643779ba6b7434a) | Add bounded vehicle camera and hand diagnostics | Ограниченная диагностика vehicle camera/hands; не сюжетная функциональность. |
| 22 | [e4fbda87](https://github.com/Ajson44/cyberpunk-vr-port/commit/e4fbda87b066b7c61dfe855e8a6cfa00676cd0c2) | Move OpenXR recenter hotkey to F6 | F6 recenter; изменение горячей клавиши. |
| 23 | [0490a2df](https://github.com/Ajson44/cyberpunk-vr-port/commit/0490a2dfebc037e465e9ad5545ff678fc90bb758) | Add optional grips and B grenade chord | Начальный grenade chord; брать только конечный hold lifecycleV39. |
| 24 | [3a3ad9c4](https://github.com/Ajson44/cyberpunk-vr-port/commit/3a3ad9c448394e46822bcd7bfcecfd49cb83472b) | Use full entity frame for mounted hand targets | Полный entity frame для рук в машине; у нас новый native hand-frame путь. |
| 25 | [726c8a31](https://github.com/Ajson44/cyberpunk-vr-port/commit/726c8a312e169c59cbc8ab907ec9c502a3fbfd29) | Repair stabilized vehicle eye orientation | Старая коррекция ориентации vehicle eye; напрямую не переносить. |
| 26 | [8cf9d261](https://github.com/Ajson44/cyberpunk-vr-port/commit/8cf9d261d43226278f8b277d9fbe3d232be67bbf) | Align stabilized auxiliary vehicle views | Стабилизация дополнительных vehicle views; напрямую не переносить. |
| 27 | [2c9cc250](https://github.com/Ajson44/cyberpunk-vr-port/commit/2c9cc2501fe4c86f0b23b74f7739355af7de0e95) | Share stabilized vehicle submission pose | Общая vehicle submit pose; не смешивать с нашим exact pose transport. |
| 28 | [3dda8647](https://github.com/Ajson44/cyberpunk-vr-port/commit/3dda864743513575d1a062031b8cfd78f951e992) | Add deterministic Ajson package manager | Установщик с записью владения файлами и откатом; полезен для дистрибутива. |
| 29 | [3a83743a](https://github.com/Ajson44/cyberpunk-vr-port/commit/3a83743aa8cf51767bb13d0a9f1d45884a2dd81c) | Flush vehicle pose backlog before on-foot resume | Очистка старых vehicle poses при выходе; сравнить только симптомы/жизненный цикл. |
| 30 | [9519befd](https://github.com/Ajson44/cyberpunk-vr-port/commit/9519befd378a2254c5741fab3b8cf71607b6af40) | Trace scripted seated stereo from stable Ajson baseline | Диагностика scripted seated stereo, включая render-side записи для экспериментов. |
| 31 | [1846009b](https://github.com/Ajson44/cyberpunk-vr-port/commit/1846009b72b3909fa29bff46a381e18359331c6b) | Package diagnostics with settings preservation and PowerShell 5 verify support | Сохранение настроек в пакете и PowerShell5 совместимость проверки. |
| 32 | [844b920a](https://github.com/Ajson44/cyberpunk-vr-port/commit/844b920a6ac841526b2580e309103c12733cedc3) | Add repeatable F10 stereo diagnostic capture button | F10 кнопка записи диагностики; опциональный инструмент разработки. |
| 33 | [7e47c086](https://github.com/Ajson44/cyberpunk-vr-port/commit/7e47c086e9cdf02c281ba2823e92e19fda22ed59) | Add three-mode stereo isolation diagnostic | Три режима изоляции stereo; диагностический эксперимент. |
| 34 | [40d7ec61](https://github.com/Ajson44/cyberpunk-vr-port/commit/40d7ec61fe959e6188090d868c4d543c18c3eb03) | Correct MAIN snapshot pool resource states | Состояния ресурсов MAIN snapshot pool; сравнить с нашей новой системой lease. |
| 35 | [980c82fc](https://github.com/Ajson44/cyberpunk-vr-port/commit/980c82fcffcc32d466d7da48253cd0b900cb71a3) | Add reversible MAIN frame-slot pose mode | Обратимый режим MAIN pose slot; не включать как production fix. |
| 36 | [b60e5030](https://github.com/Ajson44/cyberpunk-vr-port/commit/b60e50308b05439910e99e81462d6861f346f07b) | Trace MAIN production freshness through XR submission | Измерение свежести MAIN до XR submission; диагностический коммит. |
| 37 | [f2a6267b](https://github.com/Ajson44/cyberpunk-vr-port/commit/f2a6267b97efe407fe3a6e9ac396eb9db9d121e1) | Trace GPU-visible MAIN pixel handoff | Проверка GPU-visible MAIN handoff; диагностический коммит. |
| 38 | [8daf328e](https://github.com/Ajson44/cyberpunk-vr-port/commit/8daf328eff8effa431647594832f532c2045445a) | Isolate HMD injection into MAIN camera | Изоляция HMD injection в MAIN; диагностический коммит. |
| 39 | [8fb908bc](https://github.com/Ajson44/cyberpunk-vr-port/commit/8fb908bceb98a7c3cb120a619f6dbe66ff4afa6a) | docs: preserve MAIN HMD diagnostic handoff | Описание результатов MAIN/HMD эксперимента. |
| 40 | [5bad68b4](https://github.com/Ajson44/cyberpunk-vr-port/commit/5bad68b48c7ea9624ab2f6b11af437c5e8d4eab4) | Trace CameraDirector MAIN source ownership | Диагностика выбранного CameraDirector MAIN; историческая информация. |
| 41 | [5435f83e](https://github.com/Ajson44/cyberpunk-vr-port/commit/5435f83e453d3076888e2f26e6aeae1dba70e729) | Fix MAIN ownership after UI camera transitions | MAIN ownership после UI; наша CameraPoseTransport архитектура существенно новее. |
| 42 | [925f72f0](https://github.com/Ajson44/cyberpunk-vr-port/commit/925f72f09532b7d038bb2779d977fac9ac19d172) | Keep settled vehicle stereo orientation pair locked | Общий vehicle quaternion; у нас уже свой исправленный world-yaw ключ. |
| 43 | [826cb346](https://github.com/Ajson44/cyberpunk-vr-port/commit/826cb346b50b042f17283243b27176608d166732) | Restore scanner gesture with HMD gaze | Scanner HMD-ray и жест; кандидат отдельно от выбора жеста пользователя. |
| 44 | [9ff82112](https://github.com/Ajson44/cyberpunk-vr-port/commit/9ff82112700eacdc3dd9f9eb61d0fca6a4f77436) | Use HMD gaze for scripted quest attention | Judy/workspot HMD attention; важный кандидат, учитывать итоговые гейты V39. |
| 45 | [59068cf0](https://github.com/Ajson44/cyberpunk-vr-port/commit/59068cf0b9247cfe358e02e392b75c6c4ba6e5fd) | Use hybrid controls for remote cameras | Гибридный ввод удалённых камер; персональная схема, не заменять takeover целиком. |
| 46 | [082cdc00](https://github.com/Ajson44/cyberpunk-vr-port/commit/082cdc00ede02ef6260f1f4f2a40c22078867289) | Use live takeover aim for remote camera look | Живая ориентация takeover; сравнить с текущим исправлением yaw. |
| 47 | [29e6662c](https://github.com/Ajson44/cyberpunk-vr-port/commit/29e6662cd17d55881d324bda2583bde316bf34da) | Remove HMD feedback from remote camera aim | Устранение feedback remote aim; учитывать как причину, не переносить старый writer. |
| 48 | [17d57de8](https://github.com/Ajson44/cyberpunk-vr-port/commit/17d57de8f062f335403af92290a3e7357ecc7097) | Add controller-only remote camera mode | Опция controller-only surveillance camera; дополнение по желанию. |
| 49 | [faef8639](https://github.com/Ajson44/cyberpunk-vr-port/commit/faef8639b7455eb381d8e49ecd675233dba8aedf) | Preserve full remote camera pitch input | Возврат полного pitch ввода remote camera; относится к предыдущей опции. |
| 50 | [b9f3fdb4](https://github.com/Ajson44/cyberpunk-vr-port/commit/b9f3fdb45f8a77de897b1506fe0150c780279a43) | Swap remote camera zoom triggers | Поменяны местами zoom triggers; предпочтение раскладки. |
| 51 | [582fe4c2](https://github.com/Ajson44/cyberpunk-vr-port/commit/582fe4c2aca1015c26f964a5dd75ddeb2ec7456f) | Keep dominant ownership during two-hand grip | Dominant ownership при хвате двумя руками; сравнить с нашими handoff правилами. |
| 52 | [89b0bdcb](https://github.com/Ajson44/cyberpunk-vr-port/commit/89b0bdcbc2257a124f3ffdafa1c00799137f72f2) | Add optional physical weapon quick melee | Физический weapon quick-melee; отдельная опция, не наш обычный melee. |
| 53 | [05ec1ec0](https://github.com/Ajson44/cyberpunk-vr-port/commit/05ec1ec00c41676423f0925dbd937a843990195a) | Require support proximity for weapon handoff | Support-hand proximity перед передачей оружия; полезная независимая проверка. |
| 54 | [bfcef83c](https://github.com/Ajson44/cyberpunk-vr-port/commit/bfcef83cc0891ccc3fa9c1e602fa2fd3532ca0d5) | Emit keyboard Q for physical quick melee | Q вместо pad для quick-melee; требует фокуса/клавиатурной привязки. |
| 55 | [0f1a5233](https://github.com/Ajson44/cyberpunk-vr-port/commit/0f1a5233f57da766e76fc8256ec6249e5e0ec2b5) | Refine quick melee direction and reload handoff | Фильтр жеста melee и уступка native reload; возможный отдельный QoL-перенос. |
| 56 | [3cec7f65](https://github.com/Ajson44/cyberpunk-vr-port/commit/3cec7f65bd72880e60031f9143855fb0f2f05bad) | Cancel latched throttle on vehicle braking | Тормоз снимает/подавляет latched throttle; сравнить управление водителя. |
| 57 | [4f485535](https://github.com/Ajson44/cyberpunk-vr-port/commit/4f4855356254ea430da5d77474d0d6b9bd48ca6d) | Pulse D-pad chord selections | Импульсы D-pad chord; ранний этап, учитывать поздние hold fixes. |
| 58 | [094a1082](https://github.com/Ajson44/cyberpunk-vr-port/commit/094a1082bbf8916100f6972408f0f49bce01d819) | Refine armed driving and vehicle chords | Управление вооружённой машиной и chords; не переносить XInput монолитом. |
| 59 | [b00d67af](https://github.com/Ajson44/cyberpunk-vr-port/commit/b00d67af38ad716ec617d9cb259181393bae2c55) | Move dialogue selection to left grip chord | Диалоги через left grip; раскладка может отличаться от нашей. |
| 60 | [0e7b5b5b](https://github.com/Ajson44/cyberpunk-vr-port/commit/0e7b5b5b48bb4744d3dc7575683fda85ab1b7fe9) | Restore general D-pad chord alongside dialogue mode | Восстановление обычного D-pad chord рядом с dialogue mode. |
| 61 | [0189cb98](https://github.com/Ajson44/cyberpunk-vr-port/commit/0189cb980c0ca829e665dd8e16b45ed4a582fa08) | Document dialogue and vehicle control chords | Описание новых диалоговых/автомобильных chords. |
| 62 | [7eb2fb1f](https://github.com/Ajson44/cyberpunk-vr-port/commit/7eb2fb1fedf1b191b79d214932cdc867084f1abd) | Keep vehicle radio wheel held through selection | Удержание radio wheel до выбора; полезный принцип input lifetime. |
| 63 | [ca9b2bfc](https://github.com/Ajson44/cyberpunk-vr-port/commit/ca9b2bfc6e351ebc8c491fa345399e0b01794fec) | Route wheel hub gesture to native vehicle horn | Нативный horn по касанию ступицы; отдельный жест. |
| 64 | [ac2d1bad](https://github.com/Ajson44/cyberpunk-vr-port/commit/ac2d1badb3b301c33e35c5b2e6f96d0a755e5fd3) | Restore held input actions and vehicle controls | Восстановление held actions автомобиля; итог смотреть вместе с V36/V39. |
| 65 | [bdc20ef3](https://github.com/Ajson44/cyberpunk-vr-port/commit/bdc20ef3d5d740262315737af49dc7fd9a8f8dec) | Suppress normal driving stick yaw | Запрет обычного vehicle stick yaw; предпочтение управления, не обязательный фикс. |
| 66 | [6efeba9a](https://github.com/Ajson44/cyberpunk-vr-port/commit/6efeba9acd7ef08406fbded18fde884a1c8ef1af) | Verify held and vehicle input fixes | Статические проверки vehicle/held input. |
| 67 | [8545499f](https://github.com/Ajson44/cyberpunk-vr-port/commit/8545499f842b29f5efd3196054b754487fffcad9) | Disambiguate reload and grenade chord | Разделение reload и grenade chord; итог уточнён вfc502c3c. |
| 68 | [b6530e26](https://github.com/Ajson44/cyberpunk-vr-port/commit/b6530e26a2ec209bdf21df32c0786eb7b6646453) | Consume vehicle radio stick from camera yaw | Радио потребляет свой stick input и не поворачивает камеру. |
| 69 | [2244e91f](https://github.com/Ajson44/cyberpunk-vr-port/commit/2244e91f9023b005e7c3b3f109f3f5b42359d456) | Prevent mounted weapon draw from rebasing view | Weapon draw в машине не запускает recenter; не добавлять сам auto-recenter. |
| 70 | [4d3e450d](https://github.com/Ajson44/cyberpunk-vr-port/commit/4d3e450d52270ae037a2b4b1c2d572f5a6b967d9) | Keep two-hand grip out of dialogue input mode | Двуручный хват не становится dialogue modifier; полезный конфликтный тест. |
| 71 | [cb8378db](https://github.com/Ajson44/cyberpunk-vr-port/commit/cb8378db08133280e5fcb92ede0f9fda30b09319) | Prepare Ajson v9 private release documentation | Описание previewV9 и ограничений. |
| 72 | [c73fa823](https://github.com/Ajson44/cyberpunk-vr-port/commit/c73fa823c304e80b6a31cc70b28a3dd90d324f83) | Add read-only Heist Flathead state diagnostic | Read-only Heist/Flathead диагностика; полезна для воспроизведения, под debug gate. |
| 73 | [410342e0](https://github.com/Ajson44/cyberpunk-vr-port/commit/410342e0ab71637e6bd75e1e4799be852f611b6f) | Release stale device camera on direct takeover switch | Release линзы при прямом camera switch; у нас native ID release уже существует. |
| 74 | [e47cd759](https://github.com/Ajson44/cyberpunk-vr-port/commit/e47cd759e0ab7b8f8670d9fb2be2c043e771e30c) | Prevent camera watchdog slow-path feedback | Устранение watchdog slow-path feedback; старый bootstrap путь. |
| 75 | [a7669d40](https://github.com/Ajson44/cyberpunk-vr-port/commit/a7669d4047ec5eb1a8d2bc76faff1099412ea906) | Keep remote camera source switches on fast path | Переходы remote source через fast path; сравнить только с нашей текущей классификацией. |
| 76 | [db5ded05](https://github.com/Ajson44/cyberpunk-vr-port/commit/db5ded050d5d3735baeb0b21fcf676ace9e9c9f1) | Suspend camera watchdog during remote takeover | Отключение camera watchdog во время takeover; часть предыдущей цепочки. |
| 77 | [bc28e2b7](https://github.com/Ajson44/cyberpunk-vr-port/commit/bc28e2b7f8ae78ca92b8b974f71f9fed7b0b4740) | Instrument remote camera fast-path state | Диагностика remote fast path; не самостоятельный фикс. |
| 78 | [25485c3b](https://github.com/Ajson44/cyberpunk-vr-port/commit/25485c3b55c9d144558e79f7f1ea58f5a9099143) | Add no-lens takeover fast-path A-B | No-lens takeover A/B; эксперимент, итог в 08f6e9a. |
| 79 | [251fe7f1](https://github.com/Ajson44/cyberpunk-vr-port/commit/251fe7f1363dbcf1aea7d9262448a0f54f04e4fa) | Admit scripted MAIN switches through fast filter | Пропуск scripted MAIN через fast filter; старые указатели/ASM не копировать вслепую. |
| 80 | [3e0b9a2a](https://github.com/Ajson44/cyberpunk-vr-port/commit/3e0b9a2a81fcc3d285fedcdcd0affbc7b23d0126) | Bind Flathead lens through exact camera component | Flathead lens по точному cameraComponent; у нас точное имя уже используется. |
| 81 | [a6191e32](https://github.com/Ajson44/cyberpunk-vr-port/commit/a6191e3212a619bf8c9c769734146bc16a83f587) | Stabilize scripted camera identity by entity id | EntityID вместо сравнения Lua userdata; хороший небольшой кандидат. |
| 82 | [b6d1c872](https://github.com/Ajson44/cyberpunk-vr-port/commit/b6d1c8726ee6704ffc3419389d64311734a3caf8) | Route scripted passenger combat by vehicle state | PassengerCombat по PSM2, не IsDriver; у нас эта категория уже отдельно обработана. |
| 83 | [ba6e9c70](https://github.com/Ajson44/cyberpunk-vr-port/commit/ba6e9c70e2bbf1424811724f11068724ae1be0e4) | Route scripted workspot input by explicit context | Workspot/choiceHub/input context; полезная основа сюжетного ввода 3/4/7. |
| 84 | [61cd7d50](https://github.com/Ajson44/cyberpunk-vr-port/commit/61cd7d504b4a925892b8f0ca52de83247f0488e9) | Verify scripted context input routing | Статическая проверка scripted context routing. |
| 85 | [f507c771](https://github.com/Ajson44/cyberpunk-vr-port/commit/f507c77116642e61f3e73207be3850b997d13d89) | Update vehicle input preservation check | Обновлена проверка сохранения vehicle input. |
| 86 | [31957149](https://github.com/Ajson44/cyberpunk-vr-port/commit/31957149bd43f9be8a00f6f834afaf2e7466eb82) | Bank Delamain dialogue action findings | Зафиксирована неработавшая Delamain dialogue активация; не готовый фикс. |
| 87 | [7aac611e](https://github.com/Ajson44/cyberpunk-vr-port/commit/7aac611e49d73876ec656038b58c900448f04045) | Bank V21 scripted sequence validation findings | V21 отчёт: Judy работает, turret/air-hypo/dialogue ещё имеют проблемы. |
| 88 | [eaf4b7d5](https://github.com/Ajson44/cyberpunk-vr-port/commit/eaf4b7d56a41bbfb3981cccf54275c20b77c5871) | Route weapon turrets to native fire | Отделение SecurityTurret от surveillance: RT fire, временно заблокирован LT zoom. |
| 89 | [97efbfa6](https://github.com/Ajson44/cyberpunk-vr-port/commit/97efbfa67fcd68953ea4bf121536682db2c5ad07) | Restore scripted turret interaction input | Нативный X для взять/оставить турель; приоритетный кандидат для states3/4/7. |
| 90 | [0b67c3cc](https://github.com/Ajson44/cyberpunk-vr-port/commit/0b67c3cc6b072f667e23da46303c68ba255f17b2) | Add isolated mounted dialogue action test | Изолированный Delamain keyboard-action эксперимент; итог в 01b82f4a. |
| 91 | [0422abe0](https://github.com/Ajson44/cyberpunk-vr-port/commit/0422abe0981c5605c245ce080ad9bbad03b971bf) | Admit Delamain scene dialogue input | Допуск Delamain Scene даже при choiceHub=0; узкий контекст, не все машины. |
| 92 | [08f6e9a4](https://github.com/Ajson44/cyberpunk-vr-port/commit/08f6e9a4b644ea587556840331dbc5d68232d39b) | Enable validated Flathead camera path | Включён подтверждённый Flathead no-lens fast path; адаптировать к нашей камере. |
| 93 | [46d7fb4b](https://github.com/Ajson44/cyberpunk-vr-port/commit/46d7fb4b4e3720f9a0be95723e8dd76dd959637c) | Keep scripted vehicle stereo labels synchronized | Синхронизация старых scripted vehicle labels; не портировать поверх нашей pose-системы. |
| 94 | [35745096](https://github.com/Ajson44/cyberpunk-vr-port/commit/35745096e105635349d2ff7ffe535e1ffb11a6e2) | Route remote device interactions through HMD gaze | Remote-device HMD targeting для вентиляции Flathead; полезный потребитель gaze. |
| 95 | [01b82f4a](https://github.com/Ajson44/cyberpunk-vr-port/commit/01b82f4aa7e8d193822535b1598fa0a1b0467f78) | Enable validated mounted dialogue routing | Убрана тестовая настройка, mounted dialogue keyboard route стал автоматическим. |
| 96 | [b9beb9d7](https://github.com/Ajson44/cyberpunk-vr-port/commit/b9beb9d7b8c6919456b552932bebf2e94a0774ed) | Prepare Ajson v28 release candidate | Подготовка release candidateV28. |
| 97 | [76a1f2a4](https://github.com/Ajson44/cyberpunk-vr-port/commit/76a1f2a43f55a0bc890a27c15b07870b7bcb62df) | Prepare public Ajson v28 documentation | Публичное описание V28, Controls/Settings/Installer и происхождение изменений. |
| 98 | [d83b566e](https://github.com/Ajson44/cyberpunk-vr-port/commit/d83b566e9a676ed1a0a208cea564f7092a1f2c84) | Route authored scene observations through HMD gaze | Широкий Tier2+ HMD gaze; не брать, позднее отменён из-за регрессии. |
| 99 | [8572c188](https://github.com/Ajson44/cyberpunk-vr-port/commit/8572c18844f8ee71b4c030d80d6ea601764f73ce) | Preserve native origin for authored HMD gaze | Для того же эксперимента оставлен native origin; также superseded в 28a73447. |
| 100 | [07b9fe28](https://github.com/Ajson44/cyberpunk-vr-port/commit/07b9fe28ad4b927b9dcdb6120c56e086edb2b07b) | Trace authored focus-clue scan state | Диагностика scan/inspected состояния книги; не завершает её осмотр. |
| 101 | [00e0aee4](https://github.com/Ajson44/cyberpunk-vr-port/commit/00e0aee42fac603051d985f63e064c2444a30195) | Restore Jackie book manual clue inspection | Попытка через FocusClueStateChangeEvent; в отчёте признана неработающей. |
| 102 | [976148a6](https://github.com/Ajson44/cyberpunk-vr-port/commit/976148a6e12cd847bb42f17d52297fe8de83b149) | Document Ofrenda book clue investigation | Описание Ofrenda блокировки и следующего шага, а не подтверждение исправления. |
| 103 | [8beecb8c](https://github.com/Ajson44/cyberpunk-vr-port/commit/8beecb8c5c3312ef11d7ce82aa0d9773e5f0cd78) | Document controller chord routing follow-up | План исследования controller chords и launcher; не готовые исправления. |
| 104 | [7323d077](https://github.com/Ajson44/cyberpunk-vr-port/commit/7323d077a60ed5cd03ab3b88279996d9f8860092) | Route cyberware chord through dedicated action | Cyberware через выделенную E-action вместо LB+RB; возможный QoL-кандидат. |
| 105 | [886aa90b](https://github.com/Ajson44/cyberpunk-vr-port/commit/886aa90b395b75741e66823e3f9c0e333461bf6c) | Drive Ofrenda clue inspection through native state | Финальный redscript SetClueState для отсканированной книги на явный grip. |
| 106 | [28a73447](https://github.com/Ajson44/cyberpunk-vr-port/commit/28a734475f598f349504f80c406c4a63724ed5af) | Preserve native attention in authored scenes | Отменён широкий authored HMD override; native attention сохраняется у двери Джуди. |
| 107 | [038207f3](https://github.com/Ajson44/cyberpunk-vr-port/commit/038207f3501703847d473f96f89954aca469c349) | Map mounted vehicle weapons to right stick | Mounted vehicle guns назначены правому стику; личная раскладка, отдельное решение. |
| 108 | [80e493fd](https://github.com/Ajson44/cyberpunk-vr-port/commit/80e493fdf0ddcd5f40a69194c627aec83234fb4d) | Bank vehicle combat holster investigation | Vehicle combat holster отмечен как незавершённое расследование. |
| 109 | [a7ddea59](https://github.com/Ajson44/cyberpunk-vr-port/commit/a7ddea59e7b0761436aa5aa0fd62329bf57de076) | Fix Pacifica gaze and vehicle utility inputs | q110 camera gaze по read-only facts+750 мс lease; также A handbrake и phone chord. |
| 110 | [bfb8422f](https://github.com/Ajson44/cyberpunk-vr-port/commit/bfb8422feb99f6ab83fff2d5f3ad714778b4751d) | Document validated V36 progression | Отчёт о V36 и незавершённый projectile launcher; не считать launcher исправленным. |
| 111 | [59f74c24](https://github.com/Ajson44/cyberpunk-vr-port/commit/59f74c240374c2087e6671a17ef8dbd0fd98b0dd) | Preserve native quickslot hold lifecycle | Нативная длительность press/hold/release quickslot вместо 90 мс pulse; полезный принцип. |
| 112 | [fc502c3c](https://github.com/Ajson44/cyberpunk-vr-port/commit/fc502c3cec8207d94b42567411c7370ea3804997) | Fix two-hand quickslot and add pause chord | Grenade chord при установленном двуручном хвате, native pause chord, подсказки installer. |
| 113 | [ef4cac78](https://github.com/Ajson44/cyberpunk-vr-port/commit/ef4cac78184f88b615446680f56de86aa3e88ee3) | Restore plain Menu startup control | Исправлена V38 регрессия plain Menu/Start для стартовых экранов. |
| 114 | [78246284](https://github.com/Ajson44/cyberpunk-vr-port/commit/78246284e54f5e2c053285627521eff799ce38ff) | Document validated V39 playthrough checkpoint | Отчёт V39: stars/turret checkpoint пройден; Riders on the Storm stall не исправлен. |
