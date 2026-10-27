# Spine AimConstraint → Spine1 PointConstraint

2026-09-16, VR-off **PID21660**, imagebase **`0x7FF6D5E00000`**.
[Raw evidence](cp2077-native-spine-constraints-21660.json).
Продолжение [Hips ParentConstraint](cp2077-native-main-hips-constraint.md).

**Подтверждено в одном вложенном Sample-вызове:** AimConstraint ориентирует
`Spine` на `Torso_Spine1_P_GRP`, затем PointConstraint ставит `Spine1`
в эту целевую точку. Полные153-entry local pose buffers сняты до, между
и после узлов; каждый шаг изменяет только свою запись.

## 1. Узлы и реальные dispatch targets

| Узел | Handle / nodesToInit index | Runtime pointer | Sample |
|---|---|---|---|
| Spine AimConstraint | 3732 / 2180 | `0x276B186DF10` | `0x1402C626C` |
| Spine1 PointConstraint | 3731 / 2179 | `0x2769720AC90` | `0x1402CD3A0` |

Pointers сопоставлены через снятый `main_graph_nodes_to_init_21660.bin`.
Vtables/field hashes проверены на actual runtime objects. Point input+0x58
указывает на этот Aim node; Aim input — ранее исследованный Hips constraint.

Оба узла используют static weight **1.0**. В ресурсе Aim задаёт forward axis
**+X**, up axis **+Y**; эти значения также сняты из compiled runtime axes.

Для данного Sample pose mapping и full-rig CName array подтверждают:

```text
pose index4  = Spine
pose index7  = Spine1
pose index94 = Torso_Spine1_P_GRP
parent[7]   = 4
```

Числа относятся к153-entry graph pose; их нельзя без mapping переносить на
любой иной rig. Имена здесь проверены через mapping к619-entry full rig.

## 2. Same-call trace

W input **`de1b45ea40474e5aafa00f2fd1acc386`**. Native MoveXY=(0,1).
Main state=`0x276A3B79F80`, output wrapper=`0x95EC9FDAA0`,
transforms=`0x27A9F88ED60`.

| Этап | IDA stop | Что снято |
|---|---|---|
| Aim math entry, после его input Sample | `0x1402CCF8C` | полный буфер до Aim, parents |
| После получения up point | `0x1402CD0DD` | Spine model, goal model, up point, axes |
| Перед model→local | `0x1402CD34A` | desired model и parent model |
| Aim вернулся в вызывающий Point node | `0x1402CD3E0` | полный буфер после Aim |
| Point вызывает solver | `0x1402CD4CB` | target7, source94, weight1 |
| Перед model→local Point | `0x1401DC42F` | desired model и actual parent model |
| Возврат solver Point | `0x1402CD4D0` | полный буфер после Point |

Aim entry RSP=`0x95EC9F1448`; возврат в caller — `0x95EC9F1450`.
Внутренние Aim stops имеют RSP=`0x95EC9F12D0`.
Point solver before/after сохраняет RSP=`0x95EC9F1440`,
frame RBP=`0x95EC9F1490`. Object/state/stack guards приготовлены до resume.
Свободных кадров между этими снимками не было.

Первоначально рассмотренный статический helper `0x1402C327C` не был принят
за actual dispatch по похожей математике. Runtime vtable привёл к
`0x1402C626C → 0x1402CCF8C`; именно этот путь измерен.

## 3. Aim: положение сохраняется, ориентация решается по двум направлениям

`0x1402CCF8C` получает model transforms Spine и goal через
`0x1401DB8BC`. Up point возвращается source-channel virtual call +0xE8.
Для численной проверки снят **результат** этого вызова; внутренности source
channel пока не объявлены восстановленными.

Независимо вычислено:

```text
forward = normalize(goal.position - spine.position)
up0     = normalize(upPoint - spine.position)
up      = normalize(up0 - forward * dot(forward, up0))

Q1 = shortestArc(+X, forward)
Q2 = shortestArc(rotate(Q1, +Y), up)
desired.rotation = normalize(Q2 * Q1)
```

Для captured nondegenerate directions это соответствует native двум
`0x1401D9A78` и quaternion composition. Максимальная component error
между независимым Q и снятым desired model quaternion — **1.77e-7**,
с учётом эквивалентных знаков quaternion.

Desired model XYZ и scale совпадают с исходным Spine model: Aim не задаёт
новую target position. Затем `0x1401DBB00` переводит transform в local-space
относительно actual parent model. В отличие от предыдущего Hips sample,
**parent здесь имеет нетривиальную ориентацию**. Inverse-rotation/translation
и quaternion product независимо проверены с component error <2e-6.

Полный pose diff до/после Aim:

- изменён только **index4**;
- геометрическая ориентация Spine изменена на **0.192212°**;
- незначительное отличие local XYZ после model→local — округление;
- прочие152 records побитово неизменны.

## 4. Point: weighted target point и обратное преобразование

Point node предварительно разрешил один source index **94**.
Actual span и массив preprocessedWeights сняты live; единственный weight
равен **1**. Solver **`0x1401DC280`**:

1. Получает model position source94.
2. Получает actual parent model Spine, уже после Aim.
3. Compose parent model × исходный local Spine1, сохраняя orientation/scale.
4. Заменяет model position на source point.
5. Переводит desired model обратно в local-space через `0x1401DBB00`.

Снятый desired model XYZ **побитово совпадает** с goal point Aim.
После Point, model position Spine1 независимо восстановлена из local pose
и hierarchy: ошибка относительно target — **8.49e-8 m**.

Полный pose diff после Aim → после Point:

- изменён только **index7**;
- local position смещена на **0.001482345 m**;
- orientation сохраняется геометрически в пределах float arithmetic;
- прочие152 records побитово неизменны.

### Важное различие quaternion helpers

Model-cache getter нормализует quaternion. Compose helper `0x1401D700C`,
используемый Point, возвращает **raw product** без нормализации.
Поэтому проверка выполняется двумя способами:

- raw desired Q сравнивается с actual parent Q × исходный local Q;
- нормализованный desired Q сравнивается с ориентацией model-cache расчёта.

Это предотвращает ложный вывод об изменении ориентации только из-за
отличающейся длины quaternion. Четвёртая translation lane, включая
инициализацию weighted accumulator, сохранена в raw и не интерпретируется
как геометрическая XYZ-компонента.

## 5. Значение для общей native цепочки

Теперь Hips ParentConstraint, Spine AimConstraint и Spine1 PointConstraint
имеют actual runtime consumers и численные проверки. Данный trace закрывает
**одну соседнюю пару** из ранее наблюдавшихся изменений между locomotion
output и конечной main pose; не все constraints тела.

Следующий resource consumer — AimConstraint, который ориентирует Spine1
на `Torso_Spine2_P_GRP`. Он затем проверен [отдельным связанным trace](cp2077-native-spine1-aim-sources.md),
включая actual up-channel и композицию goal/up positions из local hierarchy.
Writers самих управляющих transforms и active root-motion → movement/CCT
остаются отдельными задачами.

## Артефакты

В `render_camera_RE/analysis/`:

- `spine_constraints_resource_21660.json`;
- `spine_live_sample_dispatch_21660.md`, `spine_aim_point_math_21660.md`;
- `spine_chain_21660_before_aim.bin`, `spine_chain_21660_after_aim.bin`,
  `spine_chain_21660_after_point.bin`, `spine_chain_21660_parents.bin`.

Валидатор: `render_camera_RE/scripts/verify_native_movement_evidence.py`.
Point node header перечитан после trace для исправления ошибки копирования
base64; только type/link/static fields используются из этого отдельного
header snapshot. Динамические позы и same-call captures не заменялись.
