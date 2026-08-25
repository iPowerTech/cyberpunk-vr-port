// Shared access to vrcam.json — the single file that says which authored VRCAM component is
// active and which ones exist at all.
//
// Two consumers, one file:
//   * sync_stereo.cpp  — derives the VRCAM view key (CName hash of the virtualCameraName) from
//                        the selection, so nothing in the render path hardcodes a resolution.
//   * launcher_dialog  — lists the authored components and writes the user's pick back.
//
// The file lives in the CET mod folder because CET sandboxes Lua file IO there and
// modules/vrcam_select.lua must read the same file; native code can reach out, Lua cannot.
//
// Header-only so both TUs share one implementation instead of two drifting copies.

#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cvr {

// The engine's CName hash. Used to turn a virtualCameraName into the value the render
// pipeline compares against at view-ctx+0x28.
inline uint64_t cname_hash(const char* s) {
    uint64_t h = 0xCBF29CE484222325ULL;              // FNV-1a 64 offset basis
    for (; s && *s; ++s) {
        h ^= (uint64_t)(uint8_t)*s;
        h *= 0x100000001B3ULL;
    }
    return h;
}

// Pull the string value of "key" out of a flat JSON object, returning the span so callers can
// also rewrite it in place. Deliberately a scanner, not a parser: this file is a handful of
// fields and a comment block, and a JSON dependency would be the larger risk.
// A ':' is required between the key and its value, so a key name quoted inside a comment
// string cannot be mistaken for the real field.
inline bool json_find_string_span(const std::string& text, const char* key,
                                  size_t* value_pos, size_t* value_len) {
    const size_t klen = strlen(key);
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        const size_t name = i + 1;
        const size_t name_end = text.find('"', name);
        if (name_end == std::string::npos) return false;
        i = name_end + 1;
        if (name_end - name != klen || text.compare(name, klen, key) != 0) continue;
        size_t q = i;
        while (q < text.size() && (text[q] == ' ' || text[q] == '\t' ||
                                   text[q] == '\r' || text[q] == '\n')) ++q;
        if (q >= text.size() || text[q] != ':') continue;   // not "key": ... -> keep looking
        ++q;
        while (q < text.size() && (text[q] == ' ' || text[q] == '\t' ||
                                   text[q] == '\r' || text[q] == '\n')) ++q;
        if (q >= text.size() || text[q] != '"') return false;   // value is not a string
        const size_t val = q + 1;
        const size_t val_end = text.find('"', val);
        if (val_end == std::string::npos) return false;
        *value_pos = val;
        *value_len = val_end - val;
        return true;
    }
    return false;
}

inline bool json_find_string(const std::string& text, const char* key, std::string* out) {
    size_t pos = 0, len = 0;
    if (!json_find_string_span(text, key, &pos, &len) || len == 0) return false;
    out->assign(text, pos, len);
    return true;
}

// Collect the strings of a `"key": [ "a", "b" ]` array.
inline bool json_find_string_array(const std::string& text, const char* key,
                                   std::vector<std::string>* out) {
    const size_t klen = strlen(key);
    size_t i = 0;
    while ((i = text.find('"', i)) != std::string::npos) {
        const size_t name = i + 1;
        const size_t name_end = text.find('"', name);
        if (name_end == std::string::npos) return false;
        i = name_end + 1;
        if (name_end - name != klen || text.compare(name, klen, key) != 0) continue;
        size_t q = i;
        while (q < text.size() && isspace((unsigned char)text[q])) ++q;
        if (q >= text.size() || text[q] != ':') continue;
        ++q;
        while (q < text.size() && isspace((unsigned char)text[q])) ++q;
        if (q >= text.size() || text[q] != '[') return false;
        const size_t end = text.find(']', q);
        if (end == std::string::npos) return false;
        size_t p = q + 1;
        while (p < end) {
            const size_t s = text.find('"', p);
            if (s == std::string::npos || s > end) break;
            const size_t e = text.find('"', s + 1);
            if (e == std::string::npos || e > end) break;
            if (e > s + 1) out->emplace_back(text, s + 1, e - s - 1);
            p = e + 1;
        }
        return true;
    }
    return false;
}

// <dir of the given module>\<rel>. module == nullptr gives the running exe (i.e. bin\x64).
inline bool path_beside_module(HMODULE mod, const char* rel, char* out, size_t out_size) {
    char dir[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(mod, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    char* slash = strrchr(dir, '\\');
    if (!slash) return false;
    *slash = '\0';
    return _snprintf_s(out, out_size, _TRUNCATE, "%s\\%s", dir, rel) > 0;
}

inline HMODULE this_module() {
    HMODULE hm = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&this_module), &hm);
    return hm;
}

// Canonical location first (CET mod folder, next to the exe's bin\x64), then a fallback next to
// this DLL so the plugin still works in a layout without CET.
inline bool vrcam_config_path(char* out, size_t out_size) {
    static const char* const kCetRel =
        "plugins\\cyber_engine_tweaks\\mods\\CyberpunkVRPort_Stereo\\vrcam.json";
    if (path_beside_module(nullptr, kCetRel, out, out_size) &&
        GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    if (path_beside_module(this_module(), "vrcam.json", out, out_size) &&
        GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    // Nothing exists yet: still report the canonical path so callers can create/log it.
    return path_beside_module(nullptr, kCetRel, out, out_size);
}

// Reads the WHOLE file. It used to stop at 8 KiB, which is fine until the catalogue grows: a
// truncated file loses its closing ']', json_find_string_array then finds no array, and every
// resolution reports "not in the authored list" -- a total, silent failure whose only symptom is
// the wrong second-eye resolution. The file is a few KiB; there is nothing to save by capping it.
inline bool vrcam_config_read(std::string* text, char* path_out, size_t path_size) {
    char path[MAX_PATH] = {};
    if (!vrcam_config_path(path, sizeof(path))) return false;
    if (path_out) strncpy_s(path_out, path_size, path, _TRUNCATE);
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    text->clear();
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        text->append(buf, n);
        if (text->size() > (1u << 20)) break;   // a megabyte of selection file is a corrupt file
    }
    fclose(f);
    return !text->empty();
}

// Derive vrcam_feed_<suffix> from vrcam_<suffix> — the authored naming convention.
inline bool vrcam_derive_camera(const char* component, char* out, size_t out_size) {
    static const char kPrefix[] = "vrcam_";
    const size_t plen = sizeof(kPrefix) - 1;
    if (!component || strncmp(component, kPrefix, plen) != 0) return false;
    return _snprintf_s(out, out_size, _TRUNCATE, "vrcam_feed_%s", component + plen) > 0;
}

// Replace the "component" value, leaving comments, formatting and the authored list untouched
// (rewriting the whole file would throw away the user's own notes in _comment).
// "virtualCamera" is rewritten in the same pass whenever it is present: leaving it behind is
// what silently breaks everything -- the view key is hashed from the CAMERA name, so a stale
// camera field means the render path never recognises the VRCAM view at all (no stereo, no
// mirror window) while every log line still shows the freshly picked component.
inline bool vrcam_config_write_component(const char* component) {
    std::string text;
    char path[MAX_PATH] = {};
    if (!vrcam_config_read(&text, path, sizeof(path))) return false;

    char camera[128] = {};
    const bool have_camera = vrcam_derive_camera(component, camera, sizeof(camera));

    // Rewrite the later field first so the earlier replacement cannot shift its offsets.
    size_t cpos = 0, clen = 0, vpos = 0, vlen = 0;
    const bool has_comp = json_find_string_span(text, "component", &cpos, &clen);
    const bool has_vcam = have_camera && json_find_string_span(text, "virtualCamera", &vpos, &vlen);
    if (!has_comp) return false;
    if (has_vcam && vpos > cpos) {
        text.replace(vpos, vlen, camera);
        text.replace(cpos, clen, component);
    } else {
        text.replace(cpos, clen, component);
        if (has_vcam) {
            // offsets moved: re-find it
            if (json_find_string_span(text, "virtualCamera", &vpos, &vlen))
                text.replace(vpos, vlen, camera);
        }
    }

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const size_t n = fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return n == text.size();
}

// <CET mods>\CyberpunkVRPort_Stereo\bridge\<name> — the folder the CET mod polls. Used for the
// one-way native -> Lua requests (component enable/disable) that RTTI can only do from Lua.
inline bool vrcam_bridge_path(const char* name, char* out, size_t out_size) {
    char cfg[MAX_PATH] = {};
    if (!vrcam_config_path(cfg, sizeof(cfg))) return false;
    char* slash = strrchr(cfg, '\\');
    if (!slash) return false;
    *slash = '\0';
    return _snprintf_s(out, out_size, _TRUNCATE, "%s\\bridge\\%s", cfg, name) > 0;
}

inline bool vrcam_bridge_write(const char* name, const char* body) {
    char path[MAX_PATH] = {};
    if (!vrcam_bridge_path(name, path, sizeof(path))) return false;
    char dir[MAX_PATH] = {};
    strncpy_s(dir, sizeof(dir), path, _TRUNCATE);
    if (char* slash = strrchr(dir, '\\')) { *slash = '\0'; CreateDirectoryA(dir, nullptr); }
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fputs(body, f);
    fclose(f);
    return true;
}

// Parse the "<W>x<H>" tail of a component name. Returns false for names that do not carry one,
// which is how the launcher tells a resolution component from anything else.
inline bool vrcam_parse_resolution(const char* component, int* w, int* h) {
    const char* underscore = component ? strrchr(component, '_') : nullptr;
    if (!underscore) return false;
    int a = 0, b = 0;
    char tail = 0;
    if (sscanf(underscore + 1, "%dx%d%c", &a, &b, &tail) != 2) return false;
    if (a <= 0 || b <= 0) return false;
    *w = a; *h = b;
    return true;
}

} // namespace cvr
