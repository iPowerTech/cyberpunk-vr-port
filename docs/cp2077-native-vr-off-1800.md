# VR-off native RE — PID 1800

2026-09-13. Продолжение [PID 19316](cp2077-native-vr-off-19316.md).
Raw excerpts: [JSON](cp2077-native-vr-off-1800.json).

## Проверенный запуск и pointers

- Новый PID **1800**, game HWND `0x37029A`, VR DLL отсутствует.
- Imagebase заново прочитан: `0x7FF7D0FE0000`.
- IDB imagebase `0x140000000`; EXE/IDB identity сохраняется (см. catalog artifacts).
- P `0x218AFDF8950`, S `0x218AD34D570`, E `0x218B2C47620`,
  T `0x214A0B7E930` — найдены native entry `0x1404C2018`.
- A `0x218B2C062E0` — первый child T, vtable `0x142AE68B0`.
- Animation controller N `0x214B969E8D0`, scalar storage Q `0x214BA8D7EE0`.
- `K=[A+0x178]=0x214A5E60C80`; `D=[K+0x18]=0x2149B1B58A0`;
  `bones=[D]=0x218B7013FA0`, count 619, stride 48.

Software breakpoints использованы для code sites, аппаратные — для конкретных
data fields. Каждый следующий breakpoint в сопоставленной цепи подготовлен
до resume. Release-all выполняется после свободного resume с восстановлением
предыдущего foreground HWND. Input log: `native_vroff_auto_input_1800.jsonl`.

## Q.pendingScalars → рабочее состояние AnimGraph

Подтверждённое различие двух таблиц Q:

| Смещение Q | Содержимое |
|---|---|
| `+0x10` (array), `+0x1C` (count) | текущие scalar values, stride 16 |
| `+0x20` (array), `+0x2C` (count) | отложенные scalar changes, stride 16 |

После автоматического выхода из crouch (`2563afd1da584af79cccf93602de6a7a`)
stop `0x1402E0C8D` имел `RSI=N`, `RBX=hash(crouch)`.
Q.pending содержала 2 записи; `crouch=0` был по адресу `0x214BD804BA0`.
Watchpoint на его value поймал **`0x140329043`**, read
`movss xmm3,[rbx+8]` внутри **`0x140328D0C`**.

### Сопоставленный вход crouch=1

Авто-C `0db81ec06f1c4902b4fc8323df18e190`:

1. `0x140329053`, перед scalar setter:
   - graph instance G = `R14=0x218B2E25970`;
   - **`[G+0x2590]=Q`**;
   - `RCX=graph=0x214A0B7B0F0`, `RDX=state=0x214A0990BC0`;
   - `R8=8935793DDA14500B` (`crouch`);
   - pending entry `RBX=0x214BD804BB0`, value **1.0**.
2. Следующий stop **`0x140328758`**, после store `0x140328752`:
   - definition=`[graph+0x58]=0x218AF98CB80`;
   - state memory=`[state]=0x218B36839E0`;
   - definition scalar-buffer offset `+0x1F0` = **`0x9908`**;
   - resolved index R8 = **0**;
   - destination **`0x218B368D2E8`**, значение `0x3F800000`.

Формула адреса проверена:

```text
destination = [state] + uint32[definition+0x1F0] + 4*index
            = 0x218B36839E0 + 0x9908 + 4*0
            = 0x218B368D2E8
```

`0x140328710` ищет signed int16 index через `0x140A99944(definition+0xF8,name)`.
Если index == -1, возвращает false без записи. Тот же caller затем вызывает
`0x1403286A0(G+0x2560, name, value)`, который перебирает дополнительные graphs
по записям stride 64. Их успешное обновление для crouch не утверждается по
одной проверке основного graph.

Порядок в `0x1401D2DB8`: `0x140328CB4 → 0x140328D0C` выполняется перед
позднейшим graph evaluation `0x140328934` в том же update function.
Рабочий scalar slot не равен уже смешанной pose и не является world-transform.

## От model-bone к преобразованию skin matrices

После завершённого FK `0x1404B6AA8` для D поставлен watchpoint на quaternion
bone **14**, адрес `bones+14*48+16=0x218B7014250`.
Следующее пойманное чтение:

```asm
0x140216B05 movups xmm9, [rdx+10h]
```

`RDX=bones+14*48`, count model bones 619. Это **`0x140216990`**, которая:

- берёт массив model transforms с шагом 48;
- читает mapping records с шагом 12;
- проверяет mask `record+4`;
- source bone index = `packed & 0x7FF`;
- reference-matrix index = `(packed >> 11) & 0x7FF` (матрицы по 64 bytes);
- output index = `packed >> 22`;
- строит affine matrix из model quaternion/scale/translation;
- обрабатывает матрицы пачками по 4 через `0x140216DC4`;
- пишет по **48 bytes на output slot** (три строки по 4 float).

Отдельный следующий live stop `0x140215848`, условие span.begin==bones:

- `RBX=M=0x218A9FE89B0`, vtable **`0x142AC7A18`**;
- `M+0x50=E`, то есть это компонент текущего player entity;
- RTTI getter `0x1418EEF50` возвращает зарегистрированный
  **`entSkinnedMeshComponent`**, global `0x14342DA10`;
- return **`0x140215AA3`**, caller `0x140215940(M, ...)`;
- input pose span `[0x218B7013FA0,0x218B701B3B0)` = **619×48 bytes**;
- mapping descriptor `0x21499A31808`, 26 mappings и 26 reference matrices.

Таким образом, установлена реальная связь player model pose → skinned mesh
компонент M. Bone14-read и M-wrapper stops — отдельные наблюдения; они не
выдаются за один frame или один и тот же mesh mapping.

### Установленный статический этап выдачи

В `0x140215940` при `[M+0x1B0]!=0`:

1. Получение output span через interface `[[M+0x1B0].vtable+0x28]`;
2. `0x140215A9E → 0x140215848 → 0x140216990`;
3. копирование вычисленных bounds в M+0x160/+0x170;
4. вызов interface **`+0x20`** после записи матриц.

Альтернативная ветвь использует уже подготовленный span M+0x1C0/+0x1C8.
Allocation и перенос в mapped buffer дополнительно подтверждены ниже.
Окончательный GPU draw/dispatch binding ещё не установлен.

## Проверка одной матрицы в одном останове

Stop **`0x140215AA3`** после возврата matrix builder, фильтр `RBX=M`:

- mapping record 0: `02000000FF00000043018A3E`;
- source bone **2**, reference matrix **0**, destination slot **0**;
- output span `0x228ACF93660`, count **26**;
- model pose span подтверждён через `context+0x38`;
- сняты bone2 TRS (48 bytes), reference0 (64 bytes) и output0 (48 bytes).

Независимый расчёт проверил:

```text
output3x4 = первые три строки [modelTRS(bone 2) × referenceMatrix(0)]
```

Reference input хранится как четыре column vectors; output — три строки
по четыре float. Максимальное отклонение компонента: **5.51×10⁻⁸**.
Проверка не нормализует исходный quaternion за движок и не использует его
функции вычисления. Обозначение referenceMatrix описывает доказанную роль
в произведении; её создание как inverse bind отдельно ещё не прослежено.

## SkinningManager allocation → mapped D3D12 buffer

Текущий interface **`I=[M+0x1B0]=0x214A0B7EB70`**, vtable `0x142ADAAB0`.

| Slot I.vtable | Разрешённая функция |
|---|---|
| `+0x28` | `0x140215DD8`, получает output span |
| `+0x20` | **`0x14014A700`, пустая функция** |

Таким образом, вызов `+0x20` после сборки палитры **не upload** для этого
конкретного interface. Имена от folded/переиспользованного кода (`AK::...`)
не дают функции дополнительного смысла.

### Выделение CPU staging

`0x140215DD8` запрашивает `48*matrixCount` через `0x140215E5C`:

- manager `U=[[qword_143427C00]+0xD8]=0x218A51F90C0`;
- handle объекта I+0x10 — **`0x00010039`**;
- generation/bitmap проверяется `0x140215F34`;
- `0x140216580` выделяет выровненный на 16 bytes блок;
- возвращается `[U+0xE6040] + byteOffset`;
- сохраняется запись **stride 12** `{handle, byteOffset, byteCount}`.

CPU staging base в этом запуске: **`0x228ACE90000`**. Он отдельно выделяется
в `0x140612950 → 0x140612B38`; это не mapped address из renderer record.

### Передача 26 матриц

Первый data-watch после сборки палитры поймал чтение внутри `memmove`,
return **`0x140A7DD7B`**, caller **`0x140A7DD14`**.

Следующий отдельный останов **после возврата memmove**, отфильтрованный по
handle `0x00010039` и размеру `0x4E0`, дал:

| Поле | Значение |
|---|---|
| transfer record | `0x218A52D32D0` |
| raw record | `39000100809A0A00E0040000` |
| offset | `0xA9A80` |
| bytes | `0x4E0 = 1248 = 26×48` |
| source | `0x228ACF39A80` |
| destination | `0x239DCFD9AB0` |

Оба блока полностью выгружены **в этом одном останове** и побайтово равны.
SHA-256 каждого:
`bb20c4b1efc0602ddbe33fb4f12ce6e26a71ca7ef20a441aac98b53ddac8a8f1`.

Формула, проверенная и по инструкциям, и по числам:

```text
source = stagingBase + record.byteOffset
destination = mappedResourceBase + 48 + record.byteOffset
```

Первые 48 bytes mapped resource заняты identity 3×4 matrix; initializer
**`0x140612950`** явно записывает её при создании. Это объясняет дополнительное
`+48`, а не неизвестное смещение внутри каждой кости.

`0x140A7DD14` выполняется из двух job callbacks `0x140A7DBCC/0x140A7DC6C`.
Они делят массив записей на две половины. Registrars `0x140BD1934/0x140BD1A30`
дают обеим имя **`SkinningManager_UploadNewData`**; scheduling находится в
`0x14020A958`. Имя не заменяет проверку данных — exact-copy выше выполнена.

### D3D12 resource и Map

`U+0xE6108` содержит renderer buffer ID **32731** (`0x7FDB`), не индекс кадра.
Renderer global `R=[qword_143438A28]=0x228A0910000`.

```text
bufferRecord = R + 0x5C0AF8 + (bufferID-1)*0xB0
             = 0x228A144F0D8
```

Из текущего record прочитано:

| Record field | Значение |
|---|---|
| `+0x00` | buffer byteSize `0x03C00000` (60 MiB) |
| `+0x06`, high nibble | **5**, ветка persistently mapped resource |
| `+0x08` | GPU virtual address `0x2EFE00000` |
| `+0x10` | COM resource `0x239D8970D00` |
| `+0x18` | allocation wrapper `0x218A45342D0` |
| `+0x40` | mapped CPU address **`0x239DCF30000`** |

COM vtable `0x7FFA704E2E58` принадлежит **`d3d12core.dll`**.
Статический **`0x140220110`** для high nibble 5 создаёт ресурс и на
**`0x14022040C`** вызывает slot **`+0x40`** с subresource=0,
пустым read range и ppData=`bufferRecord+0x40`.
Это ABI **`ID3D12Resource::Map`**. На `0x140220522` slot `+0x58`
(`GetGPUVirtualAddress`) заполняет record+8.

ABI сверено по локальному Windows SDK
`10.0.26100.0/um/d3d12.h:5001–5077`.
Создание происходило до attach: сам вызов Map не воспроизводился в live,
но верифицированы его статический путь, текущая ветка, поля и D3D12 object.

**Граница подтверждения:** законченная CPU palette → побайтовая копия в
mapped memory конкретного D3D12 resource. Какой draw/dispatch и какой descriptor
впоследствии потребляют этот диапазон, здесь ещё не подтверждено.

## Артефакты

В `render_camera_RE/analysis/`:

- `native_vroff_pending_scalar_consumer_1800.md`
- `native_vroff_graph_scalar_targets_1800.md`
- `native_vroff_input_storage_offsets_1800.json`
- `native_vroff_model_bone_matrix_reader_1800.md`
- `native_vroff_skin_matrix_build_1800.md`
- `native_vroff_mesh_skin_upload_1800.md`
- `native_vroff_skin_binding_dispatch_1800.md`
- `native_vroff_skin_binding_vtable_1800.md`
- `native_vroff_skin_buffer_interface_1800.md`
- `native_vroff_skin_buffer_allocation_1800.md`
- `native_vroff_skin_buffer_manager_1800.md`
- `native_vroff_skin_linear_allocator_1800.md`
- `native_vroff_skin_staging_copy_1800.md`
- `native_vroff_skin_transfer_jobs_1800.md`
- `native_vroff_skin_transfer_schedule_1800.md`
- `native_vroff_skin_upload_frame_1800.md`
- `native_vroff_skin_buffer_creation_1800.md`
- `native_vroff_gpu_buffer_create_1800.md`
- `skin_mesh_1800_before_transfer.bin`, `skin_mesh_1800_after_transfer.bin`

Указанные выводы основаны на инструкциях и конкретных runtime pointers;
декомпиляторный прототип с потерянными vector-register аргументами не принят
за корректный C ABI.
