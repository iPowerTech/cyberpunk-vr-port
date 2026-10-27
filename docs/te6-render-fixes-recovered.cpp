// Readable reconstruction, NOT compiled or installed by this project.
// Derived from dabinn/cyberpunk-vr-port release DLLs, examined 2026-09-26.
// This is not the author's original C++ source. Unknown engine types are
// descriptive placeholders; address/lifetime guards need adaptation before use.
// See te6-testing2-render-review-20260926.md for evidence and limitations.

// HIGHLIGHT: additional per-view partition of the engine's framegraph cache.
// DLL highlight RVA 134A30; game lookup RVA 983C80.
// Assembly forwards all FIVE native arguments, despite Hex-Rays initially
// displaying only three. R9 and the fifth stack argument remain intact.
uint64_t RecoveredVrcamCacheKey(uint64_t originalHash, uint64_t vrcamName) {
    uint64_t h = 0x9EDCB08A284EFDDFull;
    for (int i=0; i<8; ++i) { h ^= uint8_t(originalHash); h *= 0x100000001B3ull; originalHash >>= 8; }
    for (int i=0; i<8; ++i) { h ^= uint8_t(vrcamName); h *= 0x100000001B3ull; vrcamName >>= 8; }
    return h;
}

CacheEntry* RecoveredGraphCacheLookup(Cache* cache, uint32_t frame,
                                      uint64_t hash, uint32_t count, bool* created) {
    if (vrcamName && tlsPreparedView && tlsPreparedViewName == vrcamName)
        hash = RecoveredVrcamCacheKey(hash, vrcamName);
    return OriginalGraphCacheLookup(cache, frame, hash, count, created);
}

// Existing GraphContextPrepare hook (game RVA 79ACA0), after the native call:
void RecoveredPublishPreparedView(Manager* manager) {
    tlsPreparedView = nullptr;
    tlsPreparedViewName = 0;
    if (manager && U32(manager,0x54) && Ptr(manager,0x48)) {
        auto* firstView = Ptr(Ptr(manager,0x48),0);
        if (firstView) {
            tlsPreparedView = firstView;
            tlsPreparedViewName = U64(firstView,0x28);
        }
    }
}

// HIGHLIGHT: FlagCompute, game RVA 1D49540; DLL highlight RVA 13B630.
// This happens during graph construction, BEFORE SetStreamlineConstants.
Flags* RecoveredFlagCompute(void* a1, void* a2, ViewContext* ctx, ViewData* view) {
    // Existing rect/environment setup omitted; it is unchanged by this patch.
    bool restore=false;
    uint32_t saved=0;
    if (ctx && view && StreamlineHistoryFix) {
        const uint64_t name=U64(ctx,0x28);
        if (name==0) { mainAa=U32(view,0xF94); mainAaSeen=true; }
        else if (name==vrcamName && mainAaSeen) {
            saved=U32(view,0xF94);
            if (saved!=mainAa) { U32(view,0xF94)=mainAa; restore=true; }
        }
    }
    Flags* flags=OriginalFlagCompute(a1,a2,ctx,view);
    if (restore) U32(view,0xF94)=saved;

    if (flags && IsVrcam(ctx) && ForceViewFlags) {
        uint64_t desired=ExistingMainFlagsWithReuseOptions();
        // Retain this view's freshly computed bit 33 when copying MAIN flags.
        if (StreamlineHistoryFix)
            desired=(desired & ~0x200000000ull) | (flags->word0 & 0x200000000ull);
        flags->word0=desired;
        // Existing second flag word / upscaler handling omitted.
    }
    return flags;
}

// TESTING2: added to resource resolver, game RVA 1F3D20; DLL RVA 150B70.
// It runs AFTER OriginalResolve and BEFORE the existing post-color handling.
void RecoveredFogHistorySync(uint32_t* handle, uint32_t* logicalKey,
                             uintptr_t returnRva, uintptr_t nodeWorkRva, int side) {
    if (!handle || !logicalKey || nodeWorkRva!=0x61C3BC ||
        (side!=0 && side!=1) || returnRva!=0x61D0C0) return;

    const uint64_t sequence=InterlockedIncrement(fogResolveSequence);
    if (side==0) {
        mainFogHandle=*handle;
        mainFogSequence=sequence;
    } else if (mainFogHandle && mainFogSequence && sequence>mainFogSequence &&
               sequence-mainFogSequence<=2 && *handle!=mainFogHandle) {
        *handle=mainFogHandle;
        // Original increments CyberpunkVR_DebugFogHistorySyncHits and logs
        // the first eight substitutions. It has no frame/pose/origin check here.
    }
}
