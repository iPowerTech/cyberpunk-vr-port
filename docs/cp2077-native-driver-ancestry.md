# Источник model-позы Torso_Hips_Driver_GRP

2026-09-16, VR-off **PID21660**, новый imagebase **`0x7FF6D5E00000`**.
[Raw evidence](cp2077-native-driver-chain-21660.json).
Продолжение [Hips ParentConstraint](cp2077-native-main-hips-constraint.md).

**Результат:** в пойманном W-вызове model-поза `Torso_Hips_Driver_GRP`,
которую использует Hips ParentConstraint, уже воспроизводится из позы на
выходе Locomotion slot. По всем предкам драйвера между slot и constraint
геометрические различия находятся в пределах float rounding.

Это endpoint comparison и проверка композиции, а не утверждение об
отсутствии промежуточных stores, взаимно компенсирующихся изменений или
других режимов графа.

## 1. Все объекты переобнаружены после перезапуска

```text
LocomotionSimple action     0x27693EEF620
player entity              0x27A9EA90F50
playerLocomotion feature   0x276B53B1BE0
main graph instance        0x27AAA3BF070
main graph                 0x276B186D2B0
main graph state           0x276A3B79F80
Locomotion slot            0x276B186DBB0
Hips ParentConstraint      0x276971C01C0
```

Main ResourcePath hash снова **`7C8EF3AC7AF95509`** (`player_base.animgraph`).
Runtime nodesToInit snapshot содержит2357 handles; indices169 и2181
снова соответствуют Locomotion slot и ParentConstraint соответственно.
Constraint vtable и hashes полей проверены на новом объекте.

## 2. Сопоставленный slot → driver getter

W input **`e0c16dbea9b74c7b9b97ada70c249dbf`**:

1. На `0x1402C9B7F` — выход Locomotion slot:
   - output wrapper=`0x95ECFFDE80`;
   - descriptor=`0x27AA1AA2080`;
   - transforms=`0x27AA1AA0250`, 153×48 bytes;
   - native MoveXY=(0,1).
2. Перед основным mapping boundary пойман `0x1402C4CC5`, после getter
   model transform для index133 в Hips ParentConstraint:
   - тот же output wrapper и transforms pointer;
   - target=2, driver=133;
   - снят второй полный local buffer и153 parent indices;
   - model result getter по `0x27AA1AA3AA0` сохранён отдельно.

Между этими stops игра не возобновлялась свободно. Breakpoint на main
mapping не сработал раньше driver getter; следующий frame не подставлялся
в сравнение.

## 3. Предки и проверенные имена

Порядок child→root по **снятой** parent array:

| Pose index | Parent | Имя или пока неразрешённый CName |
|---:|---:|---|
| 133 | 127 | Torso_Hips_Driver_GRP |
| 127 | 121 | EA00A539666F519C |
| 121 | 110 | Torso_Hips_RotatePoint_GRP |
| 110 | 105 | Torso_Hips_SpineRotatePoint_GRP |
| 105 | 89 | Torso_Hips_Control_Override_GRP |
| 89 | 73 | C6097732B3CE7265 |
| 73 | 0 | D207F57371564FB5 |
| 0 | -1 | unmapped root index; имя здесь не назначено |

Имена получены через **compiled mapping src→fullRigIndex**, затем actual
full-rig CName array, затем exact FNV1a64 match с ресурсными строками.
Не предполагалось, что graph pose index автоматически равен full rig index.

### Исправленный источник names

Для внутреннего runtime rig R массив имён расположен в **`[R+0x20]`**, count
в `R+0x2C`; это подтверждает `0x1401CA020`. В данном PID pointer=
`0x27AB8997CA0`, count619.
Первый exploratory dump по `[R]` оказался float-transform data, а не names;
он явно помечен rejected в raw JSON и не используется для разрешения имён.
Правильный массив сохранён отдельным файлом `*_full_bone_cnames.bin`.

## 4. Что изменилось между двумя local snapshots

В ancestry драйвера:

- максимальное изменение local position XYZ — **3.03e-7 m**;
- максимальное изменение ориентации — **2.55e-5°**;
- scale XYZ — без изменений;
- position W меняется примерно на1.3, поэтому целые records не объявлены
  идентичными. Геометрический смысл этому четвёртому lane не присваивается.

Часть quaternion representations меняет знак. Сравнивать их raw components
без учёта `q≡−q` означало бы ошибочно объявить большой поворот.

## 5. Независимая композиция model-позы

По parent chain от root до133 выполнены scale/rotate/translate composition
и normalization quaternion как у native `0x1401DB974`.
Результаты сравнены с **измеренным model transform** getter:

| Исходная local pose | Max XYZ error | Max quaternion component error | Sign alignment |
|---|---:|---:|---:|
| Locomotion slot output | **3.42e-7** | **2.24e-7** | −1 |
| Непосредственно до ParentConstraint | **4.38e-8** | **5.26e-8** | +1 |

Scale error=0 в обоих случаях. В первом случае сравнение quaternion
проводится с эквивалентным знаком; это совпадение ориентации, не bytes.

Следовательно, Hips ParentConstraint в наблюдавшемся режиме получает
геометрически уже подготовленную locomotion-иерархию драйвера и задаёт
по ней Hips. Предыдущий trace PID17268 отдельно доказал его actual local write.
Два PID не объединяются в один вызов.

## Граница и следующий участок

Нужны дальнейшие traces источников отдельных transforms этой иерархии
внутри locomotion graph и оставшихся main-graph constraints. Соседняя пара
Spine Aim / Spine1 Point затем проверена [отдельно](cp2077-native-spine-constraints.md).
Имена трёх предков пока оставлены как hashes.
Нет вывода, что root-motion автоматически переносит эту pose в CCT.

## Артефакты

В `render_camera_RE/analysis/`:

- `main_graph_nodes_to_init_21660.bin`;
- `driver_chain_21660_full_bone_cnames.bin`, `driver_chain_21660_transform_map.bin`;
- `driver_chain_21660_slot_local.bin`, `driver_chain_21660_pre_constraint_local.bin`;
- `driver_chain_21660_pose_parents.bin`, `driver_chain_21660_ancestry.json`.

`inspect_pose_ancestry.py` воспроизводит разрешение ancestry и сравнение local
transforms. `verify_native_movement_evidence.py` проверяет object links,
name hashes, обе model-композиции и input/restore logs.
