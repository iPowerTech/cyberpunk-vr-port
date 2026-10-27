# Native movement / skeleton / camera — VR-off, PID 21236

2026-09-12. Продолжение [основного исследования](cp2077-native-movement-camera-skeleton.md).
Статика — существующая IDB, headless Python; live — x64dbg.
Все VA ниже — IDA imagebase `0x140000000`, если не указано `runtime`.
Сырые извлечения: [JSON](cp2077-native-vr-off-21236.json).

## 1. Проверенная среда

- Пользователь запустил игру после `dev-full/UNINSTALL.bat`.
- PID **21236**, EXE `C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe`.
- Заново прочитанная база EXE: **`0x7FF73E2A0000`**.
- `CyberpunkVR_Stereo.dll`: module query вернул `Module not found`.
- На `0x1404C6594` прочитан оригинальный `F3 0F 11 86 94 00 00 00`, а не jump
  в MoveXY trampoline. На `0x1404C2018` также оригинальный пролог.
- Другие моды/расширения остаются: это **VR-off**, не утверждение о полностью
  немодифицированной игре.

## 2. Найденные заново объекты

| Имя в документе | Runtime address | Связь / идентификация |
|---|---|---|
| `S` | `0x1CB9C976510` | movement component, vtable `0x142ABEDA0` |
| `P` | `0x1CB9C942B40` | provider, vtable `0x142B5D3B0`; `[P+0x10]=S` |
| `E` | `0x1CB9C8D7170` | `[S+0x90]` |
| `T` | `0x1C7899179D0` | placed component `[E+0xB0]` |
| `C` | `0x1C78A0D79E0` | active controller `[S+0x160]` |
| `B` | `0x1C797143ED0` | RED CCT backend, vtable `0x142AC9B58` |
| `F` | `0x1CB9C9582C0` | FPP camera, vtable `0x142B031D0`, CName `camera` |
| `H` | `0x1C7897F0050` | component CName `slots`, vtable `0x142B0FDF0` |
| `A` | `0x1CB96793520` | animated component CName `root`, vtable `0x142AE68B0` |
| `K` | `0x1C7A267F080` | cached animation state `[A+0x178]` |
| `R` | `0x1C7A45AACD0` | rig metadata `[K+8]` |
| `D` | `0x1C7967B2720` | pose descriptor `[K+0x18]` |
| `bones` | `0x1CBA63436C0` | `[D]`, 619 entries, stride `0x30` |

Provider найден однократным остановом **на native entry** `0x1404C2018`:
`RCX=P`, `RDX=R15=S`; return `0x140336FCB`.
Камера найдена в `0x1403F3660`: `RCX=P`, `[RBP-0x70]=F`.
Адреса из VR-on PID 6552 не использовались для этих объектов.

Physics handle `0x00010015`: registry `0x1DB902C0000`, запись
`registry+0x2018+0x15*16` содержит B, generation в
`registry+0x102010+0x15*2` равна 1. `[B+0x60]=0x1CB9C69D5A0`.

## 3. Entity world → animated component world

**Live stimulus:** пользователь подтвердил «Повернул, остановилось» после
короткого горизонтального поворота мышью без WASD.
Аппаратный watchpoint стоял **на `A+0xF8`**, не на общей функции.

- Писатель: `0x1401D8A8F`, `movups [rdx],xmm0`.
- Stop после записи: `0x1401D8A92`.
- `RSI=A`, `RDX=A+0xF0`, `R15=T`.
- Текущая child/binding запись: `0x1CB974645C0`, stride `0x20`:
  `{A, weakRefCounter, binding, weakRefCounter}`.
- Binding `0x1C7814DD518`, vtable `0x14307EA98`.
  Его slot `+0xD8` содержит `0x1401D92A0`.
- `0x1401D92A0` копирует world position/quaternion **из переданного RDX parent**
  в R8 output. Она не разыменовывает поле `+0x48` для получения трансформа.

На этом останове 32 bytes `T+0xE0` и `A+0xE0` совпали:

```text
F03FD30320233DED84F55D01000000000000000000000000FE54203FDD92473F
```

Local transform `A+0xC0/+0xD0` отдельно проверен: zero translation / identity
quaternion. Это согласуется с точным равенством world transforms в наблюдении.

## 4. Куда на самом деле привязана FPP camera

Watchpoint на **`F+0xF8`** дал тот же писатель `0x1401D8A8F`, но уже:

- `RSI=F`, `RDX=F+0xF0`, `R15=H`;
- child/binding entry `0x1C7A22B35A0` содержит F и binding `0x1C78D42EB10`;
- binding vtable `0x142ABFE30`, slot `+0xD8` → **`0x1401D768C`**;
- `[binding+0x30]=H`, `[binding+0x40]=F`;
- **slot index `[binding+0x58]=0`**.

Коррекция первоначального прочтения: `8` в `binding+0x50` — **flags**, не индекс
слота. Это проверено constructor `0x1401C6A2C` (flags `+0x50`, index `+0x58`)
и getter `0x1401D768C` (`mov edx,[rcx+58h]`). В цепочке используется **slot 0**.

```text
0x1401D8558: parent=H, child=F, binding=0x1C78D42EB10
  binding.vtable[+0xD8]
    0x1401D768C(binding, H, out)
      -> 0x1401D81E8(H, binding.slotIndex, &H.world, out)
         slot description = [H+0x120] + index*48
         resolved reference = [H+0x140] + index*40
         -> 0x1401D8DB8(reference, slotDescription, &H.world, out)
```

Slot array `[H+0x120]=0x1C7A17C4680`, count 9. Slot 0:

| Поле | Подтверждённое значение |
|---|---|
| slot name, `+0x00` | `0x6FCFDF926F11594E` = FNV1a64(`camera`) |
| referenced bone name, `+0x08` | `0x5666FFD42C359527` |
| local position, `+0x10..+0x18` | `(0,0,0)` |
| local quaternion, `+0x20` | identity |

Resolved reference для slot 0, `[H+0x140]=0x1C797205800`:

| Поле | Значение |
|---|---|
| `+0x00` | A |
| `+0x08` | weak-reference counter |
| `+0x10` | bone index **118** |
| `+0x18` | bone CName `0x5666FFD42C359527` |
| `+0x20` | rig identity `0xCBC4C8154D9FAFC0` |

`0x1401D9478` проверяет тип resolved object. У подходящей активной ветки
`0x1401D8DB8` вызывает **`0x1401D8BF4(A, &reference.boneDescriptor, outWorld)`**.
Это ребро подтверждено watchpoint на word bone index: stop `0x1401D8CAB`,
`RBX=A`, `RDI=0x1C797205810` — обратная запись descriptor после удачного чтения.

## 5. Конкретная кость и чтение её позы

Bone index 118 — **`Torso_fppCamera_Aim_JNT`**:

- Старый `bin/x64/player_bone_names.txt` использован только как источник текста
  имени; его старый `boneCount=620` не перенесён в новый запуск.
- FNV1a64 этого текста = `0x5666FFD42C359527`.
- `0x1401CA020` проверенно ищет имена в `[R+0x20]`, count `[R+0x2C]`.
- Текущий массив names `[R+0x20]=0x1CBA9CFEBB0`; entry 118 по
  `0x1CBA9CFEF60` содержит тот же hash.
- Текущий parent index 118 = **107**; имя 107 сверено по hash
  `0xFBD24A5F775921C8` = `Torso_fppCamera_Control_GRP`.
- Текущий bone count: **619**.

Точный путь чтения:

```text
0x1401D8BF4(A, boneDescriptor, outWorld)
  K = [A+0x178]  (есть fallback через 0x1402E9898 при null)
  -> 0x1401D9388(K, &descriptor, &boneTransform)
       R = [K+8]
       D = [K+0x18]
       сверка rig identity; при изменении -> 0x1401C9FC4 -> 0x1401CA020
       boneTransform = 48 bytes из [D] + 118*48
  compose A.world с boneTransform
  перевести translation в fixed point и записать outWorld
```

Для активного пути `0x1401D8BF4` составляет quaternion кости с **`A+0xF0`**,
вращает её translation этим world quaternion и прибавляет **`A+0xE0`**.
Поэтому здесь transform кости используется в model space animated component,
а возвращённый transform — world space.

Один подтверждённый успешный read вернул локальную копию 48 bytes:

```text
5E8DAFB300000035D2CCCC3F333333419E2A48BC52CBB2B3A837B7B31CFB7F3F
0000803F0000803F0000803F0000803F
```

Позже те же bytes прочитаны из bone buffer по `0x1CBA6344CE0`.
Позднее чтение не обозначается как тот же кадр.

## 6. Один buffer используется до и после local→model прохода

Это подтверждено двумя data-write остановами на **одной кости одного buffer**,
а смысл преобразования — инструкциями всего второго прохода.

### 6.1 Копирование pose output

`0x14017DDB4`, store **`0x14017DE33`**:

```asm
movups xmm1, [r11+r8*8+10h]
movups [r10+rdx*8+10h], xmm1
```

Live: `R10=bones`, `RDX=R8=708=118*6`, `RCX=118`,
`R11=0x1CB92FF1290`. Источник и destination адресуются с шагом 48 bytes.
После store quaternion (до следующего store scale):

```text
000040A900000000C8CC4C3E343313400000E0A8F404353FF404353F000020A9
0000803F0000803F0000803F0000803F
```

### 6.2 Иерархическое преобразование

**`0x1404B6968(poseDescriptor, rig)`**, store **`0x1404B6A59`**.
Live: `R11=bones`, `RDX=0x1620=118*48`,
`R10=0x1CBA0F185DC` — адрес int16 parent index 107, count 619.

Код обходит кости и для каждой с parent != -1:

```text
position[i] = position[parent] + rotate(q[parent], scale[parent] * position[i])
scale[i] *= scale[parent]
q[i] = q[parent] * q[i]
q[i] = normalize(q[i])
```

Store `0x1404B6A59` — quaternion composition, **ещё до normalization**.
Normalization идёт через `0x1401D9100`, затем второй store `0x1404B6A74`.
Снимок именно до normalization:

```text
5E8DAFB300000035D2CCCC3F33333341A02A48BC53CBB2B3A937B7B31EFB7F3F
0000803F0000803F0000803F0000803F
```

Статически подтверждённый порядок внутри **`0x1401D2DB8`**:

1. В цикле по graph instances условный call `0x140328934` на `0x1401D31F4`;
   внутри graph evaluation/copy путь к `0x14017DDB4`.
2. После завершения цикла — проверка context byte `+0x9C`.
3. Если byte установлен: call **`0x1404B6968` на `0x1401D3230`**.

Итак, `0x14017DDB4` не является границей готовой model-space позы.
Чтение одного и того же bone buffer без знания фазы может дать local или model
transforms. GPU skinning и все последующие passes этим исследованием ещё не
исчерпаны.

## 7. Подтверждён buffered physics property flush

Watchpoint на **`B+0x120`** отдельно поймал:

- очистку запроса в CCT tick: stop `0x1402BF860`;
- затем property writer: stop **`0x14092608D`**, `RCX=B`, `R9=9`.

На втором останове return address **`0x14024091C`** — это вызов из consumer
**`0x14024084C`**, а не непосредственная ветка `0x140240998`.
Payload `0x1DB9AF20770` и `B+0x120` побайтово равны:

```text
0000000000000000C7BC8EBD
```

Проверенный consumer:

```text
0x140240D94(buffer)
  для записей буфера:
    0x140240DF3 -> 0x14024084C(buffer, handle, record)
      generation / flags check
      payload = 0x140240BF8(buffer, packedOffset & 0xFFFFFF)
      property id = byte(record + index + 0x24)
      payload size = byte(record + index*4 + 0x17)
      0x140240918 -> [B.vtable+0x58](..., property, payload, size, flags)
        0x140926020: property 9 -> B+0x120/+0x124/+0x128
  ++buffer.generation(+0x84), сброс counters +0x80/+0x40
```

Регистрация producer (`0x1408E869C → 0x140240998 → 0x140240A60`) описана
в основном документе. Новое доказательство закрывает факт выполнения **buffered
consumer для конкретного B**. Полная job/fence временная цепь producer → flush →
CCT → entity → animation ещё требует отдельного подтверждения.

## 8. Подтверждённая зависимость без предположений о номере кадра

```text
entity placed T.world
  -> обычный binding 0x1401D92A0
  -> animated component A.world

AnimGraph pose output -> 0x14017DDB4 (local poses)
  -> 0x1404B6968 (local→model, in place)
  -> model transform Torso_fppCamera_Aim_JNT, index 118

A.world + model bone 118
  -> 0x1401D8BF4 (world bone transform)
  -> H slot 0 / slot binding 0x1401D768C
  -> F.world через 0x1401D8558 / store 0x1401D8A8F
```

Это dependency graph, **не измерение отсутствия задержки между кадрами**.
Parent propagation H дополнительно подтверждён ниже, в разделе 14. В здоровой
наблюдавшейся bone-reference ветке источником world bone служит A.world.

## 9. Артефакты

В `render_camera_RE/analysis/`:

- `native_vroff_camera_binding_21236.md` — vtables и slot-index layout.
- `native_vroff_slot_transform_21236.md` — slot description/reference lookup.
- `native_vroff_slot_bone_chain_21236.md` — resolved reference → bone world.
- `native_vroff_bone_world_getter_21236.md` — A.world composition.
- `native_vroff_pose_reader_21236.md` — cached pose descriptor / bone read.
- `native_vroff_rig_lookup_21236.md` — проверка bone name/index.
- `native_vroff_pose_hierarchy_21236.md` — полные инструкции local→model.
- `native_vroff_root_binding_21236.md` — простой parent world getter.
- `native_vroff_physics_flush_21236.md` — buffered consumer и generation.

Все выгрузки сделаны `player_movement_dispatch.py` по существующей IDB без
autoanalysis и без сохранения базы. Смысл полей проверялся по инструкциям;
ошибочные/неполные типы Hex-Rays не используются как самостоятельное доказательство.

## 10. Pitch/yaw → AnimFeature_FPPCamera → animation input

Native feature `[F+0x360]=0x1C7A28D4F80`, vtable `0x142B02FF8`.
Getter type `0x140CE8B20` возвращает `qword_143429248`, которое регистратор
`0x14177E19C` связывает с **`animAnimFeature_FPPCamera`**. Alias и точные
property offsets задаёт **`0x1410B4A54`** (`AnimFeature_FPPCamera`).

| Offset feature | Имя из регистратора |
|---|---|
| `+0x40` | `fov` |
| `+0x44` | `deltaYaw` |
| `+0x48` | `deltaYawExternal` |
| `+0x4C` | `deltaYawInput` |
| `+0x50` | `yawSpeed` |
| `+0x54/+0x58` | `yawMaxLeft/yawMaxRight` |
| `+0x60` | `deltaPitch` |
| `+0x64` | `deltaPitchExternal` |
| `+0x68` | `deltaPitchInput` |
| `+0x6C` | `pitchSpeed` |
| `+0x70/+0x74` | `pitchMin/pitchMax` |
| `+0x78/+0x7C` | `resetYawSpeed/resetPitchSpeed` |
| `+0x84` | `isSceneMode` |
| `+0xC2` | `sceneTransitioningToGameplay` |
| `+0xC4/+0xC8` | `yawMultiplier/pitchMultiplier` |

### 10.1 Обычная и переключаемая ветви

`0x1403F4E88(F)` проверяет `F+0x3F6` и
`feature.sceneTransitioningToGameplay`. Если обе проверки false, в
`0x1403F32A4` выполняется проверенный путь:

1. `F+0x4E4` и `feature.deltaYawInput` получают итоговый input yaw.
2. `F+0x4E8` и `feature.deltaPitchInput` получают input pitch.
3. Та же итоговая yaw-дельта записывается в **`P+0x9C`**.
4. Перед `0x1404E1638` регистр XMM2, соответствующий yaw-аргументу,
   обнулён (`0x1403F3648`), а XMM3 получает pitch.

Это различие **input yaw** и **yaw аргумента для camera animation**, а не
предположение, что все поля yaw всегда нулевые. В другой ветке `0x1403F3727`
yaw передаётся в camera update иначе. Условия этой ветки перечислены, но
scene transitions здесь не воспроизводились.

`0x1404E1638` вызывает `0x1404E0D88` с дополнительными коэффициентами.
`0x1404E0D88` записывает `deltaYaw/deltaPitch`, external-вклады,
`yawSpeed/pitchSpeed` (delta / timestep при достаточном dt), limits и
state-dependent corrections, затем ставит **`F+0x40A=1`**.
Она читает обратно результаты анимации для корректировок; это не просто store
угла в world quaternion камеры.

### 10.2 Подтверждённое вертикальное движение мыши

Пользователь сообщил **«Поднял, остановилось»**. Watchpoint на
`feature.deltaPitchInput` поймал store `0x1403F3631`, stop `0x1403F3636`:

- `RCX=F`, `RAX=feature`;
- `deltaPitchInput` и `F+0x4E8`: bytes `3333B33E`, примерно **+0.35**;
- `deltaYawInput` и `F+0x4E4`: bytes `CCCCCCBD`, примерно **−0.1**;
- на этом раннем останове `deltaPitch` ещё 0: последующий `0x1404E0D88`
  не был выполнен для этого вызова.

Хотя просили движение вверх, измерение содержит ненулевой yaw. Поэтому оно
не используется как доказательство perfectly pitch-only stimulus или
неизменности entity yaw на протяжении всего действия.

### 10.3 Публикация feature

Watchpoint на очистку **`F+0x40A`** нашёл `0x1404E3C57`, внутри
**`0x1404E3AD4`**. Полный код показывает:

```text
если F.dirtyFeature(+0x40A):
  подготовка scene/override/parallax/normalizeYaw полей
  target = [F+0x370]
  handle = [F+0x360/+0x368]
  name = 0x140BEB8CC() -> "camera", hash 0x6FCFDF926F11594E
  0x1404E3C48 -> 0x1402E0E3C(target, name, handle)
  0x1404E3C57: F.dirtyFeature = 0
```

Текущий target **`0x1C7A17D8030`**, vtable `0x142ABE1A8`;
его нельзя подменять указателем A только на основании похожего назначения.
`0x1404E3AD4` вызывается, в частности, из camera interface update
**`0x1404E374C`**. У последней **this = F+0x120**, поэтому её `+0x240`
соответствует `F+0x360`, а `+0x2EA` — `F+0x40A`.

### 10.4 Обратное чтение animation outputs

`[F+0x380]=0x1CB9861ABC0` — другой объект, не target feature и не A.
`0x1404E2968` и `0x1404E32C8` через **`0x1404E339C`** читают значения:

| Getter | Имя | Hash |
|---|---|---|
| `0x1404E35E4` | `PitchInput` | `3078681C95C74787` |
| `0x1404E3598` | `PitchRef` | `2CF4EF5343FEA096` |
| `0x1404E354C` | `YawInput` | `2D71DA6217F9C35C` |
| `0x1404E3500` | `YawRef` | `8549F5ED0064D015` |

`0x1404E339C` получает animation state из `component+0x178` либо через
`0x1402E9898`, затем pose descriptor `state+0x18`. В descriptor читает массив
`+0x28`, count `+0x34`, записи **stride 16** `{name, floatValue, ...}`.
Это отдельный именованный output-канал; его нельзя отождествлять с массивом
костей. В live-чтении cached state `feedbackComponent+0x178` был null, так что
нужен fallback. Ниже, в разделе 15, подтверждён реальный источник `PitchInput`:
это named outputs именно D.

## 11. Один W request прослежен через CCT по точному совпадению данных

В JSON это `chain=w-request-1`, ordinal 1–4. Каждый следующий watchpoint
был подготовлен **до resume** предыдущего останова. Это последовательность
наблюдений одного сопоставленного запроса; engine frame ID отдельно не записан.

1. Пользователь: **«Нажал, остановилось»** после W без мыши. Stop `0x1406ABA98`,
   после записи всех XYZ в P:
   `9D426BBCE5E65A3B0F0721BC`.
2. Следующая точка на property 9 требовала побитового совпадения **всех XYZ**.
   Она сработала на `0x14092608D`, `RCX=B`, `R9=9`.
3. После продолжения пойман следующий `0x1402BF860` на B: CCT обновил позицию,
   началась очистка pending movement.
4. Следующая поздняя запись S: `0x1403367D5`, return `0x140D0F35C`.

Из payload и context:

```text
request delta = (-0.014359143562614918,
                  0.0033401784021407366,
                 -0.00982834305614233)
dt = 0.024784499779343605   (bytes DD08CB3C)
```

В consumer payload находился по `0x1DB9AF203D4`, command record
`0x1DB9AC20448`, buffer `0x1DB9AC20000`. Поле timestep B+0x144 совпало с
context dt; после CCT B+0x140 содержит тот же dt.

Позиция B после CCT:
`25CEF443D91616C569FB2E43` (float32 XYZ).
Её перевод `int(float32 * 131072)` **по всем трём компонентам** совпал с
новой fixed-point позицией S/T.local:
`9438D303E0243DEDD2F65D01`.
Эта связь проверяется offline-валидатором, а не только визуальным сравнением.

На stop `0x1402BF860` очищены X/Y pending request, но Z ещё содержит старое
значение: останов расположен до последнего scalar store. Это не «зависший»
остаток запроса.

## 12. Late movement update и world transform — разные этапы

Текущий **`S+0x1FD=1`** (read `S+0x1F8`: `0000000001010000`).
В `0x1403362F4` после provider publication немедленный вызов `0x140336390`
происходит только при `S+0x1FD==0`. Следовательно, наблюдавшийся игрок
использует отдельный поздний путь.

Live return из `0x140336390` — **`0x140D0F35C`**:

```text
0x140D0F2F0: range job по массиву component pointers
  0x140D0F356: [component.vtable+0x240](component, dt)
    S.vtable[+0x240] = 0x140336390
```

Job wrapper цепочка `0x140B9BB8C → 0x140B9BBD8 → 0x140B9BC90` регистрирует
именно callback `0x140D0F2F0` и явное имя профайлера:
**`EntityPhysics/ApplyPostCharacterControllerTransforms`**.
Это подкрепляет связь позднего обновления с фазой после CCT; одно только имя
не используется вместо анализа зависимостей scheduler.

### 12.1 Фактически пойманное промежуточное состояние

На ordinal 4 W-цепочки, **в одном останове**:

| Данные | Состояние |
|---|---|
| `S+0x1C0/+0x1D0` | новая позиция/rotation |
| `T+0xC0/+0xD0` (local) | побайтово совпадают с S |
| `T+0xE0/+0xF0` (world) | ещё предыдущий transform |

Причина по инструкциям: `0x1401DC0E0` пишет **local** поля T, затем dirty
notification через **`0x1401C9760`**. Последняя работает с entity, ставит
`entity+0x150=1` (atomic exchange) и при первом включении добавляет
`entity+0xB0` через `0x1401CA050` в очередь transform system.

Таким образом, даже после обновления capsule и копии состояния S world fields
компонентов могут ещё не отражать новую позицию. Номер поля не заменяет знание
границы фазы.

### 12.2 Конкретный world commit root T

На **отдельном** повторном W watchpoint T+0xE0 поймал:

- store **`0x14068E258`** внутри **`0x14068E1F8`**, stop `0x14068E260`;
- `RCX=RBX=T`, return **`0x1401C94B3`**;
- в момент останова скопированы только world X/Y; world Z и quaternion
  записываются следующими инструкциями `0x14068E264/0x14068E26A`.

Подтверждённая цепь:

```text
0x140B53E3C (range job)
  -> 0x1401C9430 (weak handle, enabled/transform flags, binding lookup)
    при отсутствии parent binding:
      -> 0x14068E1F8(T)
         T.world = T.local
         при изменении/dirty: virtual notification [+0x240]
         -> 0x1401D8558(T, flags) — распространение на детей
```

При наличии parent binding `0x1401C9430` идёт по другой ветке через
binding getter и `0x1401D74FC`. Её нельзя заменять root-копированием local→world.

## 13. Дополнительные источники этого продолжения

В `render_camera_RE/analysis/`:

- `native_vroff_camera_feature_type_21236.md`
- `native_vroff_camera_feature_names_21236.json`
- `native_vroff_camera_feature_fields_21236.md`
- `native_vroff_camera_input_and_flush_job_21236.md`
- `native_vroff_fpp_feedback_21236.md`
- `native_vroff_camera_feature_publish_21236.md`
- `native_vroff_camera_feature_tick_21236.md`
- `native_vroff_feature_submission_and_job_21236.md`
- `native_vroff_pre_post_movement_dispatch_21236.md`
- `native_vroff_post_cct_jobs_and_anim_target_21236.md`
- `native_vroff_component_late_update_21236.md`
- `native_vroff_move_physics_order_21236.md`
- `native_vroff_physics_flush_scheduling_21236.md`
- `native_vroff_root_world_commit_21236.md`
- `native_vroff_dirty_world_dispatch_21236.md`

Проверки raw bytes, offsets, CName hashes, W payload/dt, CCT→local conversion,
частичных stores и путей артефактов:
`render_camera_RE/scripts/verify_native_movement_evidence.py`.

## 14. A.world → slots H: подтверждён отдельный binding pass

Входящие attachments компонента ищутся в `[component+0x70]`, а количество
проверяемых первых записей задаёт uint16 `+0x80`. Это подтверждает
`0x1401C9CF0 → 0x1401C988C`. Поле `+0x90` не заменяет этот поиск.

У H первый attachment — **`0x1C789350D60`**:

- vtable `0x14307EA98`, getter `+0xD8 = 0x1401D92A0`;
- `binding+0x30 = A`, `binding+0x40 = H`;
- flags `binding+0x50 = 0x0A`;
- local transform H — zero/identity в отдельном live-чтении.

Watchpoint **на H+0xE0** поймал store `0x1401D7581` в `0x1401D74FC`.
Функция делает три push до `mov rbp,rsp`, поэтому return address на этом
останове прочитан из сохранённого stack snapshot по **RBP+0x18**:
**`0x1401D979A`**, вызывающая **`0x1401D9528`**.

Текущая запись этого прохода (`0x1CBAA51BE48`, stride 40):

```text
+0x00 H weak handle
+0x10 A parent pointer
+0x18 binding 0x1C789350D60 weak handle
```

В `0x1401D9528` код действительно вызывает `binding.vtable[+0xD8]` с parent
из `record+0x10`, затем `0x1401D74FC(H, slotWorld)`. После своей world-записи
`0x1401D74FC` распространяет изменения на детей через `0x1401D8558`.
Среди детей H находится F с отдельным bone-slot binding, установленным в §4.

Вызывающий animation pass: **`0x141CA3720 → 0x1401D9528`**, call
`0x141CA3855`. Эта связь статически подтверждена. Её полный scheduler dependency
относительно pose update ещё не заменяется предположением «сразу после».

## 15. Обратное чтение PitchInput замкнуто до конкретного pose buffer

У D (`0x1C7967B2720`) named outputs:
`[D+0x28]=0x1CBA634AAF0`, count `[D+0x34]=29`.
В границах этого массива найден **entry 6**, `0x1CBA634AB50`:
hash `3078681C95C74787` (`PitchInput`).

Watchpoint **на F+0x430**, отфильтрованный на законченный value store,
сработал после `0x1404E3408`, внутри `0x1404E339C`:

```text
RCX = 0x1CBA634AB50   (source entry в D.named outputs)
RDX = 0x1CBA634ACC0   (конец этого массива)
RBX = 0x3078681C95C74787
RSI = F+0x430        (destination)
source+8 == destination == bytes 80FFFF3F
```

Это реальное чтение camera input feedback из того же pose descriptor D,
который даёт model bones для camera binding. Само значение не отождествляется
с единственной delta `+0.35` из более раннего пользовательского действия.

## 16. Native camera animation node

Статически найдена vtable **`0x142C9A728`**, подтверждённая constructor
`0x14132AEFC`, с методами:

| Slot | Функция | Подтверждённая часть |
|---|---|---|
| `+0xE0` | `0x140BE9AE8` | связывает camera feature properties и именованные camera/torso bones |
| `+0x120` | `0x140179688` | подготовка persistent node state через `0x140179804`, регистрация именованных outputs |
| `+0x128` | `0x1404E2284` | обработка pose с подготовленным node state |

В `0x140BE9AE8` присутствуют точные camera joint names из §5 и ссылки на
`camera.deltaYaw/deltaPitch`, limits, reset speeds, upperbody weights, parallax,
scene/vehicle flags. Эта функция подготавливает bindings; её нельзя называть
самой покадровой интеграцией углов.

**Live entry `0x1404E2284`:** объект `0x1CB98B67CD0`, его vtable побайтово
соответствует `0x142C9A728`. Runtime node state вычислен так же, как в коде:
`[R8] + uint32[node+0x68] = 0x1CB9CC72150`, offset `0xD60`.
Выходной аргумент R9 — `0x10C1AFDE70`. Это подтверждает исполнение экземпляра
узла; не все его blend/scene/vehicle ветви воспроизведены.

В конце pose-метода **`0x1404E2418`** переносит state values в scalar channels
выходной pose и отмечает соответствующие биты валидности:

| Persistent state offset | Output name |
|---|---|
| `+0x1BC` | `YawInput` |
| `+0x1B4` | `YawRef` |
| `+0x1C0` | `PitchInput` |
| `+0x1B8` | `PitchRef` |

Интеграция/clamp/reset дополнительно разобраны ниже. Сопоставление каждого
промежуточного bone pose в полной цепи blend/constraints/root-pose merge
ещё не считается исчерпанным.

Дополнительные артефакты:

- `native_vroff_deferred_binding_pass_21236.md`
- `native_vroff_anim_finalize_and_camera_node_21236.md`
- `native_vroff_camera_node_vtable_21236.md`
- `native_vroff_camera_node_execution_21236.md`
- `native_vroff_camera_node_state_math_21236.md`

## 17. Persistent angles внутри camera node

`0x140179804` собирает значения по input bindings и вызывает, в частности,
**`0x14017A8EC` (pitch)** и **`0x14017A73C` (yaw)**.

| Offset runtime node state | Смысл по чтениям/записям |
|---|---|
| `+0x1AC` | основной yaw accumulator |
| `+0x1B0` | основной pitch accumulator |
| `+0x1B4` | yaw reference contribution, публикуется как `YawRef` |
| `+0x1B8` | pitch reference contribution, публикуется как `PitchRef` |
| `+0x1BC` | итоговый `YawInput` |
| `+0x1C0` | итоговый `PitchInput` |

В обычной ветке без reset/override:

```text
mainPitch = clamp(wrap180(mainPitch + params[+0x08]), params[+0x14], params[+0x18])
pitchRef  = wrap180(pitchRef + params[+0x40])
PitchInput = mainPitch + pitchRef

mainYaw += params[+0x00]
если params[+0x93]: mainYaw = wrap180(mainYaw)
mainYaw = clamp(mainYaw, params[+0x0C], params[+0x10])
yawRef = wrap180(yawRef + params[+0x3C])
YawInput = mainYaw + yawRef
```

Здесь `params` — **собранный input block узла**, не `AnimFeature_FPPCamera`
с теми же offset. Наличие flag `params+0x94`, reset events, активного reset
или override `params+0x92` переключает указанные пути. Полный код ветвей
сохранён; scene/vehicle варианты не заявлены как live-проверенные.

`wrap180` — **`0x14017AEA8`**:
`fmodf(angle+180,360)`, коррекция отрицательного остатка, затем `−180`.
Результат в `[-180,180)`.

Reset helper **`0x14017A880(float* angle, speed, dt)`**:

1. Если `abs(speed)` или `abs(angle) <= 1.1920929e-7`, возвращает false.
2. Иначе `step=abs(angle*speed*dt)`.
3. Если step достигает abs(angle), либо abs(angle)<0.1, angle становится 0;
   иначе уменьшается к нулю на step.
4. Возвращает true. На этом пути caller не прибавляет обычную delta.

**Live:** watchpoint state+0x1C0 поймал store `0x14017A9BB`, stop
`0x14017A9C3`, `RSI=0x1CB9CC72150`. В этом останове:

```text
mainYaw=0, mainPitch=bytes 80FFFF3F,
yawRef=0, pitchRef=0,
YawInput=0, PitchInput=bytes 80FFFF3F.
```

Сохранены проверенный префикс input block (48 bytes) и evaluation context.
Полная ручная транскрипция input block не прошла проверку длины и исключена;
её хвост не используется для выводов о flags. В сохранённом префиксе
обычные deltas и reset speeds равны 0; наблюдение подтверждает хранение и
суммирование состояния, а не реакцию на новый пользовательский жест.

## 18. Camera node работает с pose, а не с entity world quaternion

На live stop после `0x1404E2748` внутри sample `0x1404E2284`:

- native node тот же `0x1CB98B67CD0`;
- подготовленный runtime state тот же `0x1CB9CC72150`;
- state `+0x1F0=0`, `+0x1F4=1` → по инструкциям выбран путь
  **`0x1404E1E80`**;
- state bone IDs `+0x1FE=107`, `+0x200=73`, `+0x202=90`, `+0x204=73`,
  `+0x206=118`, `+0x208=119`, `+0x20A=125`, `+0x20C=131`;
- output pose header имеет **152 bones**, bone buffer берётся из `pose+0x30`.
  Это intermediate pose, не конечные 619 bones D.

В `0x1404E1E80` читаются model transforms Control 107 и Chest 90,
применяются camera/upperbody поправки, затем вызывается **`0x1404E20B8`**:

1. Для Chest 90 относительно parent 73.
2. Для Control 107 относительно parent 90.

`0x1404E20B8` получает parent model transform через `0x1401D8290`, затем
`0x14017F458` пишет target local transform в `pose.bones + index*48`.
Таким образом, эта подтверждённая ветвь camera node воздействует на локальные
transforms управляющих костей. World quaternion entity ей не является.

Отдельно пойман вход в tail `0x1404E2418`, который публикует scalar outputs:
output pose также 152 bones; сохранены local transforms Aim 118 и Target 119.
Этот снимок не совмещается с предыдущим в мнимое before/after одного кадра.
Полный анализ дополнительных blend/constraint операций между sample и
конечным D продолжается.

## 19. Перенос именованных outputs в D — перед копированием костей

Watchpoint на **`D.namedOutputs[PitchInput].value`** поймал store
**`0x14028EF34`** внутри append helper `0x14028EED0`.

- Destination container `RCX=D+0x28`.
- Destination data `R9=0x1CBA634AAF0`, old count `R8=6`, new count 7.
- Записанный entry: `PitchInput`, bytes value `80FFFF3F`.
- Return address из stack snapshot, проверенный по prologue (3 push + `sub rsp,0x30`):
  **RSP+0x48 → `0x1401E1BA2`**.

Вызывающий — **`0x1401E10BC`**:

```text
0x1401E1B42..0x1401E1B49: source = pose.floatBuffer + 4*pose.regularFloatCount
цикл по дополнительным именованным outputs source pose:
  имя из таблицы output names
  float из source
  поиск имени в D.namedOutputs
  если имя отсутствует: 0x1401E1B9D -> 0x14028EED0
0x1401E1BB8 -> 0x14017DDB4 (bone/float copy)
```

**В этой merge-ветке существующее имя не перезаписывается**: переход сразу
к следующему source output. Порядок graph outputs поэтому имеет значение;
нельзя предполагать last-writer-wins.

Новая подтверждённая связь:
camera node scalar outputs → source pose named channels → D.namedOutputs →
`0x1404E339C` → F+0x430 (и остальные feedback angles).
Какая именно ветвь выбирается при дублирующемся output имени в другой сцене,
этим одиночным наблюдением не установлено.

Артефакты этого углубления:

- `native_vroff_camera_angle_integrators_21236.md`
- `native_vroff_angle_reset_and_dispatch_21236.md`
- `native_vroff_camera_pose_write_and_move_states_21236.md`
- `native_vroff_named_pose_output_merge_21236.md`

## 20. Связанный pitch trace: input → node → pose → world camera

В JSON это `chain=pitch-change-1`, ordinal 1–5. Следующая точка каждый раз
подготавливалась до resume. Исходный `PitchInput` был `0x3FFFFF80`.

| Шаг | Останов | Проверенные данные |
|---|---|---|
| 1. Input feature | `0x1403F3636` | input pitch `CCCCCC3D` ≈ +0.1; input yaw `CCCC4CBD` ≈ −0.05 |
| 2. Persistent node state | `0x14017A9C3` | `PitchInput = 0x40066640`; input block содержит ту же pitch delta |
| 3. D.namedOutputs | `0x14028EF3B` | `PitchInput` entry с **теми же битами `0x40066640`** |
| 4. Завершён local→model | `0x1404B6AA8` | весь bone loop завершён; записан checksum D+0x14; сохранена model кость 118 |
| 5. F.world | `0x1401D8A92` | новая world camera при том же named PitchInput; сохранены A.world, model bone и F.local |

`D+0x14` здесь — checksum pose, **не frame ID**. Данные его изменения служат
точкой завершения конкретного прохода, а не измерением частоты кадров.

### 20.1 Вход и накопление

```text
old mainPitch = 1.9999847412109375
input delta  = 0.09999999403953552
new PitchInput после float32 wrap180 = 2.0999908447265625
```

Offline-проверка воспроизводит округление float32 в сложении, `+180`, `fmod`,
`−180` и получает **ровно** `0x40066640`. Reference contribution в этом
наблюдении равен 0. Input block также показывает `deltaYaw=0`, при этом
`deltaYawInput≈−0.05`; вертикальный жест не трактуется как идеально изолированный
pitch без примеси yaw.

### 20.2 Модельная кость и камера

На последнем останове model bone 118 побайтово совпадает с сохранённой после
завершения FK. F.local — zero/identity.

Независимый расчёт из **снятых данных**, без вызовов игровых setters:

```text
expectedCameraQuat = normalize(A.worldQuat * bone118.modelQuat)
expectedCameraPos  = A.worldFixedPos
                   + quantize131072(rotate(A.worldQuat, bone118.modelPosition))
```

Результат проверки:

- quaternion maximum component error: **4.98×10⁻⁸** (учтена эквивалентность q/−q);
- разница fixed-point позиции по XYZ: **`(0,0,1)`**;
- один fixed-point шаг = **1/131072 m ≈ 7.63 μm**.

Этим подтверждён конкретный связанный путь от принятого pitch до F.world.
Он не заменяет ещё не собранный общий scheduler/frame-ID trace и не доказывает
те же branches для транспорта, катсцен или других locomotion states.
