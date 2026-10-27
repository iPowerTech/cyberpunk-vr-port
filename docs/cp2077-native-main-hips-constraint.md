# Кто меняет Hips после Locomotion slot

2026-09-15, VR-off **PID17268**, imagebase `0x7FF7E34A0000`.
Этот процесс перезапущен пользователем после завершения probes.
Продолжение [slot → main pose → mapping/FK](cp2077-native-main-fk.md).

**Установлен конкретный writer:** `AnimNode_ParentConstraint`, который
задаёт model-позу **Hips** по **Torso_Hips_Driver_GRP**, затем переводит
результат в local-space относительно настоящего родителя Hips.

Raw evidence:

- [поиск writer](cp2077-native-main-writer-discovery-17268.json);
- [проверенный neutral additive](cp2077-native-main-additive-17268.json);
- [Hips ParentConstraint и сопоставленная математика](cp2077-native-hips-writer-17268.json).

## 1. Как отделялись записи от геометрических изменений

После return Locomotion slot поставлен write-watch на **X компоненты pose
index2**, с отдельным main-mapping breakpoint как верхней границей.

Первый stop — store `0x1401D70AE` в transform-compose helper. Он переписал
XYZ теми же bits, а W изменился с0 на0.3. Это не геометрическое перемещение.
Следующий квалифицированный X write имел очень малую additive translation.
Такой stop устанавливает helper и адрес буфера, но ещё не доказывает
семантически значимый writer.

В одном discovery stack присутствовал pointer MathExpressionPose handle96
(`renderPlane`), но он не был подтверждён actual argument вызывающего узла.
**По одному этому соседству writer не атрибутировался MathExpressionPose.**

Дальше отдельно пойман настоящий BlendAdditive caller и затем write Hips
с X вне заданного окна исходных float bits. Этот критерий касается одной
компоненты Hips: он не доказывает глобально первый writer всех костей.

## 2. Проверенный additive не меняет геометрию в этом вызове

Resource handle **1802**, nodesToInit index **98**, runtime
`0x24C367A6950`, type **AnimNode_BlendAdditive**, AGAT_Local.
Путь: `0x1402C7C68 → 0x1401D6E08 → 0x1401D700C`.

У main graph state+**0x8664** измерен weight **0.699999988**.
Resource weight node3363 — FloatComparator с trueValue0.7; actual pointer
сверен со входом BlendAdditive. Извлечены оба pose inputs и полный output
одного вызова: 153 transforms и 52 tracks.

Проверка local additive composition и float-track add прошла. При этом
**геометрия не изменилась ни в одной из153 записей**: добавочная поза в
данном sample нейтральна по XYZ/Q/S. Этот результат не переносится на другие
состояния узла и не используется как объяснение найденных ранее64/72 изменений.

## 3. Идентификация геометрического writer

Watchpoint Hips остановился после position store **`0x1401DBC6D`**,
в helper **`0x1401DBB00`**. После завершения текущего helper и возврата
зафиксирован actual caller **`0x1402C4DB1`**:

- RDI node=`0x24C3608C100`;
- R15 main state=`0x24C2A274100`;
- R12 target pose index=**2**;
- R13 source/driver pose index=**133**.

Runtime vtable **`0x142B394E0`** и узел по nodesToInit index **2181**
сопоставлены с resource handle **3733**, **AnimNode_ParentConstraint**.
Hashes полей независимо совпали с:

```text
node.parentTransformIndex = Torso_Hips_Driver_GRP
node.transformIndex       = Hips
weight                    = 1.0
useBoneReferencePoseAsDefaultOffset = false
offsetTranslationLS / offsetEulerRotationLS = disconnected
```

Соседние resource consumers включают AimConstraint для Spine и
PointConstraint для Spine1. Их запись впоследствии проверена в отдельном
[связанном trace PID21660](cp2077-native-spine-constraints.md).

## 4. Сопоставленный driver → desired model → local write

W input **`b8c3829f9e304625a210825111084457`**. Все основные stops имеют
node=`0x24C3608C100`, main state=`0x24C2A274100`, RSP=`0x78391F1090`.

1. **`0x1402C4CC5`**, после model getter для source index133:
   - RAX=`0x250268CE070`, сняты все48 bytes driver model transform;
   - снят полный153-entry local buffer, ещё до записи Hips;
   - сняты153 parent indices из sample context.
2. **`0x1402C4DAC`**, перед model→local helper:
   - desired model на stack **побитово совпадает** с driver model из пункта1;
   - actual parent model снят отдельно;
   - исходная local pose Hips ещё не изменена.
3. **`0x1402C4DB1`**, после helper, с тем же node+RSP guard:
   - снят local результат и полный153-entry buffer;
   - **изменена только запись2**, остальные152 записи побитово прежние.

### Проверка model-позы драйвера

Независимая рекурсивная композиция local transforms по снятым parent indices,
с нормализацией quaternion как в `0x1401DB974`, воспроизвела driver model:

- maximum position error **5.39e-8**;
- maximum quaternion component error **6.00e-8**.

При weight1, отключённых offsets и runtime byte node+0xC8=0 desired pose
равна driver model. Cached reference offset был прочитан для проверки layout,
но **на этом пути не применяется**: branch из assembler его обходит.

### Проверка local conversion

`0x1401DBB00` получает output local, actual parent model и desired model.
Геометрически:

```text
local.position = inverse(parent.rotation) * (desired.position-parent.position)
                 с учётом parent.scale
local.rotation = inverse(parent.rotation) * desired.rotation
local.scale    = desired.scale / parent.scale
```

В снятых inputs parent геометрически identity, поэтому выходные XYZ/Q/S
совпадают с desired model **побитово**. Четвёртая position lane имеет
ненулевые значения; её наблюдаемое вычитание сохранено, смысл ей не присвоен.
Этот sample не доказывает точность arbitrary rotated/scaled parent path.

Итоговое изменение local Hips относительно pose до constraint:

- position: **0.001155148 m**;
- orientation: **0.034228°** (учтена эквивалентность знака quaternion).

Ранее отдельный input `f1715b1afe684fa98ee19ba6e2f7c406` независимо проверил
входы/выходы той же conversion-функции, но не объединяется с driver-chain
input в один frame.

## 5. Значение для восстановленной native цепочки

Теперь изменение Hips после locomotion pose имеет подтверждённый узел,
источник и вычисление. Это не прямое копирование Hips из анимационного клипа
в final full pose: procedural driver и constraint находятся между ними.

Следующая [проверка ancestry в PID21660](cp2077-native-driver-ancestry.md)
воспроизвела model-позу драйвера уже из Locomotion slot output с float-level
точностью. Отдельные writers внутри locomotion graph и остальные pose
consumers остаются открытыми. Нельзя переносить результат одного
Hips на все64 изменённых translation/72 rotation из предыдущего trace.
Связь root-motion с movement provider также остаётся отдельной задачей.

## Артефакты

В `render_camera_RE/analysis/`:

- `main_graph_nodes_to_init_17268.bin`, `main_slot_pose_consumers_full_17268.json`;
- `main_first_additive_resource_17268.json`, `main_first_additive_inputs_17268.json`;
- `main_additive_pose_writer_17268.md`, `main_additive_caller_wrappers_17268.md`,
  `main_additive_weight_and_tracks_17268.md`;
- `main_additive_17268_a_transforms.bin`, `main_additive_17268_b_transforms.bin`,
  `main_additive_17268_out_transforms.bin`, соответствующие `*_tracks.bin`;
- `main_geometric_writer_identity_17268.json`, `main_geometric_writer_math_17268.md`;
- `hips_model_cache_math_17268.md`, `hips_model_cache_composition_17268.md`;
- `hips_constraint_chain_17268_local_before.bin`, `hips_constraint_chain_17268_local_after.bin`,
  `hips_constraint_chain_17268_parents.bin`.

Численные проверки включены в `verify_native_movement_evidence.py`.
