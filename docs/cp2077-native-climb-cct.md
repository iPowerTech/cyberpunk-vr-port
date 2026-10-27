# PlayerClimb: buffered property1 → PhysX CCT

2026-09-17, новый PID **23736**, EXE base `0x7FF702460000`.
[Raw evidence](cp2077-native-climb-cct-23736.json).

**Фактическое применение buffered position подтверждено.** В отдельном
PlayerClimb update совпали controller world XYZ, payload запроса, bytes в
буфере и payload у consumer. После setter совпали backend world position и
рассчитанная позиция центра PhysX CCT.

Это продолжение [предыдущего залезания](cp2077-native-climb-motion.md),
снятое в новом процессе; снимки разных PID не объединяются в один вызов.

## Объекты и условия

| Объект | Адрес |
|---|---|
| Player E | `0x14899337420` |
| Move component S | `0x14899339B80` |
| Physics adapter | `0x14498E68980` |
| Handle | `0x10015` |
| Registry | `0x1589CBE0000` |
| Backend B | `0x1448A3C5FA0` |
| PhysX CCT C | `0x1489CAD09C0` |
| PlayerClimb action / controller | `0x1589C8B5A00` / `0x1589C8B5A50` |
| Buffer owner Q | `0x1589B150000` |

Identity восстановлена из LocomotionSimple context, S+0x90, physics handle,
registry generation/index lookup, B+0x60 и vtables. VR DLL отсутствует.
Input: два коротких W+Space по0.12s с сохранением/возвратом HWND, затем
release-all. Exact IDs сохранены в JSON и input log.

## 1. Producer и реально прочитанная запись

На `0x140D8178E` перед `0x140240998`:

- handle=`0x10015`, owner=Q, R15=adapter;
- stack arg6 (**до call RSP+0x28**) =1;
- payload pointer=RSP+0x30, size=12;
- XYZ совпадает с active PlayerClimb controller+0x60.

Возврат `0x140D81793` сопоставлен по adapter **и RSP**.
В связанном trace: buffer generation **`0x59872`**, payload offset **0**,
адрес **`Q+0x300060 = 0x1589B450060`**.

Позже на `0x140926020`:

- RCX=B, R9=1;
- return=`0x14024091C`, то есть вызов из buffered consumer `0x14024084C`;
- stack arg5 указывает на **тот же payload address**, size=12;
- record generation та же, payload bytes совпадают побитово.

### Почему нельзя фильтровать только первоначальный record pointer

После enqueue запись была по `Q+0x88`, а consumer прочитал header по `Q+0xB8`.
У прежнего header flags изменились **1→5**; новый header сохранил
handle/generation/property/offset и ссылался на те же bytes.

Статика `0x14024064C` при замене registry record устанавливает flag4 у старой
записи; `0x14024084C` пропускает headers с flags&6. Регистрация через
`0x140240720` вызывается из adapter после enqueue. Момент самой замены в
этом trace не остановлен, но обе стороны и совпадающий payload сняты.

Первый опыт с фильтром по старому адресу поэтому **не использован** как
доказательство применения первого запроса. Успешный связанный trace —
отдельный последующий update, с проверкой реальных аргументов consumer.

## 2. World position → положение стопы в сцене

`0x140926020` (property1) вызывает `0x1409261BC(B, XYZ, true)`:

1. записывает XYZ в **B+0x108/+0x10C/+0x110**;
2. обнуляет B+0x180;
3. вызывает CCT virtual+0x90;
4. передаёт world XYZ в `0x140926220`.

Последний helper получает сцену и вычитает float32 origin по
scene+0x134/+0x138/+0x13C. Live origin:

```text
(1024, -2048, 0)
footLocal[i] = double(float32(world[i] - sceneOrigin[i]))
```

Все три double аргумента на `0x14092628F` совпали **побитово**.
Это вызов **setFootPosition** (CCT virtual+0x28), а не setPosition центра.

## 3. Стопа → центр капсулы → actual write

Модуль `PhysX3CharacterKinematic_x64.dll`, base `0x7FFA9C000000`:

```text
virtual+0x28 → module+0x1E40
  center = foot + up * (radius + contactOffset + height/2)
  virtual+0x18 → module+0x2530
    this += 8
    module+0x16BB0 → store double XYZ
```

Captured fields:

- up=(0,0,1), C+0x28;
- contactOffset≈0.1, C+0x38;
- radius≈0.4, C+0x280;
- height≈0.2, C+0x284;
- суммарный float32 lift **0.600000024 m**.

Height — текущий параметр этого залезания, не универсальная стоячая высота.

Setter пишет XY в adjusted-this+0x208 и Z в +0x218, то есть относительно C:
**C+0x210..0x227**. Getter virtual+0x20 (`module+0x2440`) возвращает C+0x210.

До вызова сохранена старая позиция; на `module+0x1EBC` сохранён center
argument; на возврате `0x140926292` сохранены обновлённые24 bytes C+0x210.
**Аргумент и запись совпали точно**, AL=1. В следующем связанном update
расчёт повторно совпал с фактической CCT position.

Изменение world position связанного update:

```text
(-0.00146484375, +0.000732421875, +0.041667938232421875) m
```

## Граница результата

Подтверждено:

```text
PlayerClimb controller world XYZ
 → adapter property1 request
 → buffered payload, generation+offset
 → 0x14024084C → 0x140926020
 → backend world XYZ
 → scene-origin subtraction
 → PhysX setFootPosition → setPosition → CCT stored center
```

Это **позиционная синхронизация**, не измерение collision sweep. Продолжение
этого же update до T.world отдельно не снято; прежний T.local trace находится
в предыдущем документе. Ненулевой extracted clip motion также остаётся
открытым: текущий record+0xF0 снова null.

## Проверки и артефакты

`verify_native_movement_evidence.py` → `verify_climb_cct()` проверяет
длины snapshots, object/vtable links, ABI, queue descriptor, generation,
payload, float32→double origin conversion, capsule offset, machine-code
stores, input/release logs.

В `render_camera_RE/analysis/`:

- `property1_cct_dispatch_23736.md`;
- `property1_setposition_conversion_23736.md`;
- `property1_flush_routes_23736.md`;
- `property1_queue_rebinding_23736.md`;
- `property1_record_registration_23736.md`;
- `property1_physx_foot_setter_23736.bin` (224 bytes);
- `property1_physx_position_setter_23736.bin` (150 bytes);
- `native_vroff_auto_input_23736.jsonl`.
