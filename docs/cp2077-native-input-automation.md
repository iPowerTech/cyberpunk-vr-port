# Автоматический input для native RE

Разрешён пользователем 2026-09-12. Live-наблюдения по-прежнему делаются через
x64dbg; автоматические действия явно отделяются от пользовательских жестов.
Утилита: `render_camera_RE/scripts/native_re_input.py`.

## Текущий протокол

1. Проверить PID и полный путь EXE; определить видимое client window игры.
2. Сохранить **точный текущий foreground HWND и PID**.
3. Активировать окно игры и проверить фактический foreground.
4. Отправить ограниченный по длительности scan-code input через `SendInput`.
5. В `finally` отправить key-up для всех затронутых клавиш.
6. Подержать фокус игры ещё 0.2 s для обработки release, затем вернуть
   сохранённый HWND и проверить возврат.
7. После продолжения из x64dbg, когда игра идёт свободно, выполнить отдельный
   **`--release-all`** с таким же сохранением/возвратом фокуса.

`--release-all` отправляет key-up для всех отображаемых Win32 scan codes,
включая тестовые keys, и release пяти кнопок мыши. В проверенном запуске:
137 keyboard releases + 3 mouse events (пять кнопок), всего **140 SendInput events**.
Новых key-down этот режим не создаёт. Release не отменяет toggle-состояния
вроде уже включённого crouch toggle; такие состояния проверяются отдельно.

Каждый input записывается в JSONL с PID, HWND, методом, scan codes,
timestamps, результатами SendInput, фактическим foreground и результатом его
восстановления. Успех Win32 API сам по себе не считается подтверждением
обработки input игрой.

Операции изменения состояния x64dbg выполняются **последовательно**:
установка, условие, проверка точки, resume, удаление. Две установки/удаления
не отправляются параллельно. Read-only снимки независимых адресов разрешено
группировать, когда debuggee достоверно paused. Для after-call точки нужны
фильтры объекта **и сохранённого RSP**: один адрес возврата используется
несколькими actor/jobs и без фильтра может поймать чужой вызов.

## Проверенные привязки клавиш

Проверены `r6/config/inputUserMappings.xml` и текущий `UserSettings.json`:

| Действие | Key |
|---|---|
| forward/back/left/right | W/S/A/D |
| crouch toggle | C |
| crouch hold | **Right Ctrl** |
| sprint toggle | Left Shift |
| sprint hold | Right Shift |
| walk toggle | G |
| dodge/dash | **Left Ctrl** |

Left Ctrl нельзя автоматически считать кнопкой приседа в этой конфигурации.

## Проверки PID 21236 (исторический, процесс перезапущен)

- Автоматический `SendInput W`, 0.05 s, pulse ID
  `b4081753016d4f299648e1b94f3b2b4e`:
  native writer остановился на `0x1404C659C`, `MoveXY=(0,1)`.
- `PostMessage(WM_KEYDOWN/WM_KEYUP)` в окно игры, pulse ID
  `758bb1f1007c4661bb739520d6f8a7ac`: API принял сообщения, но в этом тесте
  квалифицированное `MoveY=1` не наблюдалось. Foreground принадлежал другому
  процессу. Это не доказательство поддержки PostMessage игровым input.
- Исходная простая активация через один `SetForegroundWindow` иногда не
  получала фокус; утилита не отправляла keys при неудачной проверке.
- После этого пользователь потребовал save/focus/pulse/restore и отдельный
  release после resume. Схема реализована в текущей версии helper.

## Проверки нового PID 9976

Заново проверено:

- EXE `Cyberpunk2077.exe` из Steam install;
- moduleBase `0x7FF73E2A0000`;
- `CyberpunkVR_Stereo.dll` отсутствует;
- game HWND **`0xF06D4`**;
- текущая LocomotionSimple action **`0x28592DDC260`**, vtable
  `0x142BABF38`;
- owner в action context **`0x2899E930B70`**.

Проверенный focus/release цикл, log ID `96ea71dc550c49d69fe3068f73d7eb1a`:

```text
saved HWND 0x10400 (Firefox PID 4548)
foreground -> 0xF06D4 (game PID 9976)
SendInput accepted 140/140 release events
foreground restored -> exactly 0x10400
```

Автоматический W, log ID `344a6179ef0b4dd8aeba6dda2adcd68d`:

- key-down и key-up приняты SendInput, длительность около 0.05 s;
- foreground на момент отправки принадлежал PID 9976;
- сохранённый HWND `0x13004A` возвращён точно;
- x64dbg поймал `0x1404C659C`, action+`0x90/+0x94` = bytes
  `000000000000803F` → **(0,1)**.

После resume выполнен release-all. Проверка **на следующем native MoveXY
sampling**, а не только чтением поля последнего sample во время свободного хода,
дала на том же action pointer:

```text
RIP = 0x1404C659C
RSI = 0x28592DDC260
qword:[RSI+0x90] = 0  -> MoveXY=(0,0)
```

Release-проход с 0.2 s ожиданием сохраняет foreground игры на всём этом
интервале, затем возвращает прежнее окно; проверенный log ID
`ad105fe93362464eb1a3d64998c4e5d5` вернул Firefox HWND `0x10400`.

Логи:

- `render_camera_RE/analysis/native_vroff_auto_input_21236.jsonl`
- `render_camera_RE/analysis/native_vroff_auto_input_9976.jsonl`

Адреса объектов PID 21236 не используются в PID 9976. После нового запуска
объекты и base address снова определяются заново.

## Автоматический C toggle и проверка CCT, PID 9976

Input ID **`29ad1f5d66f04b6db8cc000403a92eed`**, C down/up, 0.05 s:

- Перед input сохранён Firefox HWND `0x10400`.
- Фактический foreground при отправке — game PID 9976, HWND `0xF06D4`.
- Key-down/key-up приняты, после release interval вернулся HWND `0x10400`.
- Engine observation: `0x14108B904` копирует value 1.0 из source parameters
  `0x2858FB3C260+0x50` в active parameters `0x2858AC59C20+0x50`.
- В следующем подготовленном останове property 29 записывает CCT value 1.0.
- После свободного resume выполнен release-all, input log ID
  `57a080aeb33e456b82c68a1a08b87595`.

Обратный input **`d956b1a73a5c4f0295fedd91d8f0dc49`**, C down/up, 0.05 s:

- Опять сохранён/восстановлен тот же Firefox HWND.
- Engine observation: active parameters value 1.8 и CCT property 29 = 1.8.
- После resume выполнен release-all, input log ID
  `23d1f125a78546cd8e2d10fcba892af6`.

То есть подтверждён не только возврат SendInput, но и конкретные engine writes.
Подробнее и raw values: [PID 9976](cp2077-native-vr-off-9976.md).

## Удалённый hardware watch и восстановление отладчика, PID21660

В probe K.motion+0x30 список breakpoints стал пустым после удаления, но
запись с тем же адресом оставалась в DR0/DR7 потоков. Обычные register-clear
commands не дали устойчивого результата; release-all один раз завершился
таймаутом. Этот эпизод исключён из выводов о наличии/отсутствии root motion.

Отдельный read-only Win32 context audit показал собственный watch в73 потоках.
Утилита `render_camera_RE/scripts/clear_owned_debug_watch.py` очистила только
совпадающие по **точному адресу** DR slots. После resume запись появилась
снова в72 потоках; причина её повторного восстановления не установлена.

Достигнутое восстановление:

1. Очистить собственные matching slots в stopped session.
2. Выполнить detach от того же PID.
3. Повторный audit показал **0 matching slots в69 потоках**.
4. Успешно выполнить release-all с возвратом прежнего HWND.
5. Подключиться к тому же живому процессу заново; breakpoint list пуст.

Helper проверяет PID/EXE, по умолчанию read-only, использует сбалансированные
SuspendThread/ResumeThread и не пишет game memory. Его --clear само по себе
**не доказывает** устойчивую очистку: нужен последующий audit.
Файлы context reports и exact input IDs указаны в
[debugger-maintenance JSON](cp2077-native-motion-watch-recovery-21660.json).

Дальнейший PlayerClimb trace снят software-breakpoints. Для продолжения этой
RE-сессии предпочтительны узкие software sites; длительный условный hardware
watch здесь не повторять.

Headless IDA scripts используют одну и ту же IDB; такие команды запускать
**последовательно**, иначе второй `open_database` может получить lock failure.

PID23736 также показал гонку при установке software BP на свободно работающей
игре: первый hit может произойти **до** следующего вызова установки condition.
Поэтому после stop всегда проверять фактические registers/owner/property.
Предварительный stop property27 после установки BP на `0x140926020` исключён;
в принятых property1 snapshots R9=1 прочитан непосредственно.
