# Ненулевой motion sampler: WorkspotMovementController другого actor

2026-09-17, PID23736. [Raw snapshots](cp2077-native-nonnull-motion-sampler-23736.json).

**Поймана рабочая ветка motion extraction с ненулевым результатом.** Owner
этого controller — **не игрок**. Результат подтверждает реализацию sampler
и уточняет сценарий для дальнейшей player-проверки.

## Live identity

```text
player entity       0x14899337420
observed owner      0x148C016C2C0
move component      0x148C016DBD0
controller          0x148B2704A90
motion record       0x1448330B7F0
binding=record+D8    0x1448330B8C8
sampler=record+F0    0x144841FEE40
```

На `0x1403262AE` RCX=sampler, RDI=binding. Это non-null branch; возврат
`0x1403262C9` от `0x140326C30` сопоставлен по binding **и RSP**.
Извлечённый transform:

```text
XYZ = (0.011832489632070065, 0.11515039205551147, 0)
Q   = (0, 0, -0.010205212980508804, 0.9999479055404663)
S   = (1, 1, 1)
```

Затем `0x1402D07CD` пойман для controller с тем же record/sampler.
Controller+0x80 указывает на move component, его +0x90 — на observed owner.
Это связывает объекты; **этот второй stop не имел RSP-фильтра**, поэтому
не используется как доказательство единственного вызова extraction→provider.

## Тип controller

Live vtable rebases в **`0x142ABAD88`**:

- +0x18=`0x1402D0658` — общий Update animation-motion controller;
- +0x20=`0x140AEDBDC` — delta getter;
- +0x58=`0x1418F8960` — возвращает `qword_143477618`;
- `0x1425D9428` регистрирует это имя как **`WorkspotMovementController`**.

Поэтому совпадение Update с AnimMotionMoveController не означает совпадение
конкретного типа. Первоначальное предположение о base-vtable было отвергнуто
проверкой snapshot, после чего дочерний тип установлен через vtable/name chain.

## Sampler и таблица ключей

Live vtable **`0x142B7F588`**, sample virtual+0xD8=`0x140326844`.
Конструктор `0x140C4F710` записывает этот vtable; его pointer находится в
type descriptor `0x142E5ECE8+0xD8`. Инициализация descriptor
`0x14179C7E4` использует имя **`animPlaneUncompressedMotionExtraction`**,
размер0x48; SDK layout согласуется:

- duration +0x30;
- array pointer +0x38;
- capacity/count +0x40/+0x44, в sample **54/54**.

Статика `0x140326844`:

1. `u=(count-1)*time/duration`, clamp через отдельные крайние ветви;
2. интерполяция соседних ключей с stride12;
3. первые два float — **X/Y**, третий — **yaw degrees**, не Z;
4. yaw wrap при разнице >180°, перевод в половинный угол через π/360;
5. output translation Z=0, quaternion `(0,0,sin,cos)`, scale1.

`0x140326C30` семплирует два момента через virtual+0xD8 и вычисляет относительный
transform через `0x1401DBB00` (с отдельной ветвью для пересечения циклов).
Имена/путь animation asset и сырые54 ключа в этом trace не сняты; формула
интерполяции отдельных ключей пока только статическая.

## Ограниченная проверка игрока

На `0x1402D07CD` выставлен фильтр player owner и ненулевого record+0xF0.
Короткий W+LeftCtrl0.12s (`994fb57ec214477ba038f1601df88759`) не дал
квалифицированного stop. Доставка input проверена; это не доказательство
выполненного Dodge и не исключение root motion в других состояниях.

**Следующий сценарий:** доступное игроку анимированное workspot-взаимодействие,
например вход в сиденье/кровать. Сначала подтвердить фактический player owner
и controller, затем снять sampler и keys. Само название сценария не гарантирует
ненулевое движение — это проверяемая гипотеза, основанная на пойманном типе.

Breakpoints после probes удалены, release-all выполнен.

## Артефакты

`render_camera_RE/analysis/`:

- `nonnull_motion_sampler_identity_23736.md`;
- `nonnull_motion_identity_catalog_23736.json`;
- `nonnull_motion_controller_type_23736.md`;
- `motion_sampler_rtti_and_scenarios_23736.md`;
- `workspot_motion_controller_name_23736.md`;
- `native_vroff_auto_input_23736.jsonl`.

`verify_native_movement_evidence.py::verify_nonnull_motion_sampler` проверяет
размеры, links/vtables, non-identity output, норму Q и исключение player owner.
