# PlayerClimb: motion provider → позиционный backend → physics request / T.local

2026-09-16, VR-off **PID21660**, imagebase **`0x7FF6D5E00000`**.
[Raw same-call evidence](cp2077-native-climb-motion-21660.json).

**Продолжение 2026-09-17:** [PID23736: buffered property1 → CCT](cp2077-native-climb-cct.md)
подтвердило consumption и actual PhysX position write в новом trace.
Ниже сохранена граница исходного PID21660; клиповый sampler пока не снят.

**Подтверждён активный специальный путь движения игрока при залезании.**
В данном sample источник ненулевой дельты — интерполяция заданного смещения:
motion-extraction handle отсутствует, и его sampler вернул identity.
Поэтому результат не объявляется измерением ненулевого root motion из клипа.

## 1. Как найден правильный controller

По строкам имён controllers и vtable связям установлены:

- **AnimMotionMoveController**: vtable `0x142ABF848`, Update `0x1402D0658`,
  delta getter `0x140AEDBDC`;
- **AnimMotionMoveControllerWithDelta**: vtable `0x1431DBAF8`, базовые
  Update/getter те же, world-placement virtual slot+0xC0=`0x142A6B618`;
- **PlayerClimb**: action vtable `0x1431DBA70`, type43, name getter
  `0x142989314`. Конструктор `0x142988FB8` создаёт controller по action+0x50.

Обычный PhysicalMoveController имеет другой vtable (`0x142B5D3B0`),
его getter `0x140D455B0` был исследован отдельно. Выход K.motion, который
копирует `0x140A99854`, сам по себе не обозначает этот movement provider.

## 2. Live активация именно у игрока

Пользователь подтвердил готовность перед доступным для залезания препятствием.
Выполнен input **`db3e8867f58d45bfab8bdd7f70e5625a`**, W+Space **0.12s**,
с key-up и восстановлением исходного HWND.

На entry `0x1402D0658`:

| Объект | Pointer / значение |
|---|---|
| Player entity E | `0x27A9EA90F50` |
| PlayerClimb action | `0x28AAA344210` |
| Controller C | `0x28AAA344260` = action+0x50 |
| Move component S=`[C+0x80]` | `0x27AA6B8EC50` |
| `[S+0x90]` | E — independently checked |
| `[S+0xA8]` active provider | C |
| Motion-record array=`[C+0x30]` | `0x2768EAF2F70` |
| Count=`[C+0x3C]` | **1** |

Record stride **0x190** подтверждает `0x1402E002C`.
Это первый **захваченный** update, а не заявление о первом update action:
в controller уже находился результат предыдущего шага.

## 3. Развилка: extraction identity и ненулевой placement blend

```text
AnimMotionMoveController.Update / 0x1402D0658
  -> record.time advance
  -> 0x140325CC8
     -> 0x140325ED8
        -> 0x140326280
           binding+0x18 == null: identity result
        -> time-based placement interpolation
```

В record:

- initialized flag +0xA0=1;
- binding находится в record+0xD8;
- sampling object в binding+0x18 (**record+0xF0**) = **0**.

На **`0x1403262E7`** поймана именно null-ветвь. Возврат в
`0x140325F8F` дал XYZ=0, quaternion identity, scale1; весь48-byte result
сохранён. Ненулевой sampling branch с virtual method+0xD8 здесь не исполнялся.

После этого provider использует reference placement difference в record+0x70.
Для captured branch `0x14032610C` вычисляет:

```text
u = clamp((elapsed-delay)/duration, 0, 1)
alpha = (3-2*u)*u*u
translationXYZ = referenceDifferenceXYZ * alpha
```

Вычисления выполняются с float32 rounding по инструкциям. В sample:

- **u=0.018387888**;
- **alpha=0.001001909**;
- output XYZ на `0x1402D07CD` **побитово совпал** с этой формулой.

Elapsed/time поля независимо согласованы с timestep S+0x22C. Четвёртая
position lane сохраняется в raw, но не интерпретируется как геометрическая
координата. Quaternion provider output в этом шаге identity.

## 4. World placement → дельта controller

World-placement callback **`0x142A6B618`**, virtual+0xC0, компонует provider
result с anchor transform C+0x40 и pre/post offsets. В captured inputs
два angle offset равны0, дополнительные XYZ offset пренебрежимо малы.
С учётом float32 rounding проверен полученный world XYZ.

Затем `0x1402D0658` вычитает прошлую world position C+0x60 и сохраняет
результат в C+0xA0:

```text
delta = newWorldPosition - previousWorldPosition
delta = (0.0001220703125, 0, 0.0019989013671875) m
```

Возврат в **`0x140336FCB`** сохранён с RSP на8 bytes выше Update entry;
это один и тот же вызов controller. Getter `0x140AEDBDC` затем передаёт
translation в S+0x170. На `0x14033704C` эти16 bytes совпали с C+0xA0.

## 5. Активный backend и синхронизация physics adapter

У S+0x138 container два backend entries, active index=**0**:

1. `0x2769F047840`, vtable `0x142ABF1B0` — в наблюдавшемся методе
   **`0x1403376A0`** он прибавляет delta к своей world position +0x20.
2. `0x276AEC8A550`, vtable `0x142B5D470`, physics handle в +0x40=
   **0x10015** — синхронизируется как другой backend.

На `0x14033712C` второй backend получает absolute position/rotation активного.
Новый XYZ активного backend **побитово совпадает** с предыдущим XYZ + delta
и с рассчитанным world placement controller.

Вызван **`0x140D815F4`**, который отправил через `0x140240998`:

- handle **0x10015**;
- **property1** (шестой argument, stack arg6), payload12 bytes XYZ;
- non-null buffer owner → путь **`0x140240A60`**.

`R9=0` не является property id в этом API. Position определяется arg6=1;
это также согласуется с обработкой property1 в `0x140926020`.
Payload совпал с actual world position побитово. Позднее исполнение этой
buffered property в CCT здесь не снято: доказан **запрос**, не завершение
всех физических операций и не collision sweep.

## 6. Применение к entity local transform

В том же `0x140336390` пойманы `0x1403365FA` и возврат `0x1403365FF`:

- S=`0x27AA6B8EC50`, RSP=`0x95EC8FF1B0` на обеих точках;
- placed T=`[E+0xB0]=0x276B186CAD0`;
- input transform XYZ совпал с physics position payload;
- `0x1401DC0E0` обновил T.local по T+0xC0/0xD0.

После fixed-point conversion (`worldFloat * 131072`) получено точное
совпадение T.local. Изменение fixed XYZ:

```text
(+16, 0, +262) fixed units
 = (+0.0001220703125, 0, +0.0019989013671875) m
```

Так зафиксировано применение ненулевой дельты игрока в специальном состоянии,
а не только её существование в animation buffer.

## 7. Что закрыто и что остаётся

Подтверждено live:

```text
PlayerClimb
 -> AnimMotionMoveControllerWithDelta
 -> identity motion sample + placement smoothstep
 -> world displacement
 -> active position backend
 -> secondary physics position request
 -> T.local
```

Остаётся получить отдельный sample с **ненулевым sampling object**, где
движение действительно извлекается из animation motion data. Нельзя
приписать это пойманному залезанию с null handle. Также отдельно требуется
сопоставить queued property1 с её фактическим CCT consumption и продолжением
world propagation. Эти пробелы сохранены, а не объявлены закрытыми.

## 8. Прерванный hardware probe

До успешного climb trace probing K+0x30 не дал нового подтверждённого
consumer. После удаления watch обнаружились остаточные DR slots в потоках.
Игра восстановлена через очистку **только собственного адреса** и detach /
reattach того же PID; successful climb trace выполнялся уже software probes.
Подробности: [debugger-maintenance evidence](cp2077-native-motion-watch-recovery-21660.json).
Этот неудачный probe не используется как доказательство отсутствия root motion.

## Артефакты

В `render_camera_RE/analysis/`:

- `motion_controller_names_21660.json`, `anim_motion_controller_identity_21660.md`;
- `anim_motion_provider_dispatch_21660.md`, `anim_motion_controller_dataflow_21660.md`;
- `anim_motion_provider_sample_path_21660.md`, `anim_motion_extraction_to_controller_21660.md`;
- `player_rootmotion_extraction_binding_21660.md`, `player_anim_motion_action_update_21660.md`;
- `climb_motion_interpolation_and_physics_sync_21660.md`;
- `motion_movecomponent_provider_paths_21660.md`;
- `native_vroff_auto_input_21660.jsonl`.

Численные, byte-level и input-log проверки включены в
`render_camera_RE/scripts/verify_native_movement_evidence.py`.
