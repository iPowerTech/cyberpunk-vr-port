# Clip pair → state transition → Locomotion slot output

2026-09-15, PID **17268**, imagebase **`0x7FF7E34A0000`**.
Продолжение [проверки Sample](cp2077-native-player-graphs.md).

Два связанных **внутри себя** trace:

- [clip-pair blend](cp2077-native-locomotion-blend-17268.json),
  input `ad723c69ef9144c5aa00e9cef8fb089b`;
- [state transition → slot return](cp2077-native-locomotion-slot-17268.json),
  input `92cc8402e6eb42208c30a22b2ed93a66`.

Это разные W episodes; их веса не складываются в один frame.

## 1. Какая ветвь действительно участвует в позе

Root pose состояния `idle_to_jog` — **Blend2**, serialized handle517,
runtime node **`0x24C33CC3930`**, vtable **`0x142BB0EB0`**.
Его inputs:

```text
first:  BlendMultiple handle518, runtime 0x24C204AB0F0
second: BlendMultiple handle525, runtime 0x24C204AAD90
```

При входе в **`0x140324AC4`** в первом trace:

- child graph state=`0x24C21D302C0`;
- state memory=`0x2502F643D60`;
- parent weight в state+**0x6ED0** = **0**;
- native MoveXY=(0,1), speed >0.02.

Инструкции на `0x140324AF3..0x140324B1F` при таком weight вызывают
**только first input**, передавая тот же output wrapper. Поэтому наличие
правой ветви в ресурсах или её Update в другом вызове не делает её
участником именно этой позы.

## 2. BlendMultiple: pair и точный вес

В `0x140322CC4` для первого BlendMultiple:

| Значение | Node содержит offset | Runtime state offset | Измерено |
|---|---|---|---|
| weight | node+0xB0 | +0x6EDC | **0.9841498732566833** |
| index A | node+0xC0 | +0x6EE0 | **1** |
| index B | node+0xD0 | +0x6EE4 | **0** |

Input links имеют stride24 и actual pointer в +0x10. Снятые links +
сопоставленный resource nodesToInit устанавливают:

- A: handle520, **`fpp_idle_to_jog_270`**;
- B: handle519, **`fpp_idle_to_jog_0`**.

`0x140322D92` sample A в исходный output;
`0x140322DE9` sample B во временный output;
`0x140322DFF` вызывает **`0x1402C62FC(out, A, B, w)`**.
`XMM3` загружен из ранее сохранённого weight через `movaps xmm3,xmm6`.

До и после blend сняты целиком 153×48 bytes transforms и 52×4 bytes tracks.
Возврат фильтровался по **node+RSP**, prepared до resume.

### Независимая численная проверка

```text
translation/scale = float32(float32((1-w)*A) + float32(w*B))
```

- Все компоненты translation/scale обычных 152 transform records совпали
  **побитово** с этим float32 расчётом.
- Первые 23 обычных float tracks — **побитовое совпадение**.
- Quaternion blend: sign correction по dot, затем normalized lerp при
  dot>0.99, иначе slerp (`0x1401D8378`). Максимальная component error
  независимого расчёта — **1.17e-7**.
- Tail transform/tracks обрабатываются масками отдельно; к ним обычный
  unconditional lerp не приписывается.

На возврате в parent Blend2 **`0x140324B24`** полный transform buffer совпал
с результатом BlendMultiple **побитово**. Таким образом, для этого вызова
доказан не только Sample клипов, но и их относительный вклад в state output:
примерно **1.585% A + 98.415% B**, затем parent pass-through.

## 3. От state output до root Locomotion slot

Во втором W trace подготовлен stop перед **`0x1402C9B7A`**:

```text
GraphSlot_Test::Sample 0x1402C9A90
  -> 0x140B29FA0(attachment,...)
     -> 0x1402C9F80(attachedGraph,...)
        -> state-machine transition sampling
```

Slot=`0x24C36796FB0`, parent state=`0x24C2A274100`,
attachment=`0x2502F8A5020`. В том же вызове пойманы:

1. Выход state Blend2 `0x140324B24`. Снят полный state transform buffer
   **B** (`0x25026B8C830`).
2. Возврат из него в transition sampler **`0x140177A28`**.
   RSP=`0x7838FDA0D0`, interpolator=`0x24C29EA9708`.
3. Stack arg `[RSP+0x848]` = **0x3DD63992**, то есть
   **alpha=0.10460199415683746**. Interpolation mode `[interpolator+0x30]`=0.
4. До blend снят полный исходный output **A** (`0x25026B8A8D0`).
5. После `0x140177A49 → 0x1402C62FC`, stop **`0x140177A4E`**,
   тот же interpolator+RSP; сохранён transition result.
6. После выхода attached graph, stop **`0x1402C9B7F`**,
   тот же slot+RSP=`0x7838FDBE10`; сохранён root slot output.

### Результат

- Transition translation/scale точно совпали с `(1-alpha)*A+alpha*B`
  с округлением float32 по инструкциям.
- Максимальная quaternion component error — **1.49e-7**.
- **Все 153 transform records transition result и slot output идентичны.**

Эта проверка замыкает переход от позы состояния `idle_to_jog` до выхода
Locomotion slot для одного конкретного вызова. Это ещё не final main-graph
pose: последующие узлы, FK, добавление/маппинг костей и camera binding находятся
после возвращённого slot output.

## 4. Что теперь осталось

Последующий [main/FK trace](cp2077-native-main-fk.md) связал выход slot,
main final local pose и полный FK в одном внешнем вызове. Он также уточнил
mapping и поздние записи FPP-deformations/shadow graph.
Осталась точная attribution writers между slot и main output. Отдельно
требуется доказать передачу extracted root-motion delta в движение:
clip flag `applyMotion` не заменяет эту проверку.

## Артефакты

Под `render_camera_RE/analysis/`:

- `loco_blend_weight_sources_17268.json`, `loco_blended_clip_identities_17268.json`;
- `loco_blend_weight_dispatch_17268.md`, `loco_pose_blend_math_17268.md`;
- `loco_transform_track_interpolation_17268.md`, `loco_qstransform_lerp_17268.md`;
- `loco_root_graph_and_quat_blend_17268.md`;
- `loco_blend_17268_{a,b,out}_transforms.bin`,
  `loco_blend_17268_{a,b,out}_tracks.bin`, `loco_blend_17268_parent_transforms.bin`;
- `loco_slot_chain_17268_state_transforms.bin`,
  `loco_slot_chain_17268_transition_a_transforms.bin`,
  `loco_slot_chain_17268_transition_out_transforms.bin`,
  `loco_slot_chain_17268_slot_out_transforms.bin`.

`verify_native_movement_evidence.py` проверяет оба trace, offsets/links,
same-call guards и математику. На выходе live breakpoints удалены,
игра resumed, release-all выполнен с возвратом исходного HWND.
