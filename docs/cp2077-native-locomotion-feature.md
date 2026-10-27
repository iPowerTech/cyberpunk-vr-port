# Physics velocity → playerLocomotion → входы AnimGraph

2026-09-14, VR-off PID **1840**, EXE base **`0x7FF632ED0000`**.
Статика из существующей IDB, live через x64dbg.
[Raw snapshots и input IDs](cp2077-native-locomotion-feature-1840.json).
Все адреса кода ниже — IDA VA; pointers объектов относятся только к этому PID.

## Подтверждённый результат

```text
LocomotionSimple.Update / 0x1406AB804
  -> 0x140930EFC
     -> sample MoveXY
     -> 0x1404C6064: physics velocity, planar speed, local movement direction
     -> AnimFeature_PlayerMovement
     -> publish "playerLocomotion" / 0x140931065
        -> Q feature handles
        -> 0x140328D0C -> 0x14032A020
        -> reflection property -> compiled graph binding -> graph state bytes
        -> AnimNode_FloatInput reads speed
```

Связанный W trace доведён до **действительного чтения графом**. Отдельный
D trace численно проверяет скорость и world→local direction в том же вызове.
Далее найдена конкретная ветвь `CurveFloatValue → DampFloat → MathExpressionFloat
→ BlendAdditive`. Её дополнительный вход — **`camera.additiveCameraMovementsWeight`**.
Поэтому она описана как камера-зависимая additive-ветвь; выбор клипа ног этим
результатом ещё не установлен.

## 1. Объекты и тип feature

| Объект | PID 1840 pointer |
|---|---|
| LocomotionSimple L | `0x202A1D098B0` |
| player entity E из context+0x10 | `0x206B7C976D0` |
| feature H = `[L+0xA8]` | `0x202C5B23890` |
| publication target N | `0x202C14E8BD0` |
| Q = `[N+0xC8]` | `0x202BF3AE8D0` |
| graph instance G | `0x206BA82EC00` |
| graph / state object | `0x202AD2D8750` / `0x202AC4D9680` |
| state memory = `[state]` | `0x206BA8D4A10` |

H.vtable = **`0x142BACB90`**, RTTI getter `0x140CC4E20` возвращает
`qword_1434291F0`. Регистратор **`0x14177EE68`** задаёт тип
**`animAnimFeature_PlayerMovement`**, размер **0x90**. Поля производного типа
зарегистрированы `0x14125EEFC`; базовый `AnimFeature_Movement` — `0x1410B6694`.
Это отличается от отдельного класса `AnimFeature_Locomotion`.

| H offset | Зарегистрированное поле | Источник в наблюдавшемся caller |
|---|---|---|
| `+0x40` | movementDirection | нормализованный `[L+0x60]` |
| `+0x50` | speed | `[L+0xF4]` |
| `+0x54` | desiredSpeed | отдельное поле, источник здесь не замкнут |
| `+0x58` | stabilizedSpeed | отдельное поле |
| `+0x5C` | acceleration | `[L+0x110] / context.dt` |
| `+0x64` | strafeYaw | direction helper |
| `+0x70` | facingDirection | `0x1403C207C(E)` |
| `+0x80` | standingTerrainAngle | `[L+0x114]` |
| `+0x84` | verticalSpeed | `[L+0xF8]` |
| `+0x88` | movementDirectionHorizontalAngle | `0x140406A0C` |
| `+0x8C` | inAir | `[L+0x118]` |

Публикация feature происходит **до расчёта нового pending displacement** в
`0x1406AB990` внутри этого же `LocomotionSimple.Update`. Это локальный порядок
вызовов; он сам по себе не определяет задержку в engine frames.

## 2. Происхождение скорости и направления

В **`0x1404C6064`**:

1. `0x1404C6143 → 0x1420479EC` возвращает physics velocity.
2. Z сохраняется в `L+0xF8`; для следующего расчёта Z обнуляется.
3. `sqrt(vx²+vy²)` записывается в `L+0xF4`.
4. Разность с прежней planar speed записывается в `L+0x110`.
5. Planar velocity нормализуется `0x14013DE80`.
6. `0x1402E96C8(E)` даёт entity transform. Quaternion из `+0x10`
   инвертируется `0x14021301C` внутри **`0x1404C6E3C`**.
7. Поворот направления этим inverse quaternion даёт `L+0x60`.
8. В `0x140930EFC` `H.speed=L.speed`, `H.acceleration=deltaSpeed/dt`,
   а virtual slot `+0x128 → 0x140406A0C` задаёт direction-derived fields.

### Сопоставленный D trace

Input **`7b660528d93749e489cf2ce7b18bc852`**, D 0.1 s:

- `0x1404C6148`: полученная velocity и предыдущая speed сняты **до** расчёта;
  native MoveXY=(1,0).
- `0x1404C61F8`: сняты normalized planar vector и quaternion, непосредственно
  перед inverse-rotation helper. Object+RSP guard сохраняет тот же вызов.
- `0x140931065`: сняты результат `L+0x60` и H, с context+RSP guard.

Результат в координатах entity близок к **(+1,0,0)**, тогда как исходная
world velocity направлена иначе. Скрипт проверки независимо вычисляет
planar speed, acceleration, normalization и поворот inverse quaternion.
Vertical speed сохраняет отдельное отрицательное значение; его не включают
в planar speed и направление ходьбы.

## 3. Публикация — handle, затем поэлементное копирование в graph state

Имя `playerLocomotion` имеет hash **`23F2D4B0D459D639`**.
`0x1402E0EC0` ищет/добавляет запись `{name, featurePtr, refcountPtr}`:

```text
Q+0x70 = feature array
Q+0x7C = count
stride = 24
```

На `0x140328EF8` observed pending entry `0x202BF5EDD88` содержит этот hash
и **тот же feature pointer H**. `[G+0x2590]` совпадает с Q.

`0x14032A020` проверяет имя и совместимость типа, затем `0x140339D50`
обходит RTTI properties, включая базовые. `0x140339E04` разрешает пару
`{feature name, property name}` в descriptor compiled graph.
Тип descriptor выбирает copier в `0x140339F0C`.

### Связанный W trace speed

Input **`2364ccd93b2145e68144668565c3b792`**:

| Этап | Наблюдение |
|---|---|
| publish `0x140931065` | MoveXY=(0,1), H.speed bits **3E81CD71**, float **0.2535205185** |
| consume `0x140328EF8` | та же speed и тот же H |
| source resolution `0x14033A0B1` | source=`H+0x50`; RTTI property offset=0x50, hash=`speed` |
| store `0x14033A0BA` | destination=`[state]+0x320` |
| after `0x14033A0BF` | destination меняется с 0 на **3E81CD71** |
| read `0x14017CCAE` | FloatInput читает **тот же destination**, те же bytes |

Descriptor=`0x206BBB13050` хранит byte offset **0x320**, name hashes и
тип **1 (Float)**. Формула адреса:

```text
0x206BA8D4A10 + 0x320 = 0x206BA8D4D30
```

После store armed data-watch поймал `movss xmm0,[rdx+rax]`;
`RAX=[state]`, `RDX=0x320`, `RCX=FloatInput node`.
Проверка после store ограничена тем же state и RSP.

**H не является immutable snapshot.** Между publish-stop и consume-stop
изменились `desiredSpeed` и `strafeYaw`; speed, direction и acceleration
совпали. Утверждение о побайтовом совпадении всего H не делается.

## 4. Конкретный downstream: speed curve и ограничитель скорости изменения

Первое пойманное чтение speed принадлежит:

- FloatInput node **`0x202ACD36D30`**, vtable `0x142B53E98`;
  `[node+0x48]=playerLocomotion`, `[node+0x50]=speed`, `[node+0x60]=0x320`.
- Caller **`0x14017C054`**, node **`0x202ACCC25D0`**,
  vtable `0x142B54FC0` = **AnimNode_CurveFloatValue**;
  `[curveNode+0x98]` указывает на указанный FloatInput.
- Curve output входит в **AnimNode_DampFloat**, node **`0x202ACE28D30`**,
  vtable `0x142B54D48`, input pointer `[node+0x80]=curveNode`.

Типы подтверждены RTTI getters→registration globals; имена не выведены
из функции общего getter, используемого разными vtables.

В отдельном W input **`31aba9c79fe54be4a2b5883fe57633f3`** поймана запись
сглаженного результата `0x14017CC8C`:

```text
previous = 0.5
defaultIncreaseSpeed = 1.0
defaultDecreaseSpeed = 0.3000000119
update dt = 0.009458299726247787
result = 0.5094583034515381
float32(previous + 1.0*dt) == result  (exact bits)
```

Узел использует отдельный persistent state slot **`[state]+0x96A8`**,
а не переписывает вход `speed`. Его getter `0x14017C148` затем прочитал
снятое значение и вернул его expression evaluator.

У curve-data сняты 7 knot values и 7 output values, interpolation tag 3.
Точный curve sample этого вызова в XMM отдельно не снят; численно проверена
ветвь rate limit и сохранённые таблицы, не весь cubic evaluator.

## 5. До какого pose node дошла эта ветвь

Сглаженное значение передано в **`0x1402CBFE0`**, float input **1**
`MathExpressionFloat`, затем записано в evaluation scratch+0x110 без изменения.

Найден expression node **`0x202ACD6FF20`**, три input sockets:

0. Другой MathExpressionFloat (`0x202ACC80050`), внутренности ещё не замкнуты.
1. Указанный DampFloat.
2. FloatInput (`0x202ACD36D98`) с hashes:
   - **`6FCFDF926F11594E` = `camera`**;
   - **`2C6C3DB60ED61D94` = `additiveCameraMovementsWeight`**.

Имена сверены с реальными строками EXE; второй property регистрируется
`0x1410B4A54`. Compiled input slot — **`[state]+0x394`**.

Токены expression:

```text
81008000 81008001 A1000003 81008002 A1000003
push input0; push input1; multiply; push input2; multiply
```

Код `0x1402CC128` различает input references и operations; opcode **3**
в `0x1402CC2B4` выполняет `mulss` на `0x1402CC407`. Выражение:

```text
weight = (input0 * smoothedCurveSpeed) * camera.additiveCameraMovementsWeight
```

Получатель — **AnimNode_BlendAdditive** `0x202C1564B20`, vtable
`0x142B54990`; `[node+0x128]` указывает на expression. `0x14017BDE8`
пишет его результат в **`[state]+0x8614`**.

В положительном speed sample этого input эпизода результат остаётся 0;
в отдельном paused identity sample camera weight slot равен 0.
`0x1402C7E6C` использует этот blend weight и при `abs(weight)<=0.01`
проходит base-pose branch. Полный live sample поз этой additive-ветви
не выполнялся, а её содержимое не объявлено походкой ног.

## Граница результата и следующий участок

Подтверждены transport feature, dynamic property binding, фактическое чтение
speed, rate limit и один camera-gated downstream. Осталось установить
веса lower-body consumers / clip selection, vector consumers movementDirection
и state-dependent paths. Последующее исследование установило реальный
Locomotion graph slot, speed condition и clip Sample:
[player graphs, PID1840/5684/17268](cp2077-native-player-graphs.md).

Попытка поймать другой speed-reader дала нестабильный debugger stop:
state endpoint сообщил paused, следующий register endpoint — not paused.
Этот эпизод исключён из положительных и отрицательных выводов о consumers.

## Проверка и артефакты

`render_camera_RE/scripts/verify_native_movement_evidence.py` проверяет raw lengths,
FNV hashes, offsets, same-call stack guards, точные copies и численную математику.

В `render_camera_RE/analysis/`:

- `player_locomotion_feature_path_1840.md`
- `player_locomotion_feature_consumer_1840.md`
- `player_locomotion_feature_graph_copy_1840.md`
- `player_locomotion_property_binding_1840.md`
- `player_locomotion_property_copy_dispatch_1840.md`
- `player_locomotion_float_slot_copy_1840.md`
- `player_locomotion_speed_curve_1840.md`
- `player_locomotion_curve_math_and_binding_1840.md`
- `player_locomotion_expression_chain_1840.md`
- `player_locomotion_downstream_pose_node_1840.md`
- `player_locomotion_expression_evaluation_1840.md`
- `player_locomotion_verified_field_names_1840.md`
- `player_locomotion_direction_math_1840.md`
- `player_locomotion_gate_names_1840.json`
- `player_locomotion_blend_rtti_1840.json`
- `player_locomotion_graph_types_1840.json`
- `player_movement_feature_rtti_1840.json`
- `native_vroff_auto_input_1840.jsonl`
