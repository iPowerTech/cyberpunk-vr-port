# VR-off native RE — PID 19316

2026-09-13. Продолжение [PID 9976](cp2077-native-vr-off-9976.md).
Статика: существующая IDB через headless Python без autoanalysis/сохранения.
Live: x64dbg. [Raw observations](cp2077-native-vr-off-19316.json).

## Новый запуск

- PID **19316**, установленный Steam EXE; VR DLL отсутствует.
- Runtime imagebase **`0x7FF7D0FE0000`** — отличается от прежних запусков.
- HWND **`0x3058C`**.
- EXE SHA-256 снова проверен:
  `a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991`.
- Все pointers получены заново, в частности native entry `0x1404C2018`:

| Объект | Runtime |
|---|---|
| provider P | `0x28D32A36110` |
| movement S | `0x28D32A37110` |
| owner E | `0x28D326E84C0` |
| root placed T | `0x2893C3B9B90` |
| первый child T, animated root A | `0x28D326E96E0` |

Автоввод использует сохранение/возврат focus и release-all после свободного
resume. Журнал: `render_camera_RE/analysis/native_vroff_auto_input_19316.jsonl`.

## Подтверждённый script stack приседа

Автоматический C, pulse `7643f0e131964a2686e5bb55f2b58ade`, останов после
вычисления аргумента native **`LocomotionParameters.SetCapsuleHeight`**:
`0x141246436`, stack float `[RSP+0x30]=1.0`.

В RBX находится CStackFrame. В этой сборке layout из SDK согласуется с
используемыми инструкциями: function `+8`, context `+0x40`, parent `+0x48`.
В CBaseFunction shortName `+0x10`, declaring class `+0xB0`; CClass.name `+0x18`.

Из parent frames извлечены shortName hashes и declaring-class hashes.
Они сверены по FNV1a64 с именами в локальном script reference:

```text
CrouchEvents.OnEnter
  -> LocomotionGroundEvents.OnEnter
    -> LocomotionEventsTransition.OnEnter
      -> SetLocomotionParameters
        -> GetStateDefaultLocomotionParameters
          -> native SetCapsuleHeight(1.0)
```

Текущий context имеет RTTI name `CrouchEvents` (`0x59EE2CEF42E5639E`).
Другие подтверждённые hashes:

| Имя | FNV1a64 |
|---|---|
| `GetStateDefaultLocomotionParameters` | `3A7E98488584AFE8` |
| `SetLocomotionParameters` | `BF65DAA03BC09932` |
| `OnEnter` | `F96D8C7617882DB6` |
| `LocomotionEventsTransition` | `1708C11A3011C59A` |
| `LocomotionGroundEvents` | `0AA8E0C297BC3CC2` |

Непосредственный источник height: scripted helper вызывает
`GetStaticFloatParameterDefault("capsuleHeight", 1.8)`; в пойманной crouch
ветке результатом был **1.0**. Сам default 1.8 не означает, что crouch берёт
именно default. Чтение конкретного static parameter record ещё не прослежено.

Native `0x1412463E4` декодирует float argument через opcode table и пишет
`parameters+0x50=value`, `parameters+0x54=1`. После этого доказанный в PID 9976
merge optional parameters и property 29 ведёт к CCT. Parent-stack доказательство
в этом запуске закрывает script-часть, ранее известную только по reference.

Reference:
`engine_re/redscript_ref/cyberpunk/player/psm/locomotionTransitions.swift`,
строки 142–168, 444–454, 661–685, 1331–1353.
Не все действия между показанными вызовами обозначены на этой сокращённой цепи.

## Артефакты

- `render_camera_RE/analysis/native_vroff_script_and_skin_api_19316.json`
- `render_camera_RE/analysis/native_vroff_script_float_and_temp_handle_19316.md`
- `render_camera_RE/analysis/native_vroff_script_set_capsule_height_9976.md`
- `docs/cp2077-native-vr-off-19316.json`

Полная цепь script animation parameter → graph consumers → render skinning
ещё исследуется. Размер CCT и animation pose — разные наблюдаемые ветви.

## `crouch` как отдельный scalar animation input

Регистратор `0x1418702E0` связывает имя `SetAnimationParameterFloat` с native
handler **`0x140F75398`**. Handler декодирует CName и float, разрешает owner
handle из script interface и вызывает `0x140F754A0`.

### Выход из приседа: разрешение target

Автоматический C (`9c9fcbef08544b5eb1032edb6843792e`) пойман в
`0x140F754A0` с owner E, именем `crouch` (`8935793DDA14500B`) и float **0.0**.
До resume была установлена следующая точка на `0x1402E0C44` с тем же именем.
Она дала target **`N=0x2893DD57490`** и return `0x140F754F6`.

Это конкретно fallback-ветка `0x140F754A0`, после `0x1401A78C0`, а не
доказательство исполнения рекурсивной attached-entity ветки `0x140F75570`.
N имеет vtable `0x142ABE1A8`. Его нельзя путать с animated root A.

### Вход в присед: source script → scalar store

Следующий автоматический C (`add1dfafffe7411e84713eb37fd8460f`):

1. Stop `0x140F75423` после decoding аргументов.
   Script frame shortName **`OnEnter`**, declaring class **`CrouchEvents`**,
   CName **`crouch`**, float **1.0**.
2. Следующая data-точка на найденном entry дала store **`0x1402E0D4C`**,
   stop `0x1402E0D51`.
3. Destination `0x28D4FFA0F20+8`: bytes **`0000803F`**.
   Return `0x1402E0C8D` связывает запись с scalar update wrapper.

Хранилище **`Q=0x2893CCC44C0`** получено из `[N+0xC8]`.
`Q+0x10` содержит array `0x28D4FFA0D30`, count `Q+0x1C=32`, stride 16;
`crouch` — entry **31**. До входа оно содержало float 0.0.

### Запись не равна исполнению графа

Инструкции `0x1402E0C44/0x1402E0D88/0x1402E0CF0` показывают:

- блокировку `N+0x1DB` на время обновления;
- создание Q при отсутствии и установку `N+0x1D8=1`;
- обновление текущего scalar entry в `Q+0x10`;
- при существующем entry `abs(old-new) <= 0.005` ранний выход;
- регистрацию изменения через `0x140AA4124` в другом массиве **`Q+0x20`**.

Эта вторая таблица отделена от текущих значений. Её consumer и связь с input
buffers конкретного AnimGraph требуют трассировки. Read/write watch на первом
entry после завершения перехода не дал чтений; из этого не выводится, что граф
не получил параметр — consumer может брать pending changes/копию.

Артефакты:

- `native_vroff_crouch_scalar_routes_19316.md`
- `native_vroff_animation_scalar_writer_19316.md`
- `native_vroff_scalar_storage_and_skin_binding_19316.md`
- `native_vroff_scalar_graph_table_19316.md`

Все они в `render_camera_RE/analysis/`. Имена и значения двух разных
переходов сохранены отдельно в JSON, без объединения их в один кадр.
