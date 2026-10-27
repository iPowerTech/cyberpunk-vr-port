# Native порядок input / physics / animation jobs

2026-09-13, EXE/IDB из [сводной карты](cp2077-native-chain-index.md).
Live: VR-off PID 1800, base `0x7FF7D0FE0000`.
[Raw observations](cp2077-native-job-order-1800.json).
Это подтверждение зависимостей очередей и конкретных dispatch-путей, не замер
длительности кадра или доказательство нулевой задержки во всех режимах.

## 1. Что реально делает JobQueue

Сопоставлены SDK `JobQueue.hpp` / `JobQueue-inl.hpp` и инструкции:

| Функция | Подтверждённое действие |
|---|---|
| `0x14014299C` | dispatch job с prerequisite handle и completion handle |
| `0x1401422DC` | dispatch parallel/range job; передаёт те же зависимости |
| `0x14095EC88` | thin wrapper: разыменовывает handles, вызывает `0x14014299C` |
| `0x140141B10` | переводит текущий completion в prerequisite следующей группы jobs и создаёт новый completion |
| `0x140142838` | join другого completion handle в зависимость очереди |
| `0x140218C38` | capture completion очереди |
| `0x140142F88` | завершает очередь; связывает её completion с родительским JobGroup и освобождает handles |

Layout очереди: `+0x10` prerequisite handle, `+0x18` completion handle,
`+0x20` parent-group link, `+0x28` params, `+0x30` captured.
В handle `+0x18` — pending count; `+0x1C` — refcount.

В `0x14014299C`:

1. Увеличивается pending count completion.
2. Если prerequisite имеет ненулевой pending count, копия job связывается с
   его waiting list под блокировкой (`0x140142A47`).
3. Иначе job помещается в runnable queue (`0x140142AAF`).

**`0x140141B10` не ожидает завершения jobs, блокируя CPU.** Поэтому caller может
продолжать строить граф, пока предыдущие jobs исполняются. Ограничение порядка
создаётся handles. Внутри одного этапа несколько jobs могут исполняться
параллельно; следующий этап получает зависимость от их общего completion.

## 2. Два уровня этапов

`0x140952B30` создаёт очередь и строит:

1. `0x140952C28(..., begin=0, end=4)` — внешние этапы 0–3.
2. `0x140952D9C` — три bucket и их внутренние этапы.
3. `0x140952C28(..., begin=5, end=13)` — внешние этапы 5–12.

`0x140952C28` переключает dependency handles через `0x140141B10` **после
каждого внешнего этапа**. Названия из `0x140243BA8`:

| Внешний номер | Имя |
|---|---|
| 0 | FrameBegin |
| 1 | Multiplayer_UpdateStateSnapshots |
| 2 | EntityUpdateState |
| 3 | PreBuckets |
| 4 | Buckets — выполняются внутренним обходом |
| 5 | PostBuckets |
| 6 | CameraUpdate |
| 7 | PlayerAimUpdate |
| 8 | PostPlayerAimUpdate |
| 9 | MappinsUpdate |
| 10 | BlackboardCallbacks_SecondPass |
| 11 | PreRenderUpdate |
| 12 | Multiplayer_CaptureStateSnapshots |

### Три bucket — не три кадра

Имена массива `off_142FEF448` подтверждены по самим указателям:

| Bucket index | Имя |
|---|---|
| 0 | VehicleBucket |
| 1 | CharacterBucket |
| 2 | AttachedObjectBucket |

`0x140952D9C` имеет внешний цикл **0..2**, внутренний — **12 этапов**.
Для callback проверяются enabled byte `record+0x78` и mask:
`record[2] & (1 << bucketIndex)`.
Маска 7 означает все три bucket; маска 2 — только CharacterBucket.

После каждого непустого внутреннего этапа — `0x140141B10`. Между callbacks
одного этапа его нет: порядок регистрации **не даёт** взаимного порядка их
исполнения. Это особенно важно для параллельных physics и animation jobs.

## 3. Порядок внутри CharacterBucket

Числовые имена подтверждены `0x140626660`, регистрации — конкретными системами:

| Этап | Имя | Подтверждённый связанный callback |
|---|---|---|
| 0 | Entities_PreTick | пока без утверждений о полном составе callbacks |
| 1 | Entities_ServiceEvents | то же |
| 2 | PrePhysicsTick | stage name подтверждено; состав input/PSM ещё не исчерпан |
| 3 | UpdateTransformPrePhysics | `EntityTransforms/Pre` |
| 4 | PhysicsFlushBufferedState | `Physics/UpdateInBucket` |
| 5 | PhysicsExecuteAsyncQueries | `Physics/ExecuteAsyncQueries` и `EntityAnimations/PreUpdateKick` |
| 6 | PostPhysicsSyncResults | `EntityPhysics/PostCharacterControllerTransforms`, `EntityAnimations/PreUpdateFinish` |
| 7 | UpdateTransformPostPhysics | `EntityTransforms/Post` |
| 8 | AnimationUpdate | `EntityAnimations/Update`, `CollisionNodeManager/TickKick` |
| 9 | PostPhysicsTick | `CollisionNodeManager/TickFinish` и прочие callbacks |
| 10 | Entities_PostTick | stage name подтверждено |
| 11 | Entities_PostServiceEvents | stage name подтверждено |

Регистраторы:

- animation: **`0x1411F5D00`**, mask 7, stages 5/6/8;
- physics: **`0x1411F3A28`**, mask 7, stages 4/5/8/9;
- post-CCT entity transforms: **`0x1413DA488`**, mask **2**, stage **6**;
- transform system: **`0x14130B30C`**, mask 7, stages **3/7**.

Registration helpers `0x140626B94/0x14062690C → 0x14062575C` сохраняют ключ
`{outer=4, innerStage, bucketMask}`. Это связывает приведённые номера с
исполняющим циклом, а не только с текстом профайлера.

## 4. Отдельные completion handles physics и animation pre-update

### Physics

Stage 4: `0x14056F2D4 → 0x14056F2DC`:

- `0x14056F598` обрабатывает buffered physics state;
- `0x14056F270` создаёт очередь scene-query work;
- её capture присоединяется к **physicsSystem+0x1C8**.

Stage 5: callback **`0x140CD1A80`** выполняет
`Join(queue+0x10, physicsSystem+0x1C8)`.
Он присоединяет завершение ранее запущенной работы, а не повторяет расчёт
позиции сущности.

Stage 6: callback `0x140B9BB44` запускает range job с именем
`EntityPhysics/ApplyPostCharacterControllerTransforms`:
`0x140D0F2F0 → moveComponent.vtable+0x240 → 0x140336390`.
Эта последняя связь уже подтверждена live в W trace PID 21236.

### Animation pre-update

Stage 5: **`0x14060413C`** создаёт отдельную queue,
выполняет `0x1406041D0` и присоединяет capture к
**animationSystem+0xAA9B0**.

Stage 6: **`0x140C9A290`** делает
`Join(queue+0x10, animationSystem+0xAA9B0)`.

Post-CCT callback и animation PreUpdateFinish находятся **в одном этапе 6**.
Между ними не объявляется отдельный последовательный порядок. Переход к
этапу 7 зависит от завершения всего этапа 6 с присоединёнными подзадачами.

## 5. T.world → pose → camera binding

Stage 7 callback `0x140C7A540 → 0x140A95D70` запускает
`EntityTransforms/ApplyScheduledTransformUpdates`:
`0x140B53E3C → 0x1401C9430 → 0x14068E1F8` для root без parent binding.
Он переносит T.local → T.world, затем распространяет transform на детей.

Stage 8 callback **`0x140CA12F4 → 0x140603F78`**:

```text
0x140604D60: enqueue update anim objects
  -> callback 0x141CA3AF0 -> 0x141CA2650 -> 0x1401D2DB8
     graph input updates, graph sampling, pose copies, FK
при CharacterBucket: дополнительные instanced/ragdoll задачи
0x1406046C0:
  additional cascade transforms -> dependency boundary
  cascade transforms -> dependency boundary
0x14060400C: cleanup
```

Cascade callback `0x141CA3B60 → 0x141CA3280 → range callback 0x141CA3C60`
ведёт в **`0x141CA3720 → 0x1401D9528`**, где была поймана передача A.world
в `slots` и далее bone binding камеры.

Таким образом, в исследованном обычном пути T.world обновляется на этапе 7,
pose и связанные bone/slot transforms — на этапе 8. Внешний `CameraUpdate`
находится после завершения buckets. Это объясняет ранее пойманное состояние
«T.local уже новый, T.world ещё старый» как границу этапов.

## 6. Live подтверждения очереди, PID 1800

В одном проходе CharacterBucket сохранены три последовательных останова:

1. После `0x140604D60`, stop `0x140603FAA`:
   queue `0xB31E1FFB90`, dependency pending **2**, новый completion pending **0**.
   То есть dispatch уже вернулся, хотя предыдущая работа ещё не завершена.
2. Перед enqueue cascade, stop `0x140604726`:
   та же queue, handles после промежуточных групп изменились;
   R8/R9 адресуют именно queue+0x10/+0x18.
3. В `0x141CA203B`:
   job handler **`0x141CA3B60`**, prerequisite `0x214C10D7120` (pending **1**),
   completion `0x214C10D51E0` (pending **0** перед dispatch).

Фактический `EntityAnimations/Update` дополнительно пойман на entry
`0x140CA12F4`: bucket byte **1**, animation system **`0x218AEDD1F80`**,
return `0x140243900`, то есть общий stage callback executor `0x14024387C`.

## 7. Что установлено о motion-выходе — и что ещё нет

В `0x1401D2DB8` transform **K+0x30..+0x5F** сбрасывается и композиционно
накапливается из отдельного transform output каждого graph. Это отдельное
поле от массива bone pose D. `0x140A99854` экспортирует его вместе с spans
bones/floats и checksum в последующие animation/skinning jobs.

Data-watch после FK поймал именно это экспортное чтение, **не CCT consumer**.
Поэтому оно не используется как доказательство «root motion двигает капсулу».

На автоматическом W `a2a3d732246a443e80175effab630870` проведён before/after
**одного вызова** `0x140328934` основного graph игрока:

- graph `0x218B2E25970`, K `0x214A5E60C80`, RSP `0xB31D6FED00` совпали до/после;
- MoveXY=(0,1);
- transform output сохранил XYZ=(0,0,0), quaternion identity, scale=(1,1,1);
- четвёртая lane translation изменилась; её семантика здесь не назначается.

Это локальное подтверждение нулевой spatial delta данного graph в данном
вызове. Оно не доказывает отсутствие root-motion extraction у всех графов,
клипов или других locomotion states.

Дополнительно: активный player provider `0x140D455B0` возвращает нулевой XYZ
для временного translation выхода moveComponent, а yaw quaternion строит из
P+0xA4. Позиция обычной LocomotionSimple берётся из уже прослеженного CCT.
Путь активного root motion для других actions/сцен остаётся открытым.

Позднее [PlayerClimb trace PID21660](cp2077-native-climb-motion.md) установил
отдельный `AnimMotionMoveControllerWithDelta` и ненулевую placement delta до
physics position request / T.local. Там sampling object отсутствовал;
это уточнение специального движения, не доказательство K.motion→CCT.

## Артефакты и граница покрытия

Все файлы ниже в `render_camera_RE/analysis/`:

- `native_animation_job_primitives_1800.md`
- `native_job_dependency_protocol_1800.md`
- `native_system_phase_registration_1800.md`
- `native_named_update_stage_order_1800.md`
- `native_per_stage_dispatch_1800.md`
- `native_outer_frame_phase_sequence_1800.md`
- `native_bucket_names_1800.json`
- `native_physics_bucket_stage_order_1800.md`
- `native_cct_stage_entrypoints_1800.md`
- `native_cct_completion_fence_1800.md`
- `native_transform_stage_registration_1800.md`
- `native_animation_phase_binding_1800.md`
- `native_phase_callback_executor_1800.md`
- `native_anim_motion_output_readers_1800.md`
- `native_move_provider_modes_1800.md`

Источники raw byte lengths и взаимных адресных связей проверяет
`verify_native_movement_evidence.py`.
Структурный happens-before нормального пути теперь установлен глубже;
переключения bucket/LOD/visibility, принудительные animation states,
root-motion actions и полная frame-ID корреляция ещё не закрыты.
