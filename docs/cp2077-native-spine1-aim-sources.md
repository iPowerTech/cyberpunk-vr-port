# Spine1 AimConstraint: цель и источник up-point

2026-09-16, VR-off **PID21660**, imagebase **`0x7FF6D5E00000`**.
[Raw evidence](cp2077-native-spine1-aim-21660.json).
Продолжение [Spine Aim → Spine1 Point](cp2077-native-spine-constraints.md).

**Подтверждено:** следующий AimConstraint поворачивает Spine1 к
`Torso_Spine2_P_GRP`. Его up-point получен actual source-channel вызовом
из model position кости `Torso_Spine1_Up_GRP`. Обе model-позы независимо
воспроизведены из снятой local pose и parent hierarchy.

## 1. Identity и параметры

| Объект | Значение |
|---|---|
| Aim resource handle / nodesToInit index | 3730 / 2178 |
| Runtime node | `0x276B186E030` |
| Main graph state | `0x276A3B79F80` |
| Target pose index | **7 = Spine1** |
| Goal pose index | **97 = Torso_Spine2_P_GRP** |
| Up-point pose index | **99 = Torso_Spine1_Up_GRP** |
| Static weight / axes | **1.0 / +X forward, +Y up** |
| Up-channel pointer | `0x276A450E2B8` |
| Up-channel resource handle / type | 3830 / `AnimNodeSourceChannel_TransformVector` |

Узел сверён по actual node-array entry, vtable и field hashes. Имена pose
indices сверены через compiled mapping и full-rig CName array, а не по
номерам из другого rig. В parent resource присутствует weighted target
channel, но его наличие не принято за доказательство вызова: наблюдавшийся
goal path использует непосредственно `targetTransformIndex`.

## 2. Связанный trace

W input **`cb6443e7522a4245abde84ba82e1d7ea`**, native MoveXY=(0,1).
Полные153-entry buffers сняты до/после. Output wrapper=`0x95ECEFD940`,
transforms=`0x27AA1CA02D0`, pose count153.

```text
0x1402CCF8C: Aim math entry, после input Sample
0x1402CD0AB: goal model getter вернулся, resolved goal index97
0x1402CCF6C: up-channel разрешил index99, вызывает model getter
0x1402CCF71: up model готов, до copy
0x1402CD0DD: up-channel вернулся в Aim
0x1402CD34A: перед переводом desired model -> local
0x1402CD3E0: Aim вернулся в вызывающий Point node
```

Aim entry RSP=`0x95ECEF1398`, внутренний frame=`0x95ECEF1220`;
up-channel frame=`0x95ECEF11E0`. Final return RSP=`0x95ECEF13A0` —
ровно entry+8. Object/state/stack guards ставились до продолжения вызова.
Свободных кадров между этими этапами не было.

## 3. Goal: resolved transform index → model pose

`0x1402CCF8C` разрешает поля target/goal через `0x1402CE0E0`.
На `0x1402CD0AB`:

- R15=7 — Spine1;
- RBX=97 — goal index;
- RDI указывает на model transform Spine1;
- RAX указывает на model transform `Torso_Spine2_P_GRP`.

Оба getter используют `0x1401DB8BC`, который при необходимости строит model
transform по parent hierarchy. Независимая композиция из полного captured
local buffer дала для goal max XYZ error **6.09e-8 m**.

Это доказывает источник goal в данной pose. Writer, который ранее создал
local transform97 внутри graph/clip, этим trace отдельно не атрибутирован.

## 4. Up-channel возвращает позицию кости, а не готовое направление

Actual virtual slot **+0xE8** source-channel ведёт в **`0x1402CCF28`**:

```text
index = resolve(channel.transformIndex)
если index валиден:
    model = getModelTransform(index)
    output16 = model.position16
иначе:
    output16 = zero
```

Valid branch пойман до model getter: index=**99**.
После getter source model pointer=`0x27AA1CA34C0`, output vector=
`0x95ECEF12F0`. Точные инструкции copy:

```asm
0x1402CCF71 movups xmm0,[rax]
0x1402CCF74 mov rax,rbx
0x1402CCF77 movups [rbx],xmm0
```

На возврате в Aim **все16 bytes output совпали с первым вектором model
transform**. Снимок stack до записи содержит прежние временные данные и
не называется «предыдущим up vector».

Model position кости99 независимо воспроизведена по local hierarchy с
max XYZ error **7.75e-8 m**. Aim затем вычисляет направление вверх через
`upPoint - spine1.position`; source-channel сам нормализованный up-direction
не возвращает. Четвёртая lane сохранена для проверки copy, её геометрический
смысл не назначается.

## 5. Aim solve и результат

Независимо построены:

```text
forward = normalize(goal - Spine1.position)
up0     = normalize(upPoint - Spine1.position)
up      = normalize(up0 - forward * dot(forward, up0))
```

Две shortest-arc rotations ориентируют +X на forward, затем повернутый +Y
на projected up. Nondegenerate direction checks пройдены для этих данных.
Составленный quaternion сравнивается с actual desired model rotation с
учётом `q == -q`:

- Aim solve component error — **4.67e-7**;
- model→local position error — **1.31e-7**;
- model→local quaternion error — **3.22e-8**.

Результат после Aim:

- только **pose index7** изменён из153 records;
- orientation delta **0.055748°**;
- desired model position и scale сохранены;
- остальные152 records побитово прежние.

Raw quaternion знаки до/после могут отличаться, поэтому sign change не
интерпретируется как разворот на180°. Нет переноса измеренного delta на
другие кадры или состояния.

## Граница результата

Теперь проверены Hips ParentConstraint, Spine Aim, Spine1 Point и следующий
Spine1 Aim, включая actual up-channel position copy и оба источника по
иерархии pose. Следом по ресурсу находится PointConstraint Spine2 и Aim
Spine2 к `Torso_Chest_Driver_GRP`; они затем проверены
[отдельным связанным trace](cp2077-native-spine2-constraints.md).

Остаются writers управляющих костей внутри locomotion/main graph, другие
состояния и отдельная связь extracted root motion → movement provider/CCT.

## Артефакты

В `render_camera_RE/analysis/`:

- `spine1_aim_sources_resource_21660.json`;
- `spine1_up_channel_dispatch_21660.md`;
- `spine1_aim_21660_before.bin`, `spine1_aim_21660_after.bin`,
  `spine1_aim_21660_parents.bin`.

Численные и структурные проверки добавлены в
`render_camera_RE/scripts/verify_native_movement_evidence.py`.
