# Locomotion slot → main pose → mapping → FK полного скелета

2026-09-15, VR-off **PID17268**, imagebase **`0x7FF7E34A0000`**.
Статика из совпадающей IDB, live через x64dbg.
Продолжение [clip blend и slot output](cp2077-native-locomotion-blend.md).

**Подтверждено:** выход Locomotion slot и конечная pose основного графа
попадают в один буфер; mapping переносит результат в общий скелет.
Затем туда же пишут FPP-деформации и shadow graph. Последующий FK для всех
**619 костей** численно воспроизведён из полной local pose и parent indices.

Raw evidence:

- [slot → main → FK](cp2077-native-main-fk-17268.json),
  W input `148bc29066af4f75966203c5b35e66ee`;
- [последовательные mapping passes](cp2077-native-main-map-17268.json),
  W input `cc85df71079f4ba2b5b435dc5d2e879e`;
- дополнительный targeted root-correction probe находится во втором JSON.

Это отдельные эпизоды. Внутри каждого основного trace сохранены границы
одного внешнего вызова `0x1401D2DB8`; разные эпизоды не названы одним кадром.

## 1. Объекты и фактический порядок

| Назначение | PID17268 pointer |
|---|---|
| Animation state K | `0x24C351E8980` |
| Rig R=`[K+8]` | `0x24C3A782A08` |
| Full pose descriptor D=`[K+0x18]` | `0x24C154A9D40` |
| Full bone buffer=`[D]` | `0x2503059D9C0` |
| Parent indices=`[R+0x10]` | `0x2503A124520` |
| Main graph instance | `0x2502F093E20` |
| Main graph | `0x24C367954B0` |
| Locomotion slot node | `0x24C36796FB0` |

Полный buffer — **619×48 = 29712 bytes**, parents — **619×int16**.
Число 619 заново проверено по descriptor/rig этого PID.

```text
0x1401D2DB8(K, context)
  loop по graph instances:
    0x1401D31F4 -> 0x140328934(graphInstance, graphContext)
      -> 0x1401E10BC
         -> main graph Sample
            -> Locomotion slot Sample
            -> оставшиеся узлы main graph
         -> 0x14017DDB4: graph pose -> full local pose
  0x1401D3230 -> 0x1404B6968(D, R)
  0x1401D3235: полный model-space результат
```

Начало graph evaluation и до/после FK пойманы с одинаковыми
**K и RSP=`0x78393FEB80`**. Это более сильная связь, чем совпадение pointers
в несвязанных снимках: кадры между этими точками не возобновлялись свободно.

## 2. Slot output не является final local pose основного графа

В первом trace:

1. После attached Locomotion Sample, **`0x1402C9B7F`**:
   descriptor=`0x25026C0C720`, transforms=`0x25026C0A8F0`, count153.
2. На входе **`0x14017DDB4`** для main graph:
   **тот же descriptor и тот же transforms pointer**, count153;
   graphContext=`0x78393FEC70`, return=`0x1401E1BBD`.
3. Между точками основной граф доработал pose in place:
   - translation XYZ изменена в **64/153** записях;
   - quaternion — в **72/153**;
   - scale XYZ — в **0/153**;
   - translation W — в **153/153**.

Поэтому полный 48-byte record не совпадает ни для одной записи. Это не
означает изменение всех геометрических transforms: XYZ/Q/S проверяются
отдельно, и смысл W-lane не присваивается.

Ресурсная topology после slot проходит через Switch/Join и дальнейшие
main-graph ветви. Конкретные writer nodes для всех 64/72 изменений пока
не сопоставлены live; здесь доказаны промежуточный output, тот же буфер
на конечной границе и дальнейший путь в mapping/FK.

## 3. Точная операция mapping

`0x14017DDB4(graphInstance, graphContext, sourceDescriptor, LOD)`:

```text
mapping = graphContext+0x30 -> pointer
destinationDescriptor = graphContext+0x38 -> pointer
sourceTransforms = sourceDescriptor+0x30 -> pointer
destinationTransforms = [destinationDescriptor]

pairs = [mapping+0x40]               // {uint32 src, uint32 dst}, stride8
count = uint16([mapping+0x50] + 2*LOD)
for pair in pairs[:count]:
    destination[dst] = source[src]  // 48 bytes
```

Статика: stores `0x14017DE27`, `0x14017DE33`, `0x14017DE3F`.
После основного copy loop функция также выполняет отдельные corrections для
некоторых корневых записей и копирует float tracks через другую таблицу.

Main LOD0 table содержит **151 pair**. Source indices **0 и 152** отсутствуют
в этом copy loop. Остальные copied records не размножаются в 619 элементов:
они пишутся в явно указанные destination indices полного скелета.

Во втором trace сняты source, весь destination до main map и весь destination
**сразу после return** `0x1401E1BBD`. Все **151/151** пары совпали побитово,
а целый 29712-byte destination восстановлен из previous buffer + copies.

### Исправленная after-call проверка

В первом trace guard на mapping return ошибочно использовал RBX для
graphInstance. Инструкции показывают: на этом return **RDI=graphInstance**,
**RBX=graphContext**. Поэтому тот первый after-call stop не был получен;
его данные у более поздней границы FK не названы immediate mapping return.
Во втором trace правильный RDI+RSP guard применён для каждого graph pass.

## 4. Почему mapped main pose меняется до FK

В том же внешнем вызове последовательно пойманы три evaluation/map пути:

| Graph | Source count | Copy pairs | Пересечение targets с main |
|---|---:|---:|---:|
| `player_base.animgraph` | 153 | 151 | — |
| `player_man_fpp_deformations.animgraph` | 183 | 182 | **66** |
| `shadow_rig_ma.animgraph` | 56 | 55 | **29** |

Полные пути двух дополнительных графов:

```text
base\characters\entities\player\deformations_rigs_ma\player_man_fpp_deformations.animgraph
base\characters\entities\player\shadow_rig_ma\shadow_rig_ma.animgraph
```

ResourcePath из `[graph+0x30]` разрешён WolvenKit и независимо перепроверен
FNV1a64: **16320CECC15A325C**, **EBB9897EFD33F895** соответственно.

После main map следующая запись в общий скелет приходит уже из другого
graphInstance. Поэтому сравнение main output с итоговым pre-FK buffer в
первом trace дало **85/151** полных совпадений. Это не ошибка mapping:
последующие writers были пропущены при таком прямом сравнении.

### Корневые corrections — не произвольное несовпадение

У FPP-deformations **181/182** итоговых mapped records совпадают с source
побитово. Исключение — pair **1→2**. Для shadow — **53/55**, исключения
**1→1** и **2→2**.

Отдельно поймано выполнение:

```text
0x14017DDB4
  -> 0x14017DFD4: собрать transform parent в текущей full pose
  -> 0x14017F458: выразить source относительно этого parent
```

На observed parent XYZ=0, quaternion=identity, scale=1, translation W=1.
В этих конкретных inputs correction изменяет только W:
**float32(source.W - 1)**; XYZ/Q/S остаются теми же. Результат после helper
пойман с graphInstance+RSP guard для deformations; для обеих shadow pairs
отдельно сняты helper entry, parent и source.

С учётом этих corrections **весь destination после каждого из трёх passes
восстановлен побитово**. Последний результат shadow map также полностью
совпал с буфером у вызова FK. Никакие остальные изменения не были
«объяснены» допуском сравнения или отброшены.

Объединение targets этих таблиц — **293 различных indices из 619**.
Это не означает, что оставшиеся кости отсутствуют: они уже находятся в
общем pose buffer. Их инициализация/другие режимы не приписываются этим
трём таблицам.

Счётчик внешнего loop на границе FK равнялся **4**, хотя evaluation/map
calls пойманы для indices0,1,2. Следовательно, это не доказательство
«в списке ровно три graph entries»; skipped entry отдельно не исследована.

## 5. Полный FK для 619 костей

В первом trace до call `0x1401D3230` и после него `0x1401D3235` сняты
**все 619 records одного и того же buffer** и parent array. Оба stops
ограничены тем же K и outer RSP. Реальный callee — **`0x1404B6968`**.

Независимый расчёт по иерархии:

```text
model.position[i] = model.position[parent]
                  + rotate(model.rotation[parent],
                           model.scale[parent] * local.position[i])
model.rotation[i] = normalize(model.rotation[parent] * local.rotation[i])
model.scale[i]    = model.scale[parent] * local.scale[i]
```

Roots parent=-1 сохраняют свои transforms. Номера родителей проверены на
валидность и топологический порядок. Проверяются геометрические XYZ/Q/S;
неизвестный смысл четвёртой position/scale lane в формулу не добавляется.

Максимальные абсолютные ошибки для всех 619 записей:

| Компоненты | Ошибка |
|---|---:|
| Position XYZ | **2.53e-7** |
| Quaternion XYZW | **1.72e-7** |
| Scale XYZ | **5.96e-8** |

Таким образом, во внешнем native вызове одновременно зафиксированы
Locomotion slot output, конечный main graph output, mapped full local pose
и её полный результат local→model. Последовательные mapping writers
уточнены отдельным сопоставленным вызовом.

## 6. Граница покрытия

Замкнуты границы **slot → main output → mapping → pre-FK → model pose**.
Открыты:

- остальные writer nodes между slot и main output; один writer Hips
  впоследствии проверен: [ParentConstraint от Torso_Hips_Driver_GRP](cp2077-native-main-hips-constraint.md);
- source/semantics для оставшихся graph entries и прочих состояний;
- текущий model pose → skinning/GPU в этом же кадре (прежнее skinning
  доказательство относится к другому trace);
- extracted root-motion → active movement provider/CCT.

Это расширяет подтверждённую штатную цепочку, но не является реализацией
roomscale или заявлением о полном исследовании всей анимационной системы.

## Артефакты

В `render_camera_RE/analysis/`:

- `main_pose_mapping_fk_sites_17268.md`, `deformation_root_correction_17268.md`;
- `main_slot_pose_consumers_17268.json`;
- `main_fk_chain_17268_slot_transforms.bin`, `main_fk_chain_17268_main_local_transforms.bin`;
- `main_fk_chain_17268_transform_map.bin`, `main_fk_chain_17268_full_parents.bin`;
- `main_fk_chain_17268_full_before_map.bin`, `main_fk_chain_17268_full_local_before_fk.bin`,
  `main_fk_chain_17268_full_model_after_fk.bin`;
- `main_map_chain_17268_main_source.bin`, `main_map_chain_17268_full_before_main.bin`,
  `main_map_chain_17268_full_after_main.bin`;
- `main_map_chain_17268_second_source.bin`, `main_map_chain_17268_second_map.bin`,
  `main_map_chain_17268_full_after_second.bin`;
- `main_map_chain_17268_third_source.bin`, `main_map_chain_17268_third_map.bin`,
  `main_map_chain_17268_full_after_third.bin`, `main_map_chain_17268_full_before_fk.bin`.

Проверка: `render_camera_RE/scripts/verify_native_movement_evidence.py`.
