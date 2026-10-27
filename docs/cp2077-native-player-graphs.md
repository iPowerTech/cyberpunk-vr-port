# player_base → Locomotion slot → состояние и Sample клипа

2026-09-15. Статика: IDB соответствующего EXE и ресурсы из
`basegame_1_engine.archive`. Live: x64dbg, PID **1840**, **5684**, **17268**.
Снимки разных запусков и разных нажатий ниже не объединяются в один frame.

Raw evidence:
[PID 1840](cp2077-native-player-graphs-1840.json),
[PID 5684](cp2077-native-player-graphs-5684.json),
[PID 17268](cp2077-native-player-graphs-17268.json).
Предыдущий этап: [physics velocity → playerLocomotion](cp2077-native-locomotion-feature.md).

## 1. Найден реальный ресурс основного графа

Runtime AnimGraph имеет ResourcePath в **+0x30**:

| Ресурс | FNV1a64 |
|---|---|
| `base\gameplay\anim_graphs\player_base.animgraph` | `7C8EF3AC7AF95509` |
| `base\gameplay\anim_graphs\player_locomotion.animgraph` | `CF740A72B7A94FA5` |

Первое имя разрешено WolvenKit, затем оба файла извлечены из базового архива.
Проверка FNV выполнена независимо. Список `nodesToInit` в +0x78 содержит
16-byte handles; count в +0x84:

- **player_base: 2357** — совпадает с сериализованным ресурсом;
- **player_locomotion: 1128** — совпадает в трёх live-запусках.

Извлечённые binary/JSON находятся под
`render_camera_RE/analysis/native_resources/`.

`inspect_animgraph_resource.py` индексирует definitions по HandleId, отдельно
восстанавливает links и positions в nodesToInit. Runtime pointer получается
из **снятого массива handles**, после чего проверяются vtable, CName hashes,
compiled field offsets или animation name hash на конкретном объекте.
Равенство одного количества узлов не используется как достаточная identity.

## 2. Почему первоначальный speed-reader привёл к камере

В ограниченном дампе FloatInput allocation region PID1840 найдено пять
узлов `playerLocomotion.speed`, все они входят в nodesToInit и читают
`baseState+0x320`:

| Base graph node index | Непосредственный путь по ресурсу |
|---|---|
| 1725 | CurveFloatValue → DampFloat → camera-gated BlendAdditive |
| 914, 946 | FloatLatch, состояния `Bump` |
| 1458, 1528 | CurveFloatValue, состояния `locomotion_jump_start` |

Полный resource index дополнительно находит четыре initialized speed-input
за пределами этого allocation region и один serialized input, отсутствующий
в nodesToInit. Поэтому локальный поиск памяти не являлся полной переписью
всех consumers. FloatLatch читает вход при активации через `0x140C52EF4`,
а getter `0x140D1B770` возвращает уже сохранённый slot.

## 3. Locomotion — отдельный attached graph и отдельное состояние

В player_base **node index 169**, serialized handle **2125** —
`AnimNode_GraphSlot_Test`, name **Locomotion**, graph_TEST указывает на
`player_locomotion.animgraph`. Vtable **`0x142C1DFD0`**.

Это не только reference из файла. Пойман Update slot:

```text
0x1402C9BE0:
  slotId = parentState[node+0x70]
  найти 64-byte row в updateContext+0x70
  attachment = row+0x10
  0x1402C9CED -> 0x140AF4354(attachment,...)

attachment+0x00: attached graph
attachment+0x18: attached graph state
```

`0x140AF4354` собирает child update context и вызывает **`0x1402CA440`**,
используя graph и state из attachment. Slot `dontDeactivateInput`=true;
fallback pose и attached pose обрабатываются отдельно.

| PID | Locomotion graph | State | State memory |
|---|---|---|---|
| 1840 | `202B6958AB0` | `202AC4D9E00` | `206BB1367C0` |
| 5684 | `12596A92F90` | `125A1369A40` | `1299CBAE520` |
| 17268 | `24C358E5150` | `24C21D302C0` | `2502F643D60` |

**Speed slot здесь +0x24**, не +0x320 основного графа. Это проверено по
FloatInput fields и реальному read переходного condition. Передача всех
feature fields из main в attached state ещё требует отдельного same-call trace.

## 4. Фактический consumer скорости — условие перехода

Data watch на childState+0x24 поймал:

```asm
0x140C3B5C8 mov edx,[rcx+58h]       ; compiled offset 24h
0x140C3B5CB mov rax,[r8]            ; child state memory
0x140C3B5D1 movss xmm0,[rdx+rax]    ; speed
```

Condition object vtable=`0x142BA7328`; поля:

- +0x30: threshold **0.01999999955**, float32(0.02);
- +0x38: hash `playerLocomotion`;
- +0x40: hash `speed`;
- +0x48: comparison code **5**, assembly реализует **>=**;
- +0x58: byte offset **0x24**.

В W input **`4b7c8b5ce2f143c9aa56d9aef562cdd6`** PID1840:

1. Native MoveXY=(0,1), speed **0.3941130042**.
2. Пойман read указанного condition.
3. Same-call return **`0x1402CCD74`**, object+RSP guard, **AL=1**.

Caller `0x1402CCD24` обходит группу из трёх conditions с short-circuit AND.
Общий результат группы и конкретный переход в этом trace не сняты: следующий
guard не дал сохранённого останова. Нельзя объявить весь переход успешным
только по одному true condition.

## 5. При W реально обновляется idle_to_jog

PID17268 заново обнаружен через playerLocomotion publication, Q consumer и
Locomotion slot. После attach обнаружился старый **безусловный** breakpoint
на SkAnim.Update; он удалён до discovery. Его первый hit не использован как
доказательство принадлежности игроку.

Затем `0x140327E54` фильтровался по **child state текущего игрока**:

| Наблюдение | Узел | Проверенная animation | Resource context |
|---|---|---|---|
| До W | index22, handle80 | `fpp_idle_stand` | Ground / Idle |
| W, input `8161e43623fa43baa535c3fc13146b11` | index209, handle520 | `fpp_idle_to_jog_270` | Ground / idle_to_jog |

Имя проверено и в resource, и по CName **node+0xC8**. Sample next input
поймал также index215/handle526 **`fpp_idle_to_jog_0`**, а следующий —
index216/handle527 **`fpp_idle_to_jog_090`**.

Это несколько ветвей blend topology. Наличие `_270` при W само по себе не
означает ошибочное направление: вызовы Update/Sample не доказывают ненулевой
итоговый вес конкретного клипа. Resource содержит BlendMultiple/Blend2;
их runtime weights на момент этого этапа ещё не были записаны.
Следующий [связанный blend trace](cp2077-native-locomotion-blend.md) измерил
конкретный вклад пары клипов и переход до slot output.

## 6. Sample: от clip entry к полному выходному буферу

Sample vtable slot `+0x128` для SkAnim/SkSpeedAnim ведёт в **`0x140322E2C`**:

```text
node state = [graphState] + uint32(node+0xA0)
clip id = nodeState+0x48
0x1401D2584 resolves clip/cache entry
sample time = nodeState+0x40
0x140322E9B -> 0x1402CED70
0x140322EA0: check returned AL
```

`0x140245148` передаёт внутреннему sampler **полные spans**, используя
descriptor+0x30/0x38 как pointers и +0x24/+0x28 как counts.

### Исправленное понимание descriptor

Первые DWORDs descriptor (152,23,1,29,...) участвуют в отдельных циклах
инициализации/масок. Они **не означают**, что запрос состоит только из
transform152 и tracks23..51. Реальные spans в зафиксированном вызове:

- **153 transforms × 48 bytes = 7344 bytes**;
- **52 float tracks × 4 bytes = 208 bytes**.

Первый output probe снимал только хвост и увидел identity. После проверки
assembler выполнен отдельный **полный** before/after dump.

### Полный сопоставленный вызов

Input **`e9fdbcf4e6874d7099606cf6902b5476`**, W 0.3 s:

- Node `0x24C29DEEB20`, **`fpp_idle_to_jog_090`**.
- Child state `0x24C21D302C0`.
- Перед call `0x140322E9B`: RSP=`0x7838FDD250`.
- После call `0x140322EA0`: тот же node, state, RSP и output pointers.
- Возврат **RAX=1**.
- Все **153/153 transform records** и **50/52 float tracks** отличаются от
  содержимого выходного scratch до вызова.
- Все float components после вызова конечны. Максимальное отклонение
  `|dot(q,q)-1|` — **0.0002441253**; raw rotations не нормализовались проверкой.

`before` означает содержимое переиспользуемого scratch перед Sample, **не позу
предыдущего frame**. Число изменившихся записей доказывает формирование выхода
этим вызовом, а не движение всех костей между кадрами.

SHA256 output dumps:

```text
transforms after: e87557bfb037daf3a75f91cd1325fd5078f549043bffd0ab31b3b122fbfd59ca
tracks after:     c8b4b16fb8b264debde13c845249ccbeb56350c1f7046ce28fd9d3853a36b8b6
```

Это pose конкретного клипа в locomotion graph. Её полное сопоставление с
619-bone final player pose, итоговые веса blend и последующий FK в **том же
вызове** ещё не замкнуты. Flag resource `applyMotion=1` сам по себе также
не доказывает, что этот root delta двигает CCT.

## Дальнейшая конкретная проверка

BlendMultiple/Blend2 и переход до root slot output впоследствии проверены
[отдельными связанными trace](cp2077-native-locomotion-blend.md).
Далее требуется связать slot pose с main graph pose/FK и отдельно
проследить actual root-motion result.

## Артефакты и воспроизводимость

В `render_camera_RE/analysis/`:

- `player_graph_nodes_to_init_1840.bin`, `player_graph_float_inputs_1840.bin`;
- `locomotion_graph_nodes_to_init_1840.bin`, `locomotion_graph_nodes_to_init_5684.bin`,
  `locomotion_graph_nodes_to_init_17268.bin`;
- `player_graph_float_input_inventory_1840.json`;
- `player_base_speed_resource_paths_1840.json`, `player_base_slots_and_speed_nodes_1840.json`;
- `player_locomotion_resource_speed_paths_1840.json`, `player_locomotion_resource_speed_paths_5684.json`;
- `player_locomotion_active_clip_identity_17268.json`, `player_locomotion_w_clip_identity_17268.json`,
  `player_locomotion_w_sample_identity_17268.json`, `player_locomotion_w_output_identity_17268.json`;
- `player_locomotion_graph_slot_dispatch_1840.md`, `player_locomotion_graph_slot_state_1840.md`,
  `player_locomotion_attached_update_sample_1840.md`;
- `locomotion_state_speed_condition_1840.md`, `locomotion_skspeed_update_5684.md`,
  `locomotion_clip_sample_backend_17268.md`, `locomotion_pose_sampling_layout_17268.md`;
- `loco_sample_17268_transforms_before.bin`, `loco_sample_17268_transforms_after.bin`,
  `loco_sample_17268_tracks_before.bin`, `loco_sample_17268_tracks_after.bin`.

Валидатор `render_camera_RE/scripts/verify_native_movement_evidence.py` проверяет
byte lengths, node-array sizes, resource/animation hashes, field offsets,
same-call guards, input logs, полные pose spans и их численные характеристики.
