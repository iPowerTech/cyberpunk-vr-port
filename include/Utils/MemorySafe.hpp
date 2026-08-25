#pragma once

#include <cstdint>
#include <windows.h>

// ================================================================================================
// Reading engine memory that may not be there.
//
// Every one of these is a __try around a single dereference. They exist because this mod walks
// pointers the engine owns and does not always have a way to know a structure has been freed or a
// field is not yet written -- so the alternative to a guard is a crash in someone else's code with
// our module on the stack.
//
// INLINE, IN A HEADER, ON PURPOSE. The hooks each live in their own translation unit now and
// nearly all of them read engine memory; a single out-of-line copy would mean one more thing for
// every new hook file to link against. MSVC compiles __try inside an inline function without
// complaint -- what it forbids is __try in a function that also needs C++ unwinding, and none of
// these has an object with a destructor.
// ================================================================================================

inline bool ReadFloatSafe(uintptr_t addr, float* out) {
    __try {
        *out = *reinterpret_cast<const float*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool WriteFloatSafe(uintptr_t addr, float value) {
    __try {
        *reinterpret_cast<float*>(addr) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool ReadU8Safe(uintptr_t addr, uint8_t* out) {
    __try {
        *out = *reinterpret_cast<const uint8_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool ReadU64Safe(uintptr_t addr, uint64_t* out) {
    __try {
        *out = *reinterpret_cast<const uint64_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool ReadU32Safe(uintptr_t addr, uint32_t* out) {
    __try {
        *out = *reinterpret_cast<const uint32_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool ReadPtrSafe(uintptr_t addr, uintptr_t* out) {
    __try {
        *out = *reinterpret_cast<const uintptr_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
