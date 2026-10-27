# Штатные movement, camera и skeleton: подтверждённые связи

Начато 2026-09-12. Исследование продолжается. **Полная цепочка ещё не закрыта.**
Этот документ фиксирует проверенные участки, а не предполагаемый порядок всей сцены.
Roomscale-реализация не является частью этого исследования.

Продолжение после удаления VR: [VR-off PID 21236](cp2077-native-vr-off-21236.md).
Там подтверждены конкретный camera binding, кость `Torso_fppCamera_Aim_JNT`,
граница local→model позы и buffered physics consumer. Старые адреса объектов
из этого документа к тому запуску не относятся.

Последующие подтверждения:
- [PID 9976](cp2077-native-vr-off-9976.md): optional locomotion parameters → CCT height.
- [PID 19316](cp2077-native-vr-off-19316.md): script stack CrouchEvents и scalar `crouch`.
- [PID 1800](cp2077-native-vr-off-1800.md): pending scalar → graph working slot,
  model pose → skin palette → exact-copy в mapped D3D12 buffer.
- [Краткая карта цепочки и оставшиеся пробелы](cp2077-native-chain-index.md).
- [Порядок native jobs](cp2077-native-job-order.md): три bucket, 12 внутренних
  этапов, зависимости physics/animation pre-update и последующего cascade.

## 1. Источник доказательств

- Статика: существующая `C:\Users\dariulone\Desktop\ida_headless\cp2077.i64`,
  Python313 + IDA Professional 9.2 idalib, `run_auto_analysis=False`,
  `close_database(False)`. Исправление malformed frame metadata выполняется только
  в памяти для декомпиляции; инструкции проверяются отдельно.
- Imagebase: **`0x140000000`**. Все адреса вида `0x140...` ниже — IDA VA.
  Runtime VA = `moduleBase + (IDA_VA - 0x140000000)`.
- MD5 входного файла IDB и установленного EXE совпали:
  `9add9693b83dfbae264c449487cea379`.
- SHA-256 установленного EXE:
  `a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991`.
- Live: x64dbg, сначала PID **7808**, после перезапуска пользователем PID **6552**.
  Для обоих заново прочитанный moduleBase: `0x7FF73E2A0000`.
- [Извлечения из ответов live-инструментов](cp2077-native-movement-live-20260912.json).
  Это отдельные остановы/чтения, **не синхронная запись одного кадра**.

Повторяемые статические выгрузки:

| Метка | Артефакт в `render_camera_RE/analysis/` |
|---|---|
| S1 | `native_movement_baseline_20260912.md` |
| S2 | `native_movement_connections_20260912.md` |
| S3 | `native_movement_stateflow_20260912.md` |
| S4 | `native_movement_yaw_writer_20260912.md` |
| S5 | `native_movement_input_pose_20260912.md` |
| S6 | `native_movement_camera_bridge_20260912.md` |
| S7 | `native_movement_action_sources_20260912.md` |
| S8 | `native_movement_action_physics_20260912.md` |
| S9 | `native_movement_property_actions_20260912.md` |
| C1 | `native_movement_catalog_20260912.json` — identity, constants, names, xrefs |
| C2 | `native_movement_action_names_20260912.json` |

Генератор S1–S9: `render_camera_RE/scripts/player_movement_dispatch.py` с точными
`--targets`, `--cells`, `--repair-frames --asm`. C1:
`render_camera_RE/scripts/native_movement_catalog.py`.

**Правила чтения:** прямой `call` доказывает порядок внутри данного вызова.
Косвенный вызов связывается с реализацией только по vtable конкретного объекта.
Совпадение смещения в разных объектах не доказывает одинаковый смысл поля.
Наличие писателя не доказывает, что он единственный. Порядок строк двух разных
выгрузок не является порядком исполнения.

## 2. Объекты текущего live-запуска

PID 6552; адреса теряют силу при следующем запуске/пересоздании объектов.

| Обозначение | Runtime object | Установленная связь |
|---|---|---|
| `S` | `0x2A4A6DFDB90` | `moveComponent`; vtable IDA `0x142ABEDA0` |
| `P` | `0x2A4A6DFB970` | `[S+0xA8]`; vtable `0x142B5D3B0` |
| `E` | `0x2A4A6DB7450` | owner `[S+0x90]` |
| `T` | `0x2A0AF706FB0` | placed component `[E+0xB0]` |
| `C` | `0x2A09EF4E330` | active movement controller `[S+0x160]`; vtable `0x142B5D470` |
| `B` | `0x2A097A594C0` | RED CCT backend; vtable `0x142AC9B58` |
| `F` | `0x2A4A6DFE8F0` | камера, участвующая в `0x1403F32A4`; vtable `0x142B031D0` |
| `L` | `0x2A0AF0D6C20` | объект sampled MoveXY в подтверждённом нажатии W |

Идентификация `S` начинается с опубликованного указателя BodyYawCensus:
runtime `CyberpunkVR_Stereo.dll+0x2F9790`. Это **источник из мода**;
vtable/обратные связи далее проверяются отдельно. В PID 7808 останов на
`0x1404C2018` дал `RCX=P`, `R15=S`, `[P+0x10]=S` и ожидаемую vtable P.

Физический handle `P+0xC4` / `C+0x40` = `0x00010015`.
Registry `0x2B4A02C0000`, index `0x15`, generation `1` совпала с таблицей.
Запись `registry+0x2018+index*16` содержит `B`.
`[B+0x60]=0x2A4A2EDA1B0` — следующий объект CCT.

## 3. MoveX / MoveY: вход в locomotion

**S1/S2/S4, live W подтверждено пользователем.**

### 3.0 Заполнение action-таблицы

**S2/S8/S9 + отдельное повторное нажатие W.**

`0x1405BFF84(inputObject, actionEvent)` ищет запись по имени из
`actionEvent+0x40` в массиве `[inputObject+8]`, count `+0x14`, stride `0x30`.
Из `actionEvent+0x48` берёт type, из `+0x4C` — float value.
В ветке обновления существующей записи:

```asm
0x1405BFFBA movss xmm4, [rdx+4Ch]  ; входное значение action
...
0x1405C0023 movss [rax+0Ch], xmm4  ; текущее значение записи
```

Код также обновляет переходные flags/counter и временные поля записи;
если запись отсутствует, вызывает `0x1405C0A9C` и `0x1405C09C8` для добавления.
Один прямой вызывающий — `0x1405BFCE8`, call `0x1405BFD00`; функция также
присутствует в vtable, поэтому этот вызывающий не объявляется единственным.

Live PID 6552: inputObject `0x2A0AF784C10`, array `0x2A4A4AEF270`, count 60.
Поиск **в границах массива** нашёл `MoveY` в `0x2A4A4AEFA50`.
На подтверждённом W hardware write сработал после `0x1405C0023`:
`record+0x0C=1.0`, hash записи `0xE386BDF96B1AB457`.
Чтение `0x1404C6C9C` использует ту же раскладку и возвращает `[record+0x0C]`,
либо 0, если имени нет. Producer **самого actionEvent** ещё не прослежен.

### 3.1 Sampling

```text
0x1406AB804
  0x1406AB81F -> 0x140930EFC
    0x140930F3E -> 0x1404C64CC(L)
      0x1404C6564 -> 0x1404C6C9C(input, hash("MoveX"))
      0x1404C657D: [L+0x90] = x
      0x1404C6584 -> 0x1404C6C9C(input, hash("MoveY"))
      0x1404C6594: [L+0x94] = y
      if x*x + y*y > 1: 0x1404F6524(&L.xy)
      публикации LocomotionInputDirectionX / LocomotionInputDirectionY
      0x1404C661F -> 0x1404C6CC8(..., &L.xy)
```

Хеши FNV1a64: `MoveX=E386BCF96B1AB2A4`, `MoveY=E386BDF96B1AB457` (C1).
Имена двух публикаций подтверждены регистраторами:
`0x1418D074C` → `LocomotionInputDirectionX`,
`0x1418D0790` → `LocomotionInputDirectionY`.

При отсутствии input-указателя существует другая ветка: чтение сохранённых
`LocomotionInputDirection*`, либо нули при отсутствии значения. Она присутствует
в cold chunks `0x141E930D2..`; обычный живой W прошёл основной путь.

**Live W:** после слов пользователя «Нажал, остановилось»:
`L+0x90..0x97 = 00 00 00 00 00 00 80 3F`, то есть **`(0,1)`**.
Стоп был на `0x7FF73E221588`: сразу после перенесённого оригинального `movss`
и **до** вызова `OnOnFootMoveXYCallback`. Адрес возврата основной функции:
`0x7FF73EBD0F43` → IDA `0x140930F43`.

### 3.2 Вычисление перемещения

В том же `0x1406AB804`, **после** возврата `0x140930EFC`, берутся
`L+0x90/+0x94` и передаются в `0x1406AB990` (call `0x1406AB91A`).
Есть проверки состояния, которые могут не допустить этот вызов; это не
безусловный тик для любого состояния игрока.

`0x1406AB990` вызывает `0x1406ABB18` (call `0x1406ABA46`) и затем записывает
результат в **`P+0x80/+0x84/+0x88`**:

```asm
0x1406ABA80 movss [rax+88h], xmm1
0x1406ABA88 movss [rax+84h], xmm0
0x1406ABA90 movss [rax+80h], xmm7
```

`RAX` получен через `0x1406AC294`: context interface → `[returnedContext+8]`.
`0x1406ABB18` получает параметры locomotion, входной вектор, данные предыдущего
физического состояния, обрабатывает ограничения ground/air speed и перед
возвратом умножает результирующий вектор на timestep из context.
Конкретный участок: `0x1406ABF2F` читает `[r13]`, `0x1406ABF35` делает
broadcast, **`0x1406ABF39: mulps xmm0,xmm6`**; далее компоненты возвращаются
через output buffer (`0x1406ABF76/+0x81/+0x87`).
Это **запрос перемещения на шаг**, а не уже принятая позиция сущности.
Его нельзя отождествлять с `S+0x220` или `B+0x114` (поля скорости).

## 4. Mouse yaw: писатель, потребитель и кватернион

**S1/S2/S3/S4/S5, live mouse подтверждено пользователем.**

`0x140930EFC` после MoveXY вызывает `0x1403F32A4` по `L+0xD8`
(call `0x140930F52`, если указатель ненулевой).

В наблюдавшейся ветке `0x1403F32A4`:

```asm
0x1403F3642 movss xmm0, [rsp+68h]
0x1403F364B addss xmm0, xmm7
0x1403F3654 mov rcx, [rax+8]      ; P
0x1403F3658 movss [rcx+9Ch], xmm0
```

До этого функция суммирует вклады нескольких вызовов. Поэтому `P+0x9C`
означает итоговую дельту этого канала, **не сырое движение мыши в пикселях**.
В этой же ветке записываются поля камеры `F+0x4E4/+0x4E8` и объекта
`[F+0x360]+0x4C/+0x68`, затем вызывается `0x1404E1638`.
При другом результате `0x1403F4E88` выполняется отдельная ветка
`0x1403F3727`, которая не проходит запись `0x1403F3658`.

### 4.0 Чтение mouse и analog actions

**S6/S7/S9/C2 + runtime hashes.** Условный вызов
`0x1403F32A4 → 0x1404C4B28 → 0x1404C6730` собирает один из входов итоговой
дельты. В `0x1404C6730` через `0x1404C6C9C` читаются:

| Global | Action hash | Имя |
|---|---|---|
| `0x1437F57C8` | `CEA226A8E3DE8737` | `CameraMouseX` |
| `0x1437F57D0` | `CEA225A8E3DE8584` | `CameraMouseY` |
| `0x1437F57D8` | `88360689A2878182` | `CameraX` |
| `0x1437F57E0` | `88360789A2878335` | `CameraY` |

Runtime bytes всех четырёх globals прочитаны одним блоком:
`3787DEE3A826A2CE8485DEE3A825A2CE828187A289063688358387A289073688`.
Mouse-регистраторы `0x1418D8E38/0x1418D8E70` связывают имена с хешами.

Проверенное различие путей:

- MouseX/Y умножаются на coefficients из текущего parameter block
  (`+8/+0xC`, общий множитель из `sub_14033F224(a1+0x5C)`, `+0x18/+0x1C`).
  В этом произведении **нет dt**.
- CameraX/Y проходят отдельные scaling/processing, ограничение прироста
  magnitude через dt и дополнительные state-dependent коэффициенты.
  Их angular contribution **умножается на dt** перед сложением с mouse.
- Есть сглаживаемая история analog-направления с `exp2f(-4*dt)`, участвующая
  в выборе обработки; нельзя из этого вывести, что весь mouse input имеет
  тот же фильтр.
- Результат: `out.x=0`, `out.z=-(analogXContribution+mouseXContribution)*scale`,
  `out.y=(analogYContribution+mouseYContribution)*scale`, с дополнительными
  коэффициентами из текущих настроек/состояния.

Это подтверждённый конкретный input path, не доказательство исчерпания всех
вкладов: следующие virtual calls в `0x1403F32A4` также получают эти буферы.

Два различных живых события:

1. PID 7808: пользователь сообщил «бп сработал без поворота мышки».
   Hardware watchpoint нашёл `0x1403F3658`, содержимое `P+0x9C` — **0**.
   Это доказательство писателя, не mouse stimulus.
2. PID 6552: условие `RCX==P && (dword:[RCX+9C] & 7FFFFFFF)!=0`,
   stop после store (`0x1403F3660`). Пользователь: **«Повернул, остановилось»**.
   Байты `CC CC 4C 3D` = float32 **0.04999999701976776**.
   `P+0xA0/+0xA4` в этот момент ещё содержали нули.

### 4.1 Применение дельты

`0x140336390(S)` вызывает `0x140336F78(&S.provider, S, dt)`.
Внутри `0x140336FC8` вызов `[P.vtable+0x18]` на наблюдавшейся vtable
`0x142B5D3B0` разрешается в **`0x1404C2018`**:

```text
delta = P[0x9C]
P[0x9C] = 0
P[0xA4] = delta
P[0xA0] += delta
0x1404C28C0(...) -> P+0x150
0x1404C2124(...) -> P+0x110
P[0x80..0x88] = 0
```

Построение **delta quaternion**, подтверждённое инструкциями:

- `0x14033700C` → `[P.vtable+0x20]` → `0x140D455B0`;
- `0x140336F14` → `[P.vtable+0x30]` → `0x140CD8438`;
- обе реализации читают **`P+0xA4`**, умножают на константу
  `dword_1431EEFA4 = 0.008726646192371845 ≈ π/360`, затем `sincosf`;
- результат `(0,0,sin(delta*π/360),cos(delta*π/360))`.

Следовательно, **`P+0x9C/+0xA4` измеряются в градусах**.
Накопитель `P+0xA0` нельзя автоматически объявлять текущим yaw сущности:
в указанных построителях используется именно `+0xA4`.

`0x1404C28C0` не является этим построителем: по S1 она собирает данные
физического окружения/опоры. Прежнее описание её как преобразования yaw
не использовать.

## 5. Физика и обновление entity

### 5.1 Публикация запроса и CCT

**S1/S2/S3.** `0x1403362F4(S,dt)` сохраняет `dt` в `S+0x22C`.
При `S+0x8B != 0` вызывает `0x140336344(&S.provider, ..., dt)`:

```text
[P.vtable+0x10] -> 0x1408E869C
  0x14204AA10 -> property 27, sizeof(float)=4, timestep
  0x14204A9D0 -> property 9, size=12, &P[0x80]
  обе записи через 0x140240998
```

**S3/S9:** dispatcher `0x140240998` имеет два пути:

1. При ненулевом buffer/context вызывает `0x140240A60`, который копирует payload
   и metadata property в буфер. Проверяется generation; существующая запись
   может заменяться, новая размещается в блоках.
2. Без buffer разрешает handle через `0x14023F3FC` и вызывает
   `[B.vtable+0x58]` → **`0x140926020`**.

В `0x140926020` подтверждены destination fields:
property **9** → `B+0x120/+0x124/+0x128`, property **27** → `B+0x144`.
Property **4** пишет другие поля — `B+0x114/+0x118/+0x11C`.
Таким образом, pending displacement и velocity представлены раздельно.
Какой путь выбран в каждом конкретном tick и где выполняется flush buffer,
ещё требует сопоставленной трассы.

Сам backend tick:

```text
0x1402BEC80
  dt = B[0x144] (если отрицательное — входной timestep)
  B[0x140] = dt
  0x1402BECFB -> 0x1402BF5A8(B, &B[0x120], dt)
    gate: B[0x185]!=0 && dt>1e-6
    подготовка displacement и данных контакта
    0x1402BF6DE -> [[B+0x60].vtable+0x10]
    чтение результата CCT
    B+0x114/+0x118 = скорректированное delta-position / dt
    обновление B+0x108/+0x10C/+0x110
    очистка B+0x120/+0x124/+0x128
```

Обработка включает отдельные коррекции опоры/контактов; упрощение до
«позиция += входной вектор» не описывает этот код.
Полная доставка property 9/27 через очередь к полям backend ещё требует
проверки; двух концов недостаточно для утверждения о scheduler/fence.

### 5.2 Из controller к placed component

**S1/S2/S6 + текущая vtable C.**

`0x140336390` сбрасывает временные translation/quaternion в `S+0x170/+0x180`,
вызывает provider, затем при соответствующих флагах:

```text
0x140337070(S+0x138, dt, &S[0x170], &S[0x180], flag)
  [C.vtable+0x20] -> 0x14057B714
    запрос позиции по C+0x40 через 0x14057B0C8
    C+0x20..0x28 = полученная/скорректированная позиция
    C+0x30 = normalize(C.quaternion * incomingQuaternion)
  [C.vtable+0x48] -> позиция
  [C.vtable+0x50] -> quaternion
```

`0x1401A8CC4` получает позицию активного C; `0x140336E3C` получает его quaternion
и нормализует. Обе функции имеют fallback при отсутствии/неактивности C.

У подходящей ветки owner есть дополнительное сглаживание:
`0x140337634(S,dt)` уменьшает `S+0x250`, сводит translation `S+0x230` к нулю,
quaternion `S+0x240` к identity. Скорректированный трансформ передаётся:

```asm
0x1403365DC mov rax, [r15+90h]       ; E
0x1403365EA mov rcx, [rax+0B0h]      ; T
...
0x1403365FA call 0x1401DC0E0
...
0x1403367CD movups [r15+1D0h], xmm0  ; более поздняя копия в S
```

`0x1401DC0E0` сравнивает трансформ, записывает local fixed-point position
`T+0xC0` и quaternion `T+0xD0`; при наличии владельца системы dirty calls
`0x1401C9760`. Поэтому `S+0x1D0` **не место раннего применения трансформа T**.
Упоминание этой записи как единственного «источника yaw тела» в старых заметках
не является достаточным доказательством.

## 6. Параметры анимации — отдельная ветвь

**S2/S3/S4/S5.** Внутри `0x140930EFC` порядок такой:

```text
MoveXY -> 0x1403F32A4 (camera/yaw) -> 0x1404C6064 (sample movement state)
  -> заполнение feature по [L+0xA8]
  -> 0x1402E0E3C(animatedComponent, "playerLocomotion", featureHandle)
  -> отправка feature в AttachmentSlots.WeaponRight / WeaponLeft, если найдены
возврат в 0x1406AB804
  -> вычисление нового запроса через 0x1406AB990
```

Имя `playerLocomotion` и хеш `0x23F2D4B0D459D639` подтверждены
`0x1400ED540`, а не угаданы по назначению.

Часть полей feature, записанная в `0x140930EFC`:

| Смещение feature | Подтверждённый источник |
|---|---|
| `+0x50` | `L+0xF4`, модуль планарной скорости из `0x1404C6064` |
| `+0x5C` | `(L+0x110) / context.dt`, изменение планарной скорости / dt |
| `+0x70` | вектор из `0x1403C207C(owner)` |
| `+0x80` | `L+0x114`, угловая величина, вычисленная через сравнение векторов |
| `+0x84` | `L+0xF8`, вертикальная компонента sampled velocity |
| `+0x8C` | `L+0x118`, результат инверсии проверки `0x1420479C0` |

Направление скорости обрабатывается относительно transform owner:
`0x1404C6064` → `0x1402E96C8`, `0x1404C6E3C` → `L+0x60`;
затем вызов `[feature.vtable+0x128]`.

Публикация feature ещё **не вычисление костей**:
`0x1402E0E3C` → `0x1402E0EC0` помещает/заменяет handle в массиве записей
`{name, handle}` (stride `0x18`, container+`0x70/+0x7C`). Конкретные graph nodes,
которые читают именно этот feature игрока, пока не установлены.

## 7. Вычисление и перенос позы скелета

**S1/S2/S5/S6, live endpoint.** Подтверждённая вложенность вызовов:

```text
0x141CA2650
  -> 0x1401D2DB8
    -> 0x140328934
      -> 0x1401E10BC (обычная ветвь с graph object)
        -> 0x14017DDB4 (call 0x1401E1BB8)
```

`0x140328934` имеет и отдельную ветку `+0x25C8 != 0`: она готовит другой
источник позы и напрямую вызывает `0x14017DDB4`.

**Что точно делает `0x14017DDB4`:**

- source bones = `[a3+0x30]`;
- pose descriptor = `[a2+0x38]`;
- destination bones = `[poseDescriptor]`;
- destination scalar tracks = `[poseDescriptor+0x18]`;
- по mapping pairs копирует bone transforms **stride `0x30` (48 bytes)**,
  тремя 16-байтовыми записями;
- для отдельной ветки mapped bones вызывает `0x14017DFD4` и `0x14017F458`;
- затем копирует scalar tracks (4 bytes) через отдельную таблицу mapping.

Это не доказательство, что здесь заканчиваются все animation/IK/skinning passes.

**Live:** на entry `0x14017DDB4` track buffer совпал с опубликованным player
track buffer; адрес возврата был `0x7FF73E481BBD` → `0x1401E1BBD`.
`a1=0x2A4AA9E9400`, `a2=0xA072BFED30`, `a3=0x2A4A41A6160`, `a4=0`.
Это подтверждает именно ребро `0x1401E10BC → 0x14017DDB4` для этой позы игрока.

**Local → parent chain:** `0x14017DFD4` берёт transform кости из массива
stride 48, читает signed int16 parent index из `[rig+0x10]`, вызывает
`0x1401DCC5C(parent, accumulated)` и поднимается до `parent=-1`.
Таким образом, buffer содержит иерархические transforms; его нельзя читать как
мировые позиции отдельных костей без parent accumulation и entity transform.

## 8. Применение трансформов камеры/компонентов

**S1/S5.** `0x1401D8558` — общий обход дочерних placed components.
Получает slot transform через binding interface `[vtable+0xD8]`, составляет его
с local transform ребёнка, записывает world fixed position `+0xE0` и
quaternion `+0xF0`, затем выполняет нотификацию при изменении и обход потомков.

`0x1401D92A0` — одна из реализаций slot getter: она копирует `+0xE0/+0xF0`
**с переданного в RDX parent**, в буфер R8. Уточнено по инструкциям и live
21236: прежнее описание чтения через поле `+0x48` было неверным.
**Это не доказывает, что каждый binding камеры — этого типа.**
Полную live-цепь привязок FPP от T через skeleton/slots ещё нужно пройти.

`0x140127F58` читает camera world `+0xE0/+0xF0` в CameraSetup
`+0x00/+0x10`; это отдельный этап от вычисления позы и transform propagation.
Подробнее о микшере: [camera-write-chain](cp2077-camera-write-chain.md).
Утверждения старого документа о причинности mouse/skeleton должны проверяться
по текущим данным, а не наследоваться вместе с раскладкой CameraSetup.

## 9. Влияние установленного мода на эти наблюдения

Аудит текущих `src/Hooks/OnFootMoveXY.cpp`, `OnFootDeltaHead.cpp`,
`BodyYawCensus.cpp`, `RoomscaleMove.cpp`, `AnimPose.cpp`:

- RoomscaleMove сейчас no-op, но **остальной мод активен**.
- OnFootMoveXY может поворачивать `L.xy` перед дальнейшим engine consumption.
  Поэтому live W снимался до C++ callback.
- OnFootDeltaHead выполняет `deltaHead[idx] += snap`, даже когда snap равен нулю.
  Аппаратный watchpoint действительно поймал эту запись в DLL в PID 7808.
  Она не включена в доказательство штатного писателя.
- AnimPose вызывает оригинал, а затем модифицирует bone buffer (VRIK и другие
  режимы). Снимок после возврата всего detour нельзя называть vanilla pose.
- BodyYawCensus публикует копию состояния. Это не доказательство порядка всех
  игровых jobs, final skinning или источника input.
- Watchpoint на `F+0xF8` в PID 6552 поймал запись из VR DLL, stop
  `0x7FFA46A12444`, `RCX=F+0xF8`, `RBX=F+0xF0`. Этот останов не разрешает
  native binding камеры: регистры принадлежат helper мода, а не engine caller.

Непригодный probe на `0x1401D8783` исключён из доказательств. Пользователь
сообщил падение до примерно одного обновления за две секунды. Кроме стоимости
общего сайта, фильтр по RSI был неверен: **текущий child загружается в RSI
только на `0x1401D8791`**, после проверяемого virtual call. На самом call
`RCX=binding`, `RDX=parent`, `R8=outSlotTransform`; текущий child лежит в
локальном handle на стеке. Несрабатывание прежнего фильтра не является
доказательством отсутствия камеры на этом пути. Этот горячий probe снят.

Попытка multi-site log-only trace не дала сопоставимого журнала: проверка
установки показала только один breakpoint вместо четырёх, а `logsave` вернул
ранее накопленный UI log без ожидаемой последовательности. Порядок jobs из
этой попытки не выводится; её точки также сняты.

## 10. Что ещё требуется доказать для полной цепочки

Это список отсутствующих доказательств, а не гипотезы об устройстве движка:

1. Hardware mouse/keyboard → action producer: mapping, sensitivity, smoothing,
   accumulation и выбор конкретного источника в `0x1403F32A4`.
2. Все существенные входы/выходы PSM Stand/Walk/Run/Sprint/Crouch и настройки,
   которые переключают native movement/camera ветви.
3. Полная доставка physics property 9/27, порядок CCT update относительно
   `0x1403362F4/0x140336390` и animation jobs в одном engine tick.
4. `playerLocomotion` → точные graph nodes игрока → clip selection/blending,
   root motion, turn-in-place, game IK → финальная палитра skinning.
5. Live parent/binding graph камеры и тела, включая skeleton slots;
   отдельная ветвь pitch и ограничения взгляда.
6. Один сопоставленный trace input → physics → entity → pose → camera с engine
   tick/job identities; проверка остановки движения, обратного хода и поворота.

До закрытия этих пунктов формулировка «100% всей цепочки» неприменима.

## 11. Подготовлен следующий запуск без CyberpunkVRPort

2026-09-12 пользователь выбрал отдельный VR-off запуск и указал использовать
готовый `dist/CyberpunkVRPort-dev-full/UNINSTALL.bat`.

- Игра PID 6552 закрыта; x64dbg `stopped`.
- Установленная копия `UNINSTALL.bat` побайтово совпала с `dev-full`.
- Перед выполнением сохранены **303 файла**, 54 660 841 bytes, включая текущие
  ini, CET persistence и два development-only каталога.
- Backup: `build/native-re-before-uninstall-20260912/installed-before-uninstall.zip`.
  SHA-256: `efb5df8ff3625808e086545275043d8d63543520efab4b1f534b1ce82ff5c622`.
  Для каждого файла в соседнем `manifest.json` записаны размер и SHA-256;
  содержимое ZIP было перечитано и сверено.
- `vrik_calibration.ini` рядом с DLL отсутствовал **до** uninstall; backup не
  объявляется копией несуществующего файла.
- Официальный batch, запущенный из корня игры: **Removed 94, already gone 0,
  could not remove 0**. Предложение восстановления старого UserSettings отклонено
  (`n`), чтобы оно не подменяло текущие настройки при временном отключении.
- Оставшиеся вне batch `CyberpunkVRPort_ReloadRecorder` и `r6/tweaks/vrport`
  перенесены в `build/native-re-before-uninstall-20260912/development-residuals/`
  после проверки хешей. В loader-каталогах их больше нет.
- Повторная проверка: все 94 цели batch отсутствуют, VR plugin/CET/REDscript/
  активные VR archives/tweaks сняты.

**Это VR-off baseline, не полностью немодифицированная установка:** в игре
остаются EquipmentEx, HUDitor, NovaOptics, CETBridge и другие перечисленные
frameworks/зависимости. Новые наблюдения следует привязывать к новому PID и
заново находить объекты. Все live-снимки разделов выше сделаны **до** uninstall.

Игру запускает пользователь. Обратное восстановление: overlay `dev-full`, затем
нужные сохранённые runtime-конфиги/содержимое backup по manifest; на этом этапе
восстановление ещё не выполнялось.
