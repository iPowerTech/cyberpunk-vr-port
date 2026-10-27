# CP2077: подтверждённая native-цепочка движения, камеры и скелета

Состояние исследования: 2026-09-17. **Полное покрытие ещё не достигнуто.**
2026-09-19: на базе подтверждённого displacement/CCT-пути подготовлена
[реализация базового roomscale](roomscale-native-implementation.md).
Полный реверс остальных состояний не является блокером её live-проверки.
Это указатель на доказательства; адреса — IDA VA для EXE SHA-256
`a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991`.
Runtime pointers различаются между запусками и приведены только в их отчётах.

## Движение

```text
actionEvent
  -> 0x1405BFF84: action table MoveX/MoveY
  -> 0x1404C64CC: sample MoveXY
  -> LocomotionSimple / 0x1406AB804
  -> 0x1406ABB18: расчёт и умножение на timestep
  -> 0x1406AB990: P.pendingMovement (+0x80)
  -> property 9 + timestep property 27
  -> buffered consumer 0x14024084C -> 0x140926020
  -> CCT tick 0x1402BF5A8: collision-resolved position
  -> ApplyPostCharacterControllerTransforms / 0x140336390
  -> 0x1401DC0E0: T.local и dirty notification
  -> 0x14068E1F8: T.world
```

Один W request сопоставлен по всем XYZ bytes и dt; CCT float position
математически совпала с fixed-point T.local (`×131072`). В том же останове
T.world был ещё старым. Это доказанная граница двух этапов, не «lag по видео».

Источники: [базовая цепь](cp2077-native-movement-camera-skeleton.md),
[VR-off §§11–12](cp2077-native-vr-off-21236.md).

### Обычный прыжок

В пойманном initial jump действует **LocomotionSimple**. `0x1404C3BA4`
вычисляет `sqrt(2*abs(upwardsGravity)*JumpHeight)` и пишет solver vertical
velocity. Stat **927=JumpHeight** подтверждён регистратором EXE.
Измеренные `vZ=6.196773529` и dt дают displacement с точным float32 совпадением.
Сам stat value 1.0 пока только согласуется с формулой, непосредственно не снят.
Источник: [jump trace PID 1800](cp2077-native-jump.md).

### PlayerClimb: отдельный animation-motion controller

В PID21660 пойман **PlayerClimb** с активным
`AnimMotionMoveControllerWithDelta`. В одном вызове прослежены motion record,
smoothstep placement, world delta, активный позиционный backend, buffered
physics position request (property1, handle0x10015) и запись T.local.
Дельта **(0.0001220703125, 0, 0.0019989013671875) m** совпала с
fixed-point изменением **(16,0,262)**.

В этом конкретном record sampling object **null**: extraction helper вернул
identity, а ненулевая дельта сформирована interpolation заданного смещения.
Случай не объявляется извлечённым root motion из клипа. Последующее CCT
consumption property1 отдельно не снято.
Источник: [PlayerClimb motion path](cp2077-native-climb-motion.md).

В новом [trace PID23736](cp2077-native-climb-cct.md) закрыто фактическое
применение buffered property1: payload+generation связаны с consumer,
backend XYZ обновлён, scene origin вычтен, PhysX setFootPosition переводит
позицию стопы в центр капсулы и пишет CCT+0x210. Все координаты совпали
побитово. Record header до consumption заменяется; один pointer не является
устойчивым идентификатором запроса. Extracted clip motion у игрока пока не снят.

[Non-null sampler PID23736](cp2077-native-nonnull-motion-sampler.md) затем
пойман у **другого actor**: WorkspotMovementController использует
animPlaneUncompressedMotionExtraction (54 ключа X/Y/yaw) и возвращает
ненулевой transform. Для игрока эта ветка ещё не подтверждена; следующий
обоснованный сценарий — анимированное workspot-взаимодействие.

### Скорость и направление → AnimGraph

`0x1404C6064` использует **physics velocity**, вычисляет planar speed,
acceleration и направление в локальных координатах entity через inverse
quaternion. В сопоставленном D trace inverse-rotation совпала с ошибкой
**2.25e-7**. Затем `0x140930EFC` публикует `AnimFeature_PlayerMovement`
как **`playerLocomotion`**.

W trace прослежен через Q feature handles → RTTI property `speed` → compiled
graph binding → **state+0x320** → реальное чтение `AnimNode_FloatInput`.
Этот offset относится к конкретному graph instance.
Дальнейший обнаруженный путь `CurveFloatValue → DampFloat → MathExpressionFloat`
доходит до **BlendAdditive с camera.additiveCameraMovementsWeight**;
это камера-зависимая additive-ветвь.
Источник: [feature и graph consumers, PID 1840](cp2077-native-locomotion-feature.md).

### Прикреплённый locomotion-граф и Sample клипа

Main resource **player_base.animgraph** (2357 nodesToInit) вызывает slot
**Locomotion**, реально прикреплён **player_locomotion.animgraph** (1128 nodes).
Его отдельный state использует speed slot **+0x24**. В PID1840 пойман
`speed>=0.02` condition с возвратом true при W.

В PID17268 проверены `fpp_idle_stand` Update и Update/Sample клипов состояния
**idle_to_jog**. Один полный Sample `fpp_idle_to_jog_090` записал
**153 transforms + 52 tracks**, return=1; before/after сравнивались в одном
вызове с node+RSP guard. Итоговые blend weights и связь этого clip output с
final 619-bone pose далее уточняются.
Источник: [player graphs и полный Sample](cp2077-native-player-graphs.md).

### Поза клипов → blend → выход Locomotion slot

Для одного `idle_to_jog` Sample измерены BlendMultiple indices **1,0** и
weight **0.984149873**; translation/scale и tracks совпали с float32 lerp
побитово, quaternion — с ошибкой **1.17e-7**. Parent Blend2 с weight0
возвращает тот же buffer.

В другом связанном trace внутри одного slot Sample измерен transition
alpha **0.104601994**. Его численная проверка прошла; **153 transforms**
transition output и Locomotion slot output совпали побитово.
Источник: [blend и slot output](cp2077-native-locomotion-blend.md).
Следующий trace замкнул границы slot output → main local output → полный FK
в одном внешнем вызове, см. ниже.

### Main pose, дополнительные графы и полный FK

Один связанный trace PID17268 снял slot output, конечную main local pose,
вход и выход FK **всех 619 костей**. Независимый расчёт position/quaternion/scale
совпал с максимальными ошибками **2.53e-7 / 1.72e-7 / 5.96e-8**.

В отдельном вызове проверено последовательное построение общего local buffer:
main graph (151 mapping pair) → FPP-deformations (182) → shadow graph (55).
Deformations и shadow пересекаются с main по **66** и **29** target indices.
С учётом проверенных корневых corrections весь buffer восстановлен побитово;
его последняя версия не меняется до FK.

Slot и main output не одинаковы: между ними изменены 64 translation XYZ и
72 rotations. Конкретные writer nodes этих изменений ещё не атрибутированы.
Источник: [main pose → mapping → FK](cp2077-native-main-fk.md).

### Установленный writer Hips между slot и main output

ParentConstraint (resource handle3733, node index2181) переносит model pose
**Torso_Hips_Driver_GRP (pose index133)** на **Hips (index2)** с weight1,
затем переводит её в local-space. Driver model независимо воспроизведена
из153 local transforms и hierarchy: ошибки position/quaternion **5.39e-8 /
6.00e-8**. Local conversion совпала побитово; в полном buffer изменён только
Hips, на **1.155148 мм / 0.034228°** в данном sample.
Источник: [Hips ParentConstraint](cp2077-native-main-hips-constraint.md).
В новом PID21660 model-поза драйвера воспроизведена уже из **Locomotion slot
output**: ошибка position **3.42e-7**, quaternion **2.24e-7** с выравниванием
эквивалентного знака. Непосредственно до constraint — **4.38e-8 / 5.26e-8**.
Источник: [ancestry драйвера](cp2077-native-driver-ancestry.md).
Это подтверждает геометрию входа, но не отсутствие промежуточных записей.
Остаются источники отдельных ancestor transforms внутри locomotion graph
и остальные main-graph constraints.

### Следующие constraints Spine и Spine1

В одном вложенном Sample PID21660 измерены:

- AimConstraint: **Spine (pose4)** смотрит на **Torso_Spine1_P_GRP (pose94)**;
  independent forward/up solve совпал с ошибкой quaternion **1.77e-7**,
  local orientation delta **0.192212°**.
- PointConstraint: **Spine1 (pose7)** поставлен в ту же model-space target
  point; ошибка XYZ **8.49e-8 m**, local displacement **1.482345 мм**.

Полные buffers подтверждают изменение только index4 в первом шаге и только
index7 во втором. Проверено model→local с нетривиальным parent quaternion.
Источник: [Spine Aim → Spine1 Point](cp2077-native-spine-constraints.md).

Следующий Aim для **Spine1** также проверен: target **Torso_Spine2_P_GRP
(pose97)**, up-point **Torso_Spine1_Up_GRP (pose99)**. Actual up-channel
копирует16 bytes model position без изменения. Goal/up model positions
воспроизведены по hierarchy с ошибками **6.09e-8 / 7.75e-8 m**.
Solve error **4.67e-7**, orientation delta **0.055748°**, изменён только pose7.
Источник: [Spine1 Aim и источники точек](cp2077-native-spine1-aim-sources.md).

**Spine2 Point → Aim** проверены в одном вложенном вызове PID21660:
Point использует `Torso_Spine2_P_GRP` (97), затем Aim —
`Torso_Chest_Driver_GRP` (135) и up-point `Torso_Spine2_Up_GRP` (100).
Point local displacement **0.207424 мм**, Aim delta **0.052451°**;
Aim solve error **6.88e-8**. На каждом этапе меняется только local record10,
а полная передача Point output → Aim input побитово идентична.
Источник: [Spine2 constraints](cp2077-native-spine2-constraints.md).

Следующий **Spine3 ParentConstraint** (pose13) переносит model-позу
`Torso_Chest_Driver_GRP` (135) с weight1 без reference offset. В одном вызове
изменён только record13: **8.734085 мм / 41.257469°**; local conversion
совпала с max position/quaternion errors **1.13e-7 / 4.31e-8**.
Источник: [Spine3 Parent](cp2077-native-spine3-constraint.md).

## Mouse / camera / skeleton

```text
CameraMouseX/Y и CameraX/Y -> 0x1404C6730
  -> 0x1403F32A4: input deltas
      yaw -> P+0x9C -> P+0xA4 -> quaternion(deltaDegrees)
      camera deltas -> AnimFeature_FPPCamera
          -> native camera animation node
          -> local control-bone modifications + scalar outputs

graph pose -> 0x14017DDB4 (copy local transforms)
          -> 0x1404B6968 (FK local→model in place)

T.world -> animated root A.world
A.world × model bone 118 (Torso_fppCamera_Aim_JNT)
        -> slot 0 "camera"
        -> FPP camera world transform
```

Связанный pitch trace сохранил одно изменение input, node accumulator,
named output и model pose. Составленная camera quaternion совпала с live
с ошибкой **4.98e-8**; position — в пределах **одного fixed-point шага**.
Именованные outputs возвращаются в FPP camera отдельным feedback-каналом.

Источники: [VR-off camera, FK и feedback](cp2077-native-vr-off-21236.md),
особенно §§4–6, 10, 14–20. Запись `S+0x1D0` не объявляется единственным
владельцем yaw: T.local записан раньше, world propagation выполняется позже.

## Переключение приседа

```text
CrouchEvents.OnEnter -> базовые OnEnter
  -> SetLocomotionParameters -> GetStateDefaultLocomotionParameters
     -> SetCapsuleHeight(1.0) -> optional merge -> CCT property 29
  -> SetAnimationParameterFloat("crouch",1.0)
     -> scalar storage current/pending
     -> 0x140328D0C -> 0x140328710
     -> float slot рабочего состояния graph
```

Script stack подтверждён runtime function/class hashes. Height-переход
`1.8→1.0→1.8` проверен; animation scalar получает самостоятельную запись.
Графовый slot для конкретного живого instance: index **0**, offset **0x9908**.
Эти значения специфичны для данного compiled graph, не универсальны.

Источники: [9976](cp2077-native-vr-off-9976.md),
[19316](cp2077-native-vr-off-19316.md), [1800](cp2077-native-vr-off-1800.md).

## Pose → skinning data для mesh игрока

```text
619 model transforms
  -> mapping {sourceBone, referenceIndex, paletteIndex}
  -> 0x140216990 / 0x140216DC4
  -> 3×4 skin matrices в CPU staging
  -> SkinningManager_UploadNewData / 0x140A7DD14
  -> mapped memory конкретного ID3D12Resource
```

Одна матрица независимо вычислена из снятых model TRS и reference matrix:
ошибка **5.51e-8**. Полная palette одного `entSkinnedMeshComponent` игрока —
**26×48=1248 bytes** — побайтово совпала до и после transfer.
SHA-256 обоих блоков сохранён; `Map`-маршрут и D3D12 resource установлены.
Источники: [PID 1800](cp2077-native-vr-off-1800.md).

## Зависимости этапов теперь прослежены

[Подробный порядок jobs](cp2077-native-job-order.md) связывает регистрацию
callbacks с исполняющим циклом и prerequisite/completion handles.

```text
VehicleBucket -> CharacterBucket -> AttachedObjectBucket

Внутри CharacterBucket:
  4 PhysicsFlushBufferedState
  5 PhysicsExecuteAsyncQueries + Animation PreUpdateKick
  6 PostPhysicsSyncResults: PostCCT transforms + Animation PreUpdateFinish
  7 UpdateTransformPostPhysics: T.local -> T.world
  8 AnimationUpdate: pose, FK, dependent cascade/bone bindings
...
После buckets: CameraUpdate -> PlayerAimUpdate -> ... -> PreRenderUpdate
```

`JobQueue` выстраивает зависимости без блокирующего CPU wait после каждого
dispatch. Callbacks **одного этапа** могут работать параллельно. Для перехода
к следующему используются handles завершения всей группы, включая дочерние
queues. Порядок регистрации не подменяет эти зависимости.

## Что пока не входит в доказанную полную цепочку

1. Полный lower input path от OS до actionEvent, все smoothing/priority branches.
2. Все native/PSM states и условия переходов (Sprint, AimWalk, air, vehicles,
   scenes, forced states); отдельный crouch trace не распространяется на них.
3. Полный набор graph consumers, веса и выбор lower-body clips, motion matching,
   root-motion extraction, IK и blend/constraint ветви до final pose.
   Transport playerLocomotion, camera-gated consumer, attached Locomotion
   update, полный clip Sample, один clip blend, transition→slot output,
   границы main output→mapping и полный FK уже прослежены.
4. Timeline с единым engine tick/frame ID и всеми альтернативными путями;
   структурный порядок нормальной bucket/job цепи уже подтверждён, но сам
   по себе не доказывает нулевую задержку в каждом режиме.
5. Источник reference matrices как inverse bind и конкретный GPU draw/dispatch,
   который читает наблюдавшийся диапазон palette.

До закрытия этих пунктов roomscale/yaw fix не объявляется выведенным из полной
модели движка. Runtime mutation hooks в ходе этого исследования не добавлялись.

Проверка сохранённых доказательств:
`render_camera_RE/scripts/verify_native_movement_evidence.py`.
Автоматический input: [focus / release протокол](cp2077-native-input-automation.md).
