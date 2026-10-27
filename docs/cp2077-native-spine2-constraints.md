# Spine2 PointConstraint → AimConstraint к chest driver

2026-09-16, VR-off **PID21660**, imagebase **`0x7FF6D5E00000`**.
[Raw evidence](cp2077-native-spine2-constraints-21660.json).
Продолжение [Spine1 Aim и его источников](cp2077-native-spine1-aim-sources.md).

**Подтверждено:** PointConstraint устанавливает Spine2 в model-позицию
`Torso_Spine2_P_GRP`, затем AimConstraint поворачивает Spine2 к
`Torso_Chest_Driver_GRP`, используя `Torso_Spine2_Up_GRP` для up-point.
Оба узла измерены в одном вложенном Sample-вызове с полными промежуточными
local pose buffers и численной проверкой.

## 1. Identity и targets

| Назначение | Resource handle / nodesToInit index | Runtime pointer |
|---|---|---|
| PointConstraint Spine2 | 3729 / 2177 | `0x2769720ABC0` |
| AimConstraint Spine2 | 3728 / 2176 | `0x276B186E150` |
| Up TransformVector channel | 3835 / не отдельный pose node | `0x276A450C920` |

Actual vtable Sample:

- Point: `0x1402CD3A0`, solver `0x1401DC280`;
- Aim: `0x1402C626C → 0x1402CCF8C`;
- up-channel virtual slot +0xE8: `0x1402CCF28`.

Main state=`0x276A3B79F80`. Compiled mapping и full-rig CNames подтверждают:

```text
pose10  = Spine2
pose97  = Torso_Spine2_P_GRP
pose135 = Torso_Chest_Driver_GRP
pose100 = Torso_Spine2_Up_GRP
parent[10] = 7 (Spine1)
```

Оба constraint weights статические и равны1. Point имеет один source index97
с preprocessed weight1. Aim axes — +X forward, +Y up.

## 2. Порядок и same-call guards

W input **`c07d72f7843e472ba1ea6fa20ce4160a`**, native MoveXY=(0,1).
Output wrapper=`0x95ECDFDE80`, transforms=`0x27A9F84ED50`, count153.

| Этап | Stop | Состояние |
|---|---|---|
| Point после своего input Sample | `0x1402CD4CB` | source97, target10, полный before buffer |
| Перед Point model→local | `0x1401DC42F` | desired model и parent model |
| После Point solver | `0x1402CD4D0` | полный after-Point buffer |
| Aim math entry | `0x1402CCF8C` | отдельный полный buffer для проверки передачи |
| Goal getter return | `0x1402CD0AB` | target10 и goal135 model transforms |
| Up-channel resolved index | `0x1402CCF6C` | index100 |
| Up-channel return | `0x1402CD0DD` | фактический up-point vector |
| Перед Aim model→local | `0x1402CD34A` | desired model и parent model |
| Aim вернулся в caller | `0x1402C4BC1` | полный after-Aim buffer |

Point solver call/return имеют RSP=`0x95ECDF18D0` и
RBP=`0x95ECDF1920`. Aim entry RSP=`0x95ECDF1988`, внутренний frame=
`0x95ECDF1810`, final return=`0x95ECDF1990`. Разности stack addresses
согласованы с prologue/epilogue и вложенностью Sample calls.

Parent Aim input+0x58 указывает на наблюдавшийся Point node. Все153 records
после Point и на входе Aim **побитово совпали**. Между снимками не было
свободного выполнения другого frame.

## 3. PointConstraint: позиция источника → local Spine2

Solver разрешил span ровно из одного int16 source index97.
Desired model position сравнивается с независимо скомпозированной
model-позой `Torso_Spine2_P_GRP`: max XYZ error **6.04e-8 m**.

Model orientation/scale исходной Spine2 сохраняются, затем desired transform
выражается относительно actual parent Spine1 через `0x1401DBB00`.
Проверены raw quaternion composition без лишней normalization и
эквивалентность геометрической ориентации.

Численные ошибки обратного преобразования:

- local position: **1.97e-7 m**;
- local quaternion components: **4.62e-8**.

После solver изменилась только **local record10** из153.
Local XYZ displacement — **0.000207424 m** (0.207424 мм).
Остальные152 records побитово прежние.

## 4. AimConstraint: Spine2 → Torso_Chest_Driver_GRP

В том же parent Sample следующая фаза получила:

- model transform уже перемещённой Spine2;
- goal model transform index135;
- up-point, возвращённый TransformVector channel после разрешения index100.

Goal и target model transforms независимо проверены по after-Point local
buffer и parent array. Up-point XYZ совпал с model position index100
с max error **7.67e-8 m**. Ранее доказанная реализация TransformVector
возвращает position16; здесь зафиксированы resolved index и returned vector,
а не отдельный before-copy model snapshot.

Независимый Aim solve:

```text
forward = normalize(chestDriver.position - Spine2.position)
up0     = normalize(upPoint - Spine2.position)
up      = normalize(up0 - forward*dot(forward, up0))

Q1 = shortestArc(+X, forward)
Q2 = shortestArc(rotate(Q1, +Y), up)
desired.rotation = normalize(Q2*Q1)
```

Проверены nondegenerate направления. Desired model position и scale
сохраняются. Сравнение quaternion учитывает эквивалентность знака.

- Aim solve error: **6.88e-8**;
- model→local position error: **1.18e-7 m**;
- model→local quaternion error: **4.69e-8**;
- local orientation delta: **0.052451°**.

После Aim снова изменилась только **local record10**. Это утверждение о
local buffer: его изменения могут затем распространяться на model transforms
дочерних костей при FK.

## 5. Граница результата

Теперь подтверждена последовательность после Hips: Spine Aim → Spine1 Point
→ Spine1 Aim → Spine2 Point → Spine2 Aim. Разные этапы всей этой длинной
цепочки измерялись в разных trace; именно два Spine2 шага здесь связаны
одним вызовом.

Final stop находится в **ParentConstraint Spine3** сразу после его input
Sample. Собственное преобразование Spine3 затем проверено
[отдельным same-call trace](cp2077-native-spine3-constraint.md); оно не
приписывается данному Spine2 trace.

Writers управляющих transforms внутри графов и root-motion → movement/CCT
остаются отдельными открытыми участками.

## Артефакты

В `render_camera_RE/analysis/`:

- `spine2_constraints_resource_21660.json`, `spine2_up_channel_resource_21660.json`;
- `spine2_chain_21660_before_point.bin`, `spine2_chain_21660_after_point.bin`;
- `spine2_chain_21660_aim_entry.bin`, `spine2_chain_21660_after_aim.bin`;
- `spine2_chain_21660_parents.bin`.

Применённые static routines задокументированы в
`spine_live_sample_dispatch_21660.md`, `spine_aim_point_math_21660.md`,
`spine1_up_channel_dispatch_21660.md`.

Проверки: `render_camera_RE/scripts/verify_native_movement_evidence.py`.
