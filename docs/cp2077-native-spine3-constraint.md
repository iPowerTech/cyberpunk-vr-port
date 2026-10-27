# Spine3 ParentConstraint от Torso_Chest_Driver_GRP

2026-09-16, VR-off **PID21660**, imagebase **`0x7FF6D5E00000`**.
[Raw evidence](cp2077-native-spine3-constraint-21660.json).
Продолжение [Spine2 Point → Aim](cp2077-native-spine2-constraints.md).

**Подтверждено:** ParentConstraint задаёт model-позу Spine3 по chest driver,
затем переводит её в local-space относительно actual parent Spine2.
Полные before/after buffers и математика сняты в одном вызове.

## 1. Identity и параметры

| Поле | Значение |
|---|---|
| Resource handle / nodesToInit index | **3727 / 2175** |
| Runtime node | `0x276971C0080` |
| Main graph state | `0x276A3B79F80` |
| Sample vtable slot+0x128 | `0x1402C4B70` |
| Target pose index | **13 = Spine3** |
| Driver pose index | **135 = Torso_Chest_Driver_GRP** |
| Actual parent | **pose10 = Spine2** |
| Weight | **1.0** |
| Reference-pose offset branch | **выключена** |
| Weight/translation/rotation input links | **disconnected** |

Имя driver/target подтверждено и node CName fields, и mapping pose indices
к full-rig name array. Resource/actual node-array pointer также совпали.
Полный header snapshot содержит соседние bytes после0x140; они не используются
как поля этого node.

## 2. Сопоставленный trace

W input **`1da3330a4bc3426aad351c6b323ce44c`**, native MoveXY=(0,1).
Output wrapper=`0x95EB5FDA80`, sample context=`0x95EB5FDAB0`.
Pose transforms=`0x27AA1C202B0`, count153.

1. **`0x1402C4CC5`**, после driver model getter:
   - R12 target index13, R13 driver index135;
   - RAX driver model pointer=`0x27AA1C23B60`;
   - сохранены model transform, полный local buffer и parent array.
2. **`0x1402C4DAC`**, перед `0x1401DBB00`:
   - desired model на stack **побитово равен driver model**;
   - parent model Spine2 по `0x27AA1C223F0` снят отдельно;
   - target local transform ещё не изменён.
3. **`0x1402C4DB1`**, после helper:
   - сохранены target и полный after buffer;
   - тот же node, state и **RSP=`0x95EB5F1650`**.

Весь after buffer отличается от before **только record13**.
Изменения parent state/frame между stops не допускались guards.

## 3. Независимая проверка

Model transform chest driver воспроизведён из снятой local pose и hierarchy:

- max position error **6.20e-8 m**;
- max quaternion component error **6.65e-8**.

Проверено также происхождение parent model Spine2. Для этого sample нет
blend с исходной target pose: вес1, offset branch выключена, дополнительные
links отсутствуют; desired model полностью совпадает с driver.

Inverse parent transformation независимо даёт:

```text
local.position = inverse(parent.rotation) * (desired.position-parent.position)
                 с учётом parent.scale
local.rotation = inverse(parent.rotation) * desired.rotation
local.scale    = desired.scale / parent.scale
```

Максимальные ошибки относительно фактического local output:

| Компоненты | Ошибка |
|---|---:|
| Position XYZ | **1.13e-7 m** |
| Quaternion XYZW | **4.31e-8** |

Изменение local Spine3 в данном вызове:

- translation XYZ — **0.008734085 m** (8.734085 мм);
- rotation — **41.257469°**, с учётом `q≡−q`.

Это один конкретный sample, не постоянная поправка и не утверждение об ошибке
движка. Значения translation W сохранены как raw, геометрический смысл им
не назначается. Остальные152 local records побитово неизменны; model-позы
дочерних костей могут меняться позже при FK.

## 4. Граница покрытия

Теперь actual write и математика установлены для последовательности
Hips Parent → Spine Aim → Spine1 Point/Aim → Spine2 Point/Aim → Spine3 Parent.
Вся длинная последовательность не представляется одним frame: её этапы
подтверждены отдельными trace, а этот отчёт относится к одному Spine3 вызову.

Отдельно открыты active root-motion → movement/CCT, другие PSM states и
режимы, оставшиеся pose consumers/IK и сквозная связь с GPU-потребителем.
Эти пробелы не закрываются повторной проверкой похожего constraint.

## Артефакты

В `render_camera_RE/analysis/`:

- `spine3_constraint_resource_21660.json`;
- `spine3_chain_21660_before.bin`, `spine3_chain_21660_after.bin`;
- `spine3_chain_21660_parents.bin`.

Sample/math routines описаны в `main_geometric_writer_math_17268.md` и
`hips_model_cache_composition_17268.md`; IDA VA совпадают с данным EXE,
runtime addresses заново получены для PID21660.

Численные и структурные проверки включены в
`render_camera_RE/scripts/verify_native_movement_evidence.py`.
