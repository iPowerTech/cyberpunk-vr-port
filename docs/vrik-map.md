# Полная карта VRIK — и что поворачивает тело за мышкой

Составлено 2026-08-13 по коду (`src/Hooks/AnimPose.cpp`, `src/Anim/CharacterRig.cpp`,
`src/Anim/HandSnapshot.cpp`, `src/Natives/CameraPair.cpp`) плюс уже сделанный реверс движка
(`docs/cp2077-camera-write-chain.md`). Всё, что здесь названо адресом или слотом, проверено
в коде на этот момент, а не по памяти.

---

## 0. Где VRIK стоит

| что | значение |
|---|---|
| точка хука | `Cyberpunk2077.exe + 0x17DDB4` — pose-apply анимации, MinHook |
| установка | НЕ на бут: натив `InstallVRAnimPoseHook` из скрипта (`InstallAnimPoseHook()`) |
| детур | `Hooked_AnimPoseApply(a1, a2, a3, a4)` — сначала `OriginalAnimPose`, потом наши записи |
| буфер костей | `poseDesc = ((void**)a2)[7]`; `boneBuf = poseDesc[0]`; `trackBuf = poseDesc[3]` |
| формат кости | 48 байт: `VRIK_TRANS_OFF` (3 float) + `VRIK_ROT_OFF` (4 float), **локально к родителю** |
| идентификация | `trackBuf == g_PlayerTrackBufA/B` — игрок; `g_WeaponTrackBufA/B` — оружие |
| ранний выход | нет `g_PlayerTrackBuf*`, или `g_VRBind<=0` и все прочие режимы выключены |
| защита | один `__try` на весь детур; `VirtualQuery` на этом пути запрещён (проседание до 4 fps) |

Хук вызывается для **каждого** скелета в сцене (все NPC), поэтому два сравнения указателей —
это и есть штатное состояние.

---

## 1. Порядок внутри одного pass

Движок применяет позу игрока **4–5 раз за тик** (батч), поэтому шаги делятся на «каждый pass»
и «один раз на батч».

```
1. OriginalAnimPose(...)                       движок положил свою позу
2. WeaponRigIdentifyAndWrite / census          чужие скелеты (по именам костей)
3. WeaponRigCaptureParts                       скелет оружия
--- дальше только игрок ---
4. RefreshHandsSnapshot()                      seqlock [127] -> g_handsStable[0..126]
5. VrikReloadFingerPose / VrikSmokingCigPose / VrikSmokingLighterPose      КАЖДЫЙ pass
6. fppCamera bone freeze (5 костей g_VRFppCamIdx, режимы 0..4)             КАЖДЫЙ pass
7. батч-часы: gap > CyberpunkVR_VrikBatchGapMs  =>  ++s_batchSeq
   tickNow = CyberpunkVR_VrikBatchClock ? s_batchSeq : g_VrikFrameEpoch
8. tickNow == g_solveCacheTick ?
      да  -> REPLAY: восстановить g_solveCacheVal в boneBuf (bit-exact)
      нет -> FRESH SOLVE (раздел 2), затем заполнить кэш
```

Почему часы — батч, а не Present: при 79.5 fps `freshSolve` был 77.0, при 67.5 — 63.0, то есть
3–7 % кадров получали replay вместо нового солва. Причина была фазовая, не частотная.

Замер «насколько движок увёл кость до нашей записи» живёт на пути replay (`g_VrikEngineOverwriteMm`,
гистограмма по номеру pass'а в батче) и включается `CyberpunkVR_XrDeepDiag`.

---

## 2. Fresh solve, по шагам

```
VRIK_LatchViewPacket()          -> g_viewPkt[0..16]  (ViewFrame in-process seqlock;
                                   fallback: shared [104..111],[141],[227..230] под [143])
g_solveCacheYaw = g_viewPkt[8]                        героя frame-of-reference: heading
[bodyyaw] census                                      см. раздел 6
vrikInVehicle = SharedPose(31) > 0.5                  в машине — ТОЛЬКО руки

VRIK_ComputeFK(boneBuf, VRIK_FKCount())               локальные -> модельные (parent < child)
не в машине:
  VRIK_DampenTorsoWeaponPose    спайны -> identity; HIPS local rot ПИНится к среднему за 90 солвов
  VRIK_PinGirdleTranslations    8 костей (ключица/плечо/предплечье/кисть x2): локальные
                                ТРАНСЛЯЦИИ пинятся к среднему за 90 солвов
  VRIK_ComputeFK

VRIK_ScaleArmBonesFromRest x2 + VRIK_ComputeFK         длины сегментов из rest, масштаб к
                                                       измеренной руке пользователя

headModelPos = g_fkPos[g_VRHeadBoneIdx]                БЕЗ фильтра (медиана-3 удалена вместе с AER)
hmdRel       = SharedPose(16..19)                      ориентация HMD, к которой сделаны
hmdPosBase   = SharedPose(124..126)                    контроллеры -- ОДНА публикация с ними

VRIK_ComputeCamModel:
    snap = VRIK_ReadTransformSnapshot()                 camQ + entQ + (cam-entity) из ОДНОГО
                                                        CET-пуша, под одним seqlock
    outPos = rotate(conj(snap.entQ), snap.camMinusEntity)
    xrHead = AcquireFrameHeadSample()                   тот же latch, который позже читает PatchCamera
    worldCamQ = engineBodyYaw * gamePitch * map(xrHead)
    outRot = conj(engineBodyYaw) * worldCamQ
    fallback outRot = conj(snap.entQ) * snap.camQ       машина / нет свежего engine yaw
vrikCamBake = OpenXRManager::GetCameraOffset()          camModelPos += bake  (не в машине)
handAnchor  = camModelPos + xrHeadOffset* + g_vrikEyeBake

VRIK_BodyAxesFromCamYaw(camModelRot, ...)              ОСИ IK, не поворот тела:
    camYaw = 2*atan2(qz,qw)  (twist вокруг модельного +Z, без flip при взгляде вниз)
    follow = clamp((|camYaw| - dead) * gain, 0, cap), фейд последних 40 град у ±180
    bodyFwd = Rz(bodyYaw) * (+Y), bodyRight = bodyFwd x bodyUp

ТЕЛО (если g_VRBodyUnderHMD и не в машине):
    bodyCamModelPos = camModelPos - rotate(conj(entQ), g_headDeltaFP/131072)
    VRIK_PlaceBodyUnderHMD(...)                        раздел 3
    публикация camera-mount offset -> [85..88]

headRef = camModelPos + (head-cam) в осях тела, усреднённое за первые 90 кадров и далее КОНСТАНТА

на каждую руку:
    anchorStableShoulder: сброс weapon-stance трансляций (по самой ШИРОКОЙ виденной стойке),
        затем ключица ВРАЩАЕТСЯ к желаемой точке (кап 75 град), позиция не пишется никогда
    VRIK_BuildHandTarget(...)                          предварительный target
    ГЛАВНЫЙ путь (гизмо), если VRIK_ResolveViewPos дал позу вида:
        vq   = SharedPose(104..107)                    ориентация вида
        vyaw = g_viewPkt[8]                            heading из ТОГО ЖЕ пакета
        vqUse = vq * map(conj(headOri_пакета) * hmdRel)   time-align на сэмпл рук
        rvM  = Rz(-vyaw) * vqUse ;  vpM = Rz(-vyaw) * (viewWorld - entityPos)
        target  = vpM + rvM * map(vrPos * scale) + off
        handRot = rvM * map(vrQuat) * wristCorr
    иначе fallback: hmdRel/hmdPosBase + camModelRot вокруг handAnchor
    VRIK_ApplyHandStop(...)                            зона стопа кисти (коллизия рук)
    VRIK_SolveArm(...)                                 2-bone IK, раздел 4

публикации после солва: g_VRPalmModel*/Rot*, g_VRCamModel*, g_VRViewWorld*,
    g_VRSmokeMouthWorld*, [20..22] дистанции холстеров (от РЕШЁННОГО кистевого таргета)

кэш для replay (до 96 костей): hips и ВСЕ его предки до 0, спайны, шея, голова,
    ноги (up/leg/foot x2), ключицы, плечи/предплечья/кисти, скручивания предплечья x3 x2
g_solveCacheTick = tickNow
```

Три правила, которые здесь оплачены поломками и не должны нарушаться:

1. **`hmdRel`/`hmdPosBase` берутся из той же публикации, что контроллеры.** Свежий сэмпл головы
   рядом со старыми контроллерами оставляет остаток = движение головы между двумя моментами, и он
   уезжает прямо в цели рук и в тело под ними.
2. **View-пакет берётся ОДНОЙ структурой.** Пофайловая замена чтений на «живые» объекты смешивает
   инстанты внутри одного кадра отсчёта — дважды сломало anchor рук.
3. **Нельзя собирать относительную позу из разных фаз кадра.** В частности,
   `g_lastLocate*(N-1)` нельзя вычитать из `CyberpunkVR_PlayerEntity*(N)`: `LocateCamera` идёт после
   animation solve. Остаток равен целому кадру перемещения игрока и даёт видимое тело при ходе назад;
   старый кватернион камеры одновременно создаёт отставание головы на повороте.

---

## 3. `VRIK_PlaceBodyUnderHMD` — что именно пишется

```
1. захват модельных позиций стоп (footR, footL)
2. squat: hy = SharedPose(90) (пивот шеи) или [89] (сырая высота HMD)
   drop = -hy - g_VRSquatThreshold, кап 0.7 м, deadzone 2 см + EMA 0.25 -> s_vrSharedSquatDrop
   headAnchor = (camModelPos.xy, camModelPos.z + g_VRHeadDrop - squatDrop)
3. стопы РЕЦЕНТРИРУЮТСЯ под headAnchor.xy (сохраняя расстояние между ними)
4. hips: torsoVert = median3(head.z - hips.z), кап >= 0.2
   hipsTarget = (headAnchor.xy, headAnchor.z - torsoVert)   -> VRIK_WriteLocalPos (ПОЗИЦИЯ)
5. CCD спайна: chain = спайны + шея, 3 прохода, каждая кость на 0.5 остатка к headAnchor
6. VRIK_SolveLeg на обе ноги к захваченным стопам (колени вперёд по bodyFwd)
7. head bone: VRIK_WriteLocalRot(..., camModelRot)        ОРИЕНТАЦИЯ головы = камера
8. публикация костей тела для мяча: g_VRBodyBone[0..10]
9. eye-bake: d = (headFK + (-0.02, +0.10, +0.15)) - camModelPos, EMA 0.1, кламп ±0.9
   -> g_vrikEyeBake + зеркало в [116..119] для CET
```

**Ротация hips не пишется нигде.** Пишутся: позиция hips, локальные ротации спайнов/шеи (CCD),
ноги, ротация головы. Это ключ к разделу 6.

---

## 4. `VRIK_SolveArm` — форма алгоритма

- `xDir` = единичный вектор **кисть → плечо** (анкор на реальной позиции руки пользователя);
- `yDir` ⟂ `xDir` в плоскости, где должен лежать локоть: по умолчанию проекция `-bodyUp` (локоть
  вниз), при почти вертикальной руке — `-bodyFwd`; плюс cross-body коррекция (кисть за средней
  линией — локоть наружу) и фейд этой коррекции при почти прямой руке;
- угол в кисти — теорема косинусов, отсюда точка локтя;
- недосягаемость (`hsLen > upLen + foreLen`) — **растяжение** сегментов пропорционально, а не
  кламп дистанции: клампом рука навсегда остаётся полусогнутой;
- запись — только ротации (`VRIK_WriteLocalRot`), кисть ставится `VRIK_WriteHand`.

---

## 5. Данные: кто пишет, кто читает

| значение | пишет | канал | читает | каденс |
|---|---|---|---|---|
| контроллеры + `hmdRel` + `hmdPosBase` | `FlushHandsToShared` (Present) | shared `[0..19]`, `[124..126]`, seqlock `[127]` | solve через `SharedPose()` | Present |
| view frame (quat, delta, heading, headOri) | `LocateCamera` | `cvr::camera::ViewFrame` (in-process seqlock) + зеркало shared `[104..111]`,`[141]`,`[227..230]` | `VRIK_LatchViewPacket` | locate |
| `g_headDeltaFP`, рецепт анкора | `LocateCamera` | глобалы плагина | `PatchCamera`, тело в solve | locate |
| coherent entity/cam snapshot, пара `(cam-entity)` | CET → натив `SetVRPlayerYaw` | `VrikTransformSnapshot` под seqlock; ABI-зеркала `g_VREntity*`, `g_VRCam*`, `g_VRCamPairLocal*` | snapshot: `VRIK_ComputeCamModel`; ABI-зеркала: `VRIK_ResolveViewPos` | Lua tick |
| текущая ориентация головы для head bone | OpenXR frame latch | `AcquireFrameHeadSample()` + engine body yaw/pitch | `VRIK_ComputeCamModel` → только `VRIK_PlaceBodyUnderHMD`; тот же sample позже в `PatchCamera` | aim epoch |
| packet-compatible ориентация рук | coherent entity/cam snapshot | `outPairedRot = conj(entityQ) * camQ` | arm axes, fallback кистей, `g_VRCamModelRot`; A/B: `CyberpunkVR_VrikSplitHeadHandRot` | Lua tick |
| camera bake, eye bake | `OpenXRManager`, `PlaceBodyUnderHMD` | `GetCameraOffset()`, `g_vrikEyeBake` | solve, `LocateCamera` | по мере |
| решённые ладони, поза вида, кости тела | solve | `g_VRPalmModel*`, `g_VRViewWorld*`, `g_VRBodyBone*` | мяч, дым, скрипты | solve |
| дистанции холстеров | solve | shared `[20..22]` | Lua | solve |

Пара `(cam − entity)` в `SetVRPlayerYaw` проходит через **slew-лимитер** ~0.5 м/с по XY (Z сырой),
защиту от телепорта (>0.35 м — снап) и публикуется с опережением `g_VRPairLeadTicks` на
EMA-скорости пары. Это единственный фильтр этой величины в проекте — и вид, и скелет читают ОДИН
результат, поэтому разъехаться они не могут.

---

### 5.1 Нативная кадровая пара (2026-08-21)

Основной источник `camModelPos` больше не является Lua-публикацией. `LocateCamera` атомарно
публикует `LocatedCameraFrame` вместе с `g_VrikFrameEpoch`; следующий `BodyYawFollowTick(N)`
соединяет `camera(N-1)` с сохранённым `entity(N-1)` только при совпадении epoch. Результат
публикуется как `VrikTransformSnapshot` без CET-частоты и без `SetVRPairSlew`. Lua-снимок остаётся
startup/failure fallback и live A/B; главный IK target обеих рук берёт позицию из того же
нативного `handAnchor`, а не реконструирует её повторно через `VRIK_ResolveViewPos`.

Контроль: `CyberpunkVR_VrikNativeFramePair`; счётчики
`CyberpunkVR_DebugVrikNativePairPublished`, `...Rejected`, `...PhaseMiss`.
Потребление контролируют `CyberpunkVR_DebugVrikNativePairUsed` и
`CyberpunkVR_DebugVrikLuaPairFallback`.

Отдельный незакрытый аудит: `RefreshHandsSnapshot` всё ещё копирует view/offset-слоты
`[91..93]`, `[104..111]`, `[120..123]` под hands-seqlock `[127]`, хотя их писатели этим
seqlock не пользуются. Нативный `ViewFrame` уже существует; перенос его потребителей должен идти
отдельным diff и отдельным игровым замером.

## 6. Что поворачивает тело за мышкой

**В VRIK — ничто.** Ни одной записи yaw в hips или спайны нет; наоборот,
`VRIK_DampenTorsoWeaponPose` **пинит** локальную ротацию hips к захваченному эталону и обнуляет
локальные ротации спайнов. `camYaw` используется только для построения осей IK
(`VRIK_BodyAxesFromCamYaw`), то есть влияет на плоскость сгиба локтя и колена, а не на поворот
модели.

**Значит поворот приходит трансформом сущности,** и это замыкается с реверсом камеры:

```
мышь -> (ввод; директор своё поле +0x720 в рантайме не читает)
     -> yaw РОДИТЕЛЯ камеры (сущность игрока)
        камера своего yaw не имеет: её локальный кватернион не пишется НИ РАЗУ,
        слот-провайдер sub_1401D92A0 отдаёт ровно parent+0xE0/+0xF0
     -> sub_1401D9528 (цикл привязок) -> sub_1401D74FC SetWorldTransform
     -> sub_1401D8558 UpdateWorldTransforms: world = slotQuat * localQuat   <- НАШ ХУК
     -> тело рисуется этим же трансформом сущности
```

То есть тело и камера поворачиваются **одним** значением, а двойник на резком флике — это
**фаза**, а не второй поворот: поза и скиннинг считаются в анимационном батче, а камера того же
такта появляется позже (измерено ранее: `solve − patch = +1` в 299 из 300 замеров).

### Замер, который это оцифровывает

Добавлен в этой сборке, печатается раз в две секунды рядом с `[vrik]`:

```
[bodyyaw] view-vs-entity peak 4.90 deg | entity step 5.10 deg/solve | hips model step 0.03 deg
```

- `lag` (`view-vs-entity peak`) — угол между heading вида и yaw сущности, то есть **размер
  двойника** в градусах;
- `step` — изменение yaw сущности между двумя fresh-солвами, скорость поворота, с которой lag
  надо сравнивать: `lag ≈ step` означает ровно один кадр задержки, `lag >> step` — другой
  механизм;
- `hips model step` — изменение yaw hips в МОДЕЛЬНОМ пространстве. ~0 при растущем lag означает,
  что ни одна кость поворот не несёт и он целиком в трансформе сущности; заметное значение
  означает, что граф анимации доворачивает торс ВНУТРИ модели и два источника могут расходиться.

Оговорка записана и в коде: кватернион сущности приходит из CET-пуша, поэтому его возраст — это
каденс пуша, а не движка. Он ограничивает lag **сверху**, что и есть полезное направление.

### НАЙДЕНО ЖИВЬЁМ (2026-08-13, x64dbg, поворот мышью): `sub_140336390`

Как искали: позиция сущности из общего блока (`[96..98]`) переведена в фиксированную точку и найдена
поиском по памяти — так объект находится без угадывания, какой из сотен компонентов сейчас в цикле.

Что нашли на этом пути:

```
три объекта держат ПОЗИЦИЮ СУЩНОСТИ + ЧИСТЫЙ yaw-кватернион (0, 0, 0.8930, 0.4501) = 2.2078 рад,
что совпадает с опубликованным heading до пятого знака. Локальные трансформы у них ТОЖДЕСТВЕННЫ
(pos 0,0,0 / quat 0,0,0,1) -- значит это распространение, а не источник.
```

| уровень | кто пишет `+0xF0` | как выяснено |
|---|---|---|
| нижние компоненты | `sub_1401D74FC` (SetWorldTransform), `movups [rbx+0xF0], xmm0` @`0x1D7593` | hw-watchpoint; возвраты на стеке `0x1D979A` (сразу после `call sub_1401D74FC` в `sub_1401D9528`) и `0x1CA385A` (в `sub_141CA3720`) |
| верхний компонент | `sub_1401D8558` UpdateWorldTransforms — **через наш собственный стаб PatchCamera** (стаб инкрементит счётчик, сохраняет `rdx`/`rsi`/`xmm0` и зовёт колбэк в нашей DLL) | тот же watchpoint; значит этот объект проходит через наш хук, и мы отсеиваем его по `camKind` |
| кто просит пересчёт | `sub_14068E1F8+0xB4` (сразу после `call [vt+0x240]` — пересчёт AABB) ← `sub_1401C9430+0x83` ← `sub_140B53E3C` (цикл по диапазону «грязных») ← `sub_140A95FB4` | возвраты на стеке |
| **источник yaw** | **`sub_140336390`**, инструкция `movups [r15+0x1D0], xmm0` @`0x1403367CD`; `xmm0` приходит из `sub_1401D9100` (нормализация кватерниона) | hw-watchpoint на промежуточном буфере трансформа |

`sub_1401C9430(rcx = массив хэндлов, edx = индекс)` — это флеш «грязных» трансформов: берёт
`[rcx] + idx*16`, поднимает refcount, проверяет `[obj+0x88] & 2` и зовёт `sub_14068E1F8`, который
пересчитывает AABB и мировой трансформ. То есть ниже `sub_140336390` — только распространение.

Про сам `sub_140336390` (0x883 байт, 492 инструкции): состояние объекта, куда он пишет, выглядит так —
`+0x1C0` позиция (фиксированная точка, тот же множитель `dword_1431EEE78`), `+0x1D0` кватернион
(наш yaw), `+0x1E0/0x1E4/0x1E8` три float'а, дальше `+0x1EC/0x1F0/0x1F4` и `+0x220..0x228`. Читает
`[rcx+0x22C]`. Вызывается из `sub_1403362F4` и `sub_1409CE470`, плюс лежит в таблице переходов,
которую использует `sub_1401A7248`.

**Вывод, к которому это сводится:** тело и камера поворачиваются ОДНИМ значением — тем, что
`sub_140336390` положил в `+0x1D0`. Камера наследует его через слот родителя, тело — через тот же
трансформ сущности. Значит двойник на флике это не второй поворот, а разница моментов: во сколько
поза запеклась против того, во сколько этот yaw был посчитан.

### Рецепт (если понадобится повторить)

Статический поиск по именам не даёт её: `m_mouseTurnRate`/`m_analogTurnRate` — RTTI-имена без
ссылок из кода, `moveMovementOrientationType` — это enum направления движения
(`NotSet/Forward/Backward/Left/Right`), к камере не относится. Поэтому — один заход в x64dbg,
без `run` между шагами (иначе объект переиспользуется и watchpoint ловит аллокатор — уже
проверено):

```
1. bp <base>+0x1D8783            вызов [vt+0xD8] внутри UpdateWorldTransforms
2. в этом же останове: rdx = РОДИТЕЛЬ (объект, чей yaw и есть поворот тела)
3. bp <base>+0x1D74FC            SetWorldTransform
   условие по указателю: rcx == <значение rdx из шага 2>
4. на срабатывании — stack_get_trace: вызывающий и есть тот, кто применяет ввод
5. снять бряки: bphwc И bpmc (hardware readwrite = memory bp)
```

---

## 7. Что уже исключено измерением (не повторять)

| версия | как закрыта |
|---|---|
| VRIK-solve виноват в двойнике | выключен — двойник есть |
| VRCAM / стерео | выключен — есть |
| DLSS / temporal | выключен пользователем; плюс у temporal нет своей камеры (§15 camera-write-chain) |
| двойная постановка трансформов | `perPresent 179` = 179 сущностей × 1 раз на кадр |
| движок перетирает наши кости | 0.0 мм в 0 из 450 проходов |
| HUD рисуется дважды | штатные два входа в ноду, документировано в коде |
| точка записи камеры | компонент и буфер SerializeSetup дали одинаковый трейл |
| heading отставал на кадр | исправлено (`HeadingFromPreWrite`) |
| head-дельта отставала на кадр | исправлено (`DeltaFromFreshSample`) |
| тело видно при ходе назад / голова дёргается при повороте | причина найдена: `g_lastLocate(N-1)` смешивался с entity(N); исправлено coherent `(cam-entity)` snapshot + общий с камерой `AcquireFrameHeadSample` |
