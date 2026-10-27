# VR-off native RE — PID 9976

2026-09-12. Новый запуск после PID 21236; base и pointers определены заново.
Старые наблюдения находятся в [отчёте PID 21236](cp2077-native-vr-off-21236.md).
Автоматический ввод разрешён пользователем; [протокол](cp2077-native-input-automation.md).

## Идентификация

- EXE: Steam `Cyberpunk 2077/bin/x64/Cyberpunk2077.exe`.
- Runtime imagebase **`0x7FF73E2A0000`**, IDA imagebase `0x140000000`.
- `CyberpunkVR_Stereo.dll` отсутствует.
- Game HWND **`0xF06D4`**.

| Объект | Runtime address |
|---|---|
| LocomotionSimple action L | `0x28592DDC260` |
| Move component S | `0x2899E5F2580` |
| Provider P | `0x2899F10BB30` |
| Owner E | `0x2899E930B70` |
| Root placed T = `[E+0xB0]` | `0x285AB708990` |
| CCT backend B | `0x2859AC3A5F0` |
| Active solver `[L+0x150]` | `0x299A9751E80` |
| Active parameter object `[L+0x158]` | `0x2858AC59C20` |

L найден native entry `0x1406AB804`; его vtable равна `0x142BABF38`,
owner подтверждён в update context `+0x10`. P/S найдены entry
`0x1404C2018`, `RCX=P`, `R15=S`. Physics handle `P+0xC4=0x00010015`.
Registry `0x299A37D0000`; запись index `0x15` указывает на B,
generation по `registry+0x102010+index*2` равна 1.

## W: принятие и release

Автоматический input `344a6179ef0b4dd8aeba6dda2adcd68d` дал в native
`0x1404C659C` значение `L.xy=(0,1)`.
После resume и release-all следующий native sampling на том же L подтвердил
`(0,0)`. Чтение по свободно работающей игре не используется вместо проверки
актуального sampling события.

## Optional parameters → CCT geometry: автоматический crouch

Параметры по `0x2858AC59C20` имеют vtable `0x142C4A230`.
Статическая структура merge проверена в **`0x14108B898`**:

```asm
0x14108B8FB cmp byte ptr [rdx+54h], r8b  ; r8b = 0
0x14108B8FF jz  skip
0x14108B901 mov eax, [rdx+50h]
0x14108B904 mov [rcx+50h], eax
0x14108B907 mov byte ptr [rcx+54h], 1
```

Таким образом, `+0x50` копируется только при наличии source flag `+0x54`;
merge не является безусловным обнулением/заменой всех optional fields.

### Down transition

Автоматический C, input ID `29ad1f5d66f04b6db8cc000403a92eed`:

1. Watchpoint на active parameters+0x50 дал stop **`0x14108B907`**.
   - destination RCX `0x2858AC59C20`;
   - source RDX `0x2858FB3C260`;
   - source+0x50/+0x54: `0000803F01000000` (1.0, flag=1);
   - destination+0x50/+0x54: те же bytes;
   - return `[RSP] = 0x1406A9B94` — merge из `0x1406A9B50`;
   - **B+0x138 ещё 1.8** на этом останове.
2. До resume подготовлен watchpoint на B+0x138.
3. Следующий stop **`0x1409261B9`**, RCX=B, R9=29:
   `B+0x138/+0x13C = 0000803FCDCCCC3E` (1.0, 0.4).

### Up transition

Следующий автоматический C, input ID `d956b1a73a5c4f0295fedd91d8f0dc49`:

- stop `0x1409261B9`;
- active parameters+0x50/+0x54 = `6666E63F01000000` (1.8, flag=1);
- B+0x138/+0x13C = `6666E63FCDCCCC3E` (1.8, 0.4).

После каждого законченного прохода игра продолжена, отправлен release-all,
фокус возвращён исходному HWND. Toggle C возвращён к исходному состоянию
вторым нажатием; сами key-up не считаются отменой toggle.

## Значение geometry fields

`0x140926020`, property 29/30, вызывает **`0x1418CD9B4`**:

```text
controller = [B+0x60]
virtual +0xC0 получает (B+0x138 - 2*B+0x13C)
virtual +0xD8 получает B+0x13C
B+0x186 = 1
```

Это подтверждает различие total height и radius в wrapper и передаваемой
цилиндрической части capsule. На этапе property store размеры уже изменены,
но сама поза и все последующие component transforms ещё не обязаны быть
обновлены. Отдельный manual crouch PID 21236 показал эту границу визуально
и по model bone камеры; его pointers/снимки не переносятся в этот запуск.

## Артефакты

- `render_camera_RE/analysis/native_vroff_auto_input_9976.jsonl`
- `render_camera_RE/analysis/native_vroff_locomotion_parameters_layout_9976.md`
- `render_camera_RE/analysis/native_vroff_cct_dimension_setter_21236.md`
- [raw observations](cp2077-native-vr-off-9976.json)

Источники перехода в script/PSM до создания source parameter object,
остальные locomotion states и весь skinning pipeline ещё исследуются.
