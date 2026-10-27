"""Bounded live VRIK observations using freshly resolved symbols and simulator poses.

Reads the target with VM_READ only. The pose channel restores the initial HMD
pose in finally. Asynchronous observations are labelled; this is not a substitute
for the separate before/after capture made at solver boundaries in x64dbg.
"""
import argparse
import base64
import ctypes
import json
import math
import os
from pathlib import Path
import struct
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "roomscale_tests"))
from live_read import Reader
from simulator_probe import read_status, send, await_pose


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--addresses", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--case", choices=("all", "idle", "translate_x", "translate_y", "translate_fast", "yaw", "pitch", "combined", "combined_fast"), default="all")
    parser.add_argument("--hold", type=float, default=.8)
    parser.add_argument("--idle-seconds", type=float, default=0, help="Extra stationary interval at the end (0..30seconds)")
    parser.add_argument("--pose-history", action="store_true", help="Record coherent completed camera-write history for readback analysis")
    args = parser.parse_args()
    if not .5 <= args.hold <= 2:
        raise ValueError("Hold must be0.5..2seconds")
    if not 0<=args.idle_seconds<=30:
        raise ValueError("Idle must be0..30seconds")
    if args.out.exists() or not args.out.parent.is_dir():
        raise ValueError("Fresh output under existing directory required")
    cfg = json.loads(args.addresses.read_text(encoding="utf-8-sig"))
    addresses = {k: int(v, 16) for k, v in cfg.items() if isinstance(v, str) and v.startswith("0x")}
    exe = Path("C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe")
    reader = Reader(cfg["pid"], exe, addresses["debug"], addresses["debug_seq"])
    reader.k.GetTickCount64.restype=ctypes.c_uint64
    reader.k.GetTickCount64.argtypes=[]
    directory = Path(os.environ["LOCALAPPDATA"]) / "OpenXR-Simulator"
    initial = read_status(directory)
    head = initial["head_tracking"]
    original = {**head["position"], **{k: head[k] for k in ("yaw", "pitch", "roll")}}
    current = dict(original)
    stop = threading.Event()
    phase = ["baseline"]
    rows, failures = [], []

    def raw(name, size):
        return reader.read(addresses[name], size)

    def unpack(name, fmt):
        return struct.unpack(fmt, raw(name, struct.calcsize(fmt)))

    n = unpack("bone_count", "<i")[0]
    if not 25 < n <= 800:
        raise ValueError("Unexpected live rig size")
    upper = list(raw("upper_owned", n))
    shadows = list(unpack("shadow_sources", "<" + "i" * n))
    roles = {k[5:-6]: unpack(k, "<i")[0] for k in addresses if k.startswith("bone_") and k.endswith("_index")}
    pairs = [(i, source) for i, source in enumerate(shadows) if 0 <= source < n]
    roles.update({"shadow_" + name: next((i for i, source in pairs if source == bone), -1)
                  for name, bone in list(roles.items()) if name in ("head", "right_hand", "left_hand", "neck", "neck1")})
    expected_cache = sum(bool(upper[i]) or shadows[i] >= 0 for i in range(n))
    result = {"pid": cfg["pid"], "addresses": cfg, "initial_status": initial, "original": original,
              "bone_count": n, "roles": roles, "shadow_pairs": pairs, "expected_cache": expected_cache,
              "samples": rows, "read_failures": failures,
              "boundary": "Running-process observations. Cache and source publications have separate stability checks; cross-source values are not asserted to be one invocation."}

    def packet(name, size):
        for _ in range(6):
            seq = unpack(name + "_seq", "<I")[0]
            if not seq or seq & 1:
                continue
            value = raw(name, size)
            if seq == unpack(name + "_seq", "<I")[0]:
                return seq, value
        raise RuntimeError(name + " packet in progress")

    def sample():
        before_ns = time.perf_counter_ns()
        epoch=unpack("epoch","<I")[0]
        tick = unpack("solve_cache_tick", "<I")[0]
        count = unpack("solve_cache_n", "<i")[0]
        if count != expected_cache or not raw("solve_cache_model", 1)[0]:
            return
        indices_raw = raw("solve_cache_indices", count * 4)
        values_raw = raw("solve_cache_values", count * 28)
        indices = struct.unpack("<" + "i" * count, indices_raw)
        if any(i < 0 or i >= n for i in indices):
            raise ValueError("Cache indices invalid")
        values = dict(zip(indices, struct.iter_unpack("<7f", values_raw)))
        cache_stable = (tick == unpack("solve_cache_tick", "<I")[0] and
                        count == unpack("solve_cache_n", "<i")[0] and
                        values_raw == raw("solve_cache_values", count * 28))
        row = {"phase": phase[0], "perf_ns": before_ns, "tick": tick, "cache_stable": cache_stable,
               "cache": {role: values.get(bone) for role, bone in roles.items()},
               "native_counters": unpack("hand_publish_counters", "<3Q"),
               "pose_counters": unpack("placed_pose_counters", "<3Q"),
               "solve_counters": [unpack("fresh_solves", "<i")[0], unpack("replay_solves", "<i")[0]],
               "entity_pos": unpack("entity_pos", "<3f"), "entity_q": unpack("entity_quat", "<4f"),
               "raw_targets": unpack("raw_hand_targets", "<6f")}
        row["source_counters"]=unpack("used_counters", "<2Q")
        if "identity_components" in addresses:
            row["identity_counters"]={key:unpack(key,"<2Q") for key in
                ("identity_components","identity_final","identity_final_miss","identity_image_writes",
                 "identity_captured","identity_capture_miss")}
            row["identity_counters"].update({key:unpack(key,"<Q")[0] for key in
                ("identity_copied","identity_copy_changed","identity_vrik_input_changed")})
            # Preserve full native IDs (bit63 set) through JSON/JavaScript tools.
            row["pose_ids"]={key:[hex(v) for v in unpack(key,"<2Q")] for key in
                ("identity_capture_ids","identity_submit_ids","identity_image_generations")}
            row["pose_ids"]["vrik_source"]=hex(unpack("identity_vrik_source","<Q")[0])
        row["submit_counters"]={name:unpack(name,"<Q")[0] for name in
            ("pose_readback","pose_exact","pose_estimated","vrcam_label","eye_reused","eye_paired","eye_unpaired") if name in addresses}
        if args.pose_history:
            history_head=unpack("placed_history_head","<Q")[0]
            history=raw("placed_history",64*88)
            if history_head==unpack("placed_history_head","<Q")[0]:
                row["pose_history"]={"head":history_head,"base64":base64.b64encode(history).decode("ascii")}
        row["cam_model"]=unpack("cam_model", "<3f")
        row["view_valid"]=bool(raw("view_packet_valid",1)[0])
        row["render_epoch"]=epoch
        row["now_ms"]=reader.k.GetTickCount64()
        if "cache_capture_ms" in addresses:
            row["cache_capture_ms"]=unpack("cache_capture_ms","<Q")[0]
        lua_seq,lua=packet("lua",68)
        row["lua_pair"]={"seq":lua_seq,"camera_q":struct.unpack_from("<4f",lua),
                          "entity_q":struct.unpack_from("<4f",lua,16),
                          "camera_span":struct.unpack_from("<3f",lua,32),
                          "valid":struct.unpack_from("<I",lua,44)[0]}
        row["lua_pair"]["age_ms"]=row["now_ms"]-struct.unpack_from("<Q",lua,48)[0]
        errors = []
        for dst, source in pairs:
            if dst not in values or source not in values:
                raise ValueError("Shadow/source missing in upper cache")
            a, b = values[dst], values[source]
            p = math.dist(a[:3], b[:3]) * 1000
            qa, qb = a[3:], b[3:]
            dot = sum(x * y for x, y in zip(qa, qb))
            norm = math.sqrt(sum(x * x for x in qa) * sum(x * x for x in qb))
            if norm < .1:
                raise ValueError("Invalid cached quaternion")
            angle = math.degrees(2 * math.acos(min(1, abs(dot) / norm)))
            errors.append((p, angle))
        row["shadow_max_mm"] = max(x[0] for x in errors)
        row["shadow_max_deg"] = max(x[1] for x in errors)
        native_size = 96 if cfg.get("native_owner_packets") else (68 if cfg.get("camera_status_packets") else 64)
        seq, native = packet("native",native_size)
        row["native_pair"] = {"seq": seq, "camera_q": struct.unpack_from("<4f", native),
                              "entity_q": struct.unpack_from("<4f", native, 16),
                              "camera_span": struct.unpack_from("<3f", native, 32),
                              "valid": struct.unpack_from("<I", native, 44)[0],
                              "body_span": struct.unpack_from("<3f", native, 52)}
        row["native_pair"]["consumer_epoch"]=struct.unpack_from("<I",native,48)[0]
        if len(native)>=68:row["native_pair"]["unavailable"]=struct.unpack_from("<I",native,64)[0]
        if cfg.get("native_owner_packets"):
            row["native_pair"].update(zip(("owner","origin","stamp_ms"),struct.unpack_from("<3Q",native,72)))
            row["native_pair"]["age_ms"]=row["now_ms"]-row["native_pair"]["stamp_ms"]
        if "native_phase_miss" in addresses:
            row["native_publication_counters"]={name:unpack(name,"<Q")[0] for name in ("native_phase_miss","native_published","native_rejected")}
            if "previous_entity_epoch" in addresses:row["entity_epoch"]=unpack("previous_entity_epoch","<I")[0]
            if "last_paired_camera_seq" in addresses:row["paired_camera_seq"]=unpack("last_paired_camera_seq","<I")[0]
            row["centre_counters"]=[unpack("body_centre_match","<Q")[0],unpack("body_centre_miss","<Q")[0]]
            lseq,located=packet("located",52)
            row["located"]={"seq":lseq,"frame":struct.unpack_from("<I",located,28)[0],
                            "epoch":struct.unpack_from("<I",located,32)[0],"centre_known":struct.unpack_from("<I",located,36)[0]}
        hs = raw("hands_stable", 512)
        hf = struct.unpack("<128f", hs)
        row["hands"] = {"sequence": unpack("hands_stable_seq", "<I")[0], "poses": hf[:20],
                        "head_position": hf[124:127], "bake": hf[91:94],
                        "anchor_metadata": hf[112:116],
                        "stable": hs == raw("hands_stable", 512)}
        row["view_packet"] = unpack("view_packet", "<17f")
        row["head_ref_q"] = unpack("head_ref_q", "<4f")
        row["roomscale"] = reader.sample()
        bone_pointer = unpack("bone_buffer_cell", "<Q")[0]
        local = reader.read(bone_pointer, n * 48)
        row["native_locals"] = {role: struct.unpack_from("<12f", local, bone * 48)
                                 for role, bone in roles.items() if bone >= 0 and role in ("hips", "left_leg", "right_leg", "left_foot", "right_foot")}
        row["bone_buffer_stable"] = (bone_pointer == unpack("bone_buffer_cell", "<Q")[0] and
                                        local == reader.read(bone_pointer, n * 48))
        row["read_ns"] = time.perf_counter_ns() - before_ns
        row["stable_render_epoch"]=epoch==unpack("epoch","<I")[0]
        rows.append(row)

    def collect():
        while not stop.is_set():
            try:
                sample()
            except Exception as exc:
                failures.append({"phase": phase[0], "error": str(exc)})
            stop.wait(.008)

    def hold(name, seconds=.8):
        phase[0] = name
        time.sleep(seconds)

    def sweep(goal, name, duration=1.2):
        nonlocal current
        phase[0] = name
        start_pose, start = dict(current), time.monotonic()
        while True:
            t = min(1, (time.monotonic() - start) / duration)
            u = t * t * (3 - 2 * t)
            current = {k: start_pose[k] + (goal[k] - start_pose[k]) * u for k in original}
            send(directory, current)
            if t == 1:
                break
            time.sleep(.01)
        await_pose(directory, current)

    thread = threading.Thread(target=collect, daemon=True)
    try:
        sample()
        thread.start()
        hold("baseline", 1.2)
        cases = [
            ("translate_x", {"x": .30}, 1.2),
            ("translate_y", {"y": .15}, 1.2),
            ("translate_fast", {"x": .80}, .5),
            ("yaw", {"yaw": math.radians(55)}, 1.2),
            ("pitch", {"pitch": math.radians(-40)}, 1.2),
            ("combined", {"x": .25, "yaw": math.radians(45), "pitch": math.radians(-25)}, 1.2),
            ("combined_fast", {"x": -.30, "z": -.15, "yaw": math.radians(-60), "roll": math.radians(12)}, .5),
        ]
        for name, change, duration in cases:
            if args.case!="all" and args.case!=name:
                continue
            goal = {**original, **{k: original[k] + v for k, v in change.items()}}
            sweep(goal, name + "_out", duration)
            hold(name + "_hold",args.hold)
            sweep(original, name + "_back", duration)
            hold(name + "_rest",args.hold)
        hold("after", 1.2+args.idle_seconds)
    except BaseException as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            send(directory, original)
            result["final_status"] = await_pose(directory, original)
        except BaseException as exc:
            result["cleanup_error"] = str(exc)
        stop.set()
        if thread.is_alive():
            thread.join(2)
        reader.close()
        args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    stable = [r for r in rows if r["cache_stable"]]
    summary = {"pid": cfg["pid"], "samples": len(rows), "stable_cache": len(stable),
               "read_failures": len(failures), "error": result.get("error"), "cleanup_error": result.get("cleanup_error"),
               "max_shadow_mm": max((r["shadow_max_mm"] for r in stable), default=None),
               "max_shadow_deg": max((r["shadow_max_deg"] for r in stable), default=None), "output": str(args.out)}
    print(json.dumps(summary, indent=2))
    if result.get("error") or result.get("cleanup_error") or not stable:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
