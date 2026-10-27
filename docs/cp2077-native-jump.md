# Обычный прыжок: подтверждённый путь через LocomotionSimple

2026-09-13, VR-off PID **1800**, imagebase `0x7FF7D0FE0000`.
Этот процесс завершён/перезапущен; все pointers ниже исторические.
Статика: существующая CP2077 IDB headless Python; live: x64dbg.
[Raw evidence](cp2077-native-jump-1800.json).

## Почему не сработал первый breakpoint

Первый тест Space фильтровал entry `0x1406AB804` по vtable
`gamestateMachineplayeractionsLocomotionAir` (`0x143137700`).
Пользователь подтвердил: **«прыгнул персонаж но бп не сработал»**.
Этот отрицательный результат не использован как доказательство выбранного
action или отсутствия класса во всех стадиях полёта.

Повторный тест отслеживал **реальную положительную запись P+0x88**, без
предполагаемого имени action. Он поймал `0x1406ABA80` → stop `0x1406ABA88`.
Return address восстановлен по инструкциям prologue:

- `[RBP+0x98] = 0x1406AB91F`, вызывающий `0x1406AB804`;
- сохранённый caller RSI, `[RBP+0xA8] = L=0x2149CD4EAA0`;
- vtable L = **`0x142BABF38`**, подтверждённый **LocomotionSimple**.

Именно этот вызов создал начальный upward displacement. Нативный класс action
не следует выводить из имени script superclass `LocomotionAirEvents`.
Примечание: Air в vtable тоже использует `0x1406AB804`; отличие определяется
конкретным объектом и параметрами, а не одним адресом Update.

## 1. Начальный upward request

Авто-Space `e6d5b09ad6054ba1829c1f20fbdee9f3`:

| Поле | Значение |
|---|---|
| provider P | `0x218AFDF8950` |
| solver | `0x228B5B12150` |
| timestep | **0.025812000036239624 s** |
| request Z | **0.1599511206150055 m** |
| request Z / timestep | около **6.1967736 m/s** |

Останов был после Z store, перед следующими X/Y stores. В этом тесте X/Y
прочитаны как нули; общее окончание всей XYZ-записи этим ранним stop не обозначается.

В отдельном последующем Space поймана запись результата CCT:
`0x1402BF8D4 → 0x1402BF8DC`, backend B=`0x214AE898390`.
Его vertical velocity = **6.196773529052734**, dt=**0.016420599073171616**,
pending displacement Z=**0.10175473242998123**.
На этом stop pending Z ещё не очищен — это промежуточная инструкция CCT tick.

## 2. Источник скорости: JumpHeight и upwardsGravity

Найден и пойман **`0x1404C3BA4`**. Функция вызывается из
`0x1406ABB18` на `0x1406ABDDC`.

```text
h = GetStat(owner, 927)
если alreadyApplied или h == 0:
    return false
scratch.verticalVelocity = sqrt(2 * abs(parameters.upwardsGravity) * h)
return true
```

Точные инструкции:

```asm
0x1404C3BBD mov r8d, 39Fh          ; stat id 927
0x1404C3BCB call 0x1404C47A0
0x1404C3BD0 test bl, bl            ; already-applied guard
0x1404C3BEF movss xmm1,[rsi+40h]   ; upwardsGravity
0x1404C3BF6 andps xmm1,absMask
0x1404C3BFD mulss xmm1,xmm0        ; |g| * h
0x1404C3C04 addss xmm1,xmm1
0x1404C3C08 sqrtss xmm0,xmm1
0x1404C3C0C movss [rdi+E8h],xmm0
```

Имя **JumpHeight=927** подтверждено **в бинарнике**, не только SDK:
`0x141C92AE7` задаёт R9D=`0x39F`, `0x141C92AED` — строку `JumpHeight`,
`0x141C92AFE` регистрирует пару. Соседний stat 928 — JumpSpeedModifier.

`0x1404C47A0` обращается к stats-system virtual slot `+0x1E8`, передавая
owner StatsObjectID из `owner+0x48` и stat id. Это не чтение позиции кости.

## 3. Сопоставленный trace от расчёта до displacement

Авто-Space **`2b8e98e37f5d47cba8470c706120ce2e`**; точки подготовлены до resume
предыдущей. Использованы только исходные вызовы движка.

1. **`0x1406ABB18`**, вход в solver:
   - solver `0x228B5B12150`;
   - output `0xB31E0FF090`;
   - scratch `[RSP+0x28]=0xB31E0FF0A0`;
   - action `Jump` имеет тип **BUTTON_PRESSED=0**, value 1;
   - сырая velocity B перед расчётом равна нулю.
2. Watchpoint scratch+0xE8:
   - stop **`0x1404C3C14`**, непосредственно после sqrt/store;
   - parameters=`0x214BD72AEB0`;
   - `upwardsGravity` bits **C199999A**, float **−19.200000762939453**;
   - вычисленный vZ bits **40C64BF8**, float **6.196773529052734**.
3. Возврат этого же solver в **`0x1406ABA4B`**, с проверкой solver и RSP:
   - dt bits **3C2DA702**, float **0.01059889979660511**;
   - output Z **0.06567898392677307**;
   - **float32(vZ × dt) побитово совпадает с output Z**.

Само умножение на dt находится на `0x1406ABF2F/0x1406ABF39`.
Далее вызывающий `0x1406AB990` записывает output в P+0x80/+0x84/+0x88,
и действует уже проверенная цепь physics property 9 → CCT → entity transforms.

### Что измерено, а что вычислено

Фактический ответ JumpHeight из XMM0 **отдельно не снимался**: используемый
MCP register endpoint не предоставляет XMM0/XMM1. Не вводились инструкции
или вызовы движка для извлечения значения.

Из измеренных g и vZ получается эквивалентное **h≈1.000000017**; подстановка
h=1.0 в установленную float32 формулу даёт точно те же bits vZ. Это проверка
согласованности, **не заявление о прямом чтении stat value 1.0**.

## 4. Обновление скорости после первого шага

`0x1406AC878` выбирает upwards/downwards gravity по знаку входной vertical
velocity. `0x1406AC5D4` делает `g*dt + базовая vertical velocity`, результат
пишет в scratch+0xE8. У sqrt-helper есть guard alreadyApplied; caller
`0x1406ABB18` при успешном результате включает свой флаг `solver+0x58`.

Тем самым первый подъём в пойманном пути задан вычисленной jump velocity,
а не извлечением displacement из skeletal pose. Полная дуга до приземления
и специальные варианты charge/hover/double jump этим trace не исчерпаны.

## 5. Input event table — не таблица состояния клавиатуры

Input object `0x214A2770E90`, array `0x218D21402F0`, count 85, stride 48.
Entry `Jump` по `0x218D2140A10`, FNV1a64=`15A477E9F9A165AD`.

До нового нажатия наблюдались type **3** и value **1**. Регистратор
`0x141094B40` доказывает:

- 0 = BUTTON_PRESSED;
- 1 = BUTTON_RELEASED;
- **3 = BUTTON_HOLD_COMPLETE**.

Type 3 нельзя называть release. Одно старое value=1 не доказывает ни свежий
press, ни зависшую OS-клавишу. В сопоставленном trace дополнительно проверены
type=0, новый автоматический input ID и его down/up timestamps.

## 6. Root motion и прочие специальные действия

Этот результат **не доказывает отсутствие root motion в игре вообще**.
Он закрывает начальный обычный прыжок в наблюдавшейся LocomotionSimple.

Отдельные подтверждённые статические пути:

- `PlayerMoveOnSpline` имеет собственный embedded provider, vtable
  `0x1431DB8F0`; getter `0x141902D30` возвращает подготовленные translation
  и quaternion. Имя action возвращается `0x142988164`.
- Vault action Update `0x14297B588 → 0x1429834C0` пишет P.pendingMovement.
  `0x142983098` в состояниях 1/2 вычисляет
  `normalize(direction[+0x70]) * dt * speed[+0x90]` и переключает фазу по elapsed.
  Этот участок математически описывает движение по подготовленному направлению,
  но источник direction и весь vault-state cycle ещё не замкнуты live.

Нельзя автоматически приписывать этим механизмам чтение K.motion или bone0.
Активный путь skeletal root-motion extraction → movement пока остаётся
отдельной незакрытой задачей.

Следующий [PlayerClimb trace](cp2077-native-climb-motion.md) показал actual
motion controller и placement interpolation → physics position request /
T.local. В пойманном record sampling object был null; extractor вернул
identity. Этот специальный путь не переносится на обычный jump и не закрывает
случай ненулевого extracted clip motion.

## Артефакты

В `render_camera_RE/analysis/`:

- `jump_formula_and_special_action_delta_1800.md`
- `jump_stat_and_input_enum_verified.md`
- `jump_vertical_velocity_helpers_1800.md`
- `jump_motion_scratch_layout_1800.md`
- `air_vault_action_dispatch_1800.md`
- `rootmotion_action_update_chain_1800.md`
- `rootmotion_delta_provider_writers_1800.md`
- `vault_trajectory_delta_1800.md`
- `native_vroff_auto_input_1800.jsonl`

Численные проверки и byte lengths включены в `verify_native_movement_evidence.py`.
