#pragma once
#include "Render/NativeStereoProbe.hpp"

namespace cvr::stereo::retained_geometry {
// Experimental native-engine recordings only: audited context owners are
// consumed at submission and lists Reset before reacquisition. Injected resource
// transitions do not support foreign/manual replay of a submitted recording.
bool Active();
inline constexpr UINT SourceAssemblyCommandLimit=2048,SourceAssemblyDetailLimit=65536;
bool InRenderPass(ID3D12GraphicsCommandList*);
void FrameBoundary();
bool Draw(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&);
bool Indirect(ID3D12GraphicsCommandList*,ID3D12CommandSignature*,UINT,ID3D12Resource*,UINT64,ID3D12Resource*,UINT64);
void Flush(ID3D12GraphicsCommandList*,UINT reason);
void BeforeBarriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
void BeforeTargets(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*);
void BeforeResources(ID3D12GraphicsCommandList*,ID3D12Resource* source,ID3D12Resource* destination,UINT reason);
void BeforeClose(ID3D12GraphicsCommandList*);
void Reset(ID3D12GraphicsCommandList*);
void RenderPass(ID3D12GraphicsCommandList*,bool begin);
bool BeforeSubmit(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
// Called after the actual source Execute calls and Submitted, outside the
// registered queue-order lock. No CPU fence completion is required.
void SubmitCurrent(ID3D12CommandQueue*);
UINT CurrentBoundary(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void CurrentPrefixExecuted(UINT count);
void CurrentSuffixExecuted(UINT count);
void OrderedExecution(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*,bool separateCalls);
}
extern "C" {
// Eight observation-only rows: side,slot,captured,bound,validatedAfterPrefix,
// failure,stage,readerCode,handleRefs,ownerRefs,owner,allocation,heap,bucket,
// bucketIndex,queueSlot. No access/residency/native export permission.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedTargetLifetime[128];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RetainedGeometryRequest,CyberpunkVR_RetainedGeometryState;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RetainedGeometryStage,CyberpunkVR_RetainedGeometryVerified;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparedCurrentEnable;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparedInfixEnable;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparedTailEnable;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparedVrsRefinementEnable;
// Per eye10Q: enabled,frame,firstOrdinal,lastOrdinal,sourceCount,
// baselineCaptured,baselineAllocationBytes,stage9,qualified,error.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedTail[20];
// enabled,frame,attempts,boundary,mainIndex,peerIndex,batchCount,suffixCount,
// batchId,consumer,graphEpoch,fingerprint,prefixExecuted,consumerSubmitted,suffixExecuted,error.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedInfix[16];
// armed,frame,packets,recordedBeforeLastSourceSubmit,mainWasSubmitted,peerWasSubmitted,
// payloadsProcessed,cbvPayloadComplete,overlayStage,submitted,mainSubmitted,peerSubmitted,
// queue,mainSignal,peerSignal,error. This records source events, not GPU completion.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedCurrent[16];
// Per eye8Q: frame,colors,privateAllocationBytes,captured,failure,writableTargetsValidated,reserved0,reserved0.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedBaseline[16];
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedGeometryStats[16];
// source,eye,allocation(0 deferred/1 local-cache/2 ring),failure,descriptors,tables,heap,base,capacity,cursorBefore,
// cursorAfter,list,generation,borrowedOriginalMask,referenceBound,submissionValid.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedNativeTableRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedNativeTableCount,CyberpunkVR_RetainedNativeTableRowBytes;
// source,parameter,count,immutable CPU heap,GPU handle,first source,first parameter,reused.
// Row cursorAfter-cursorBefore counts FRESH descriptors; descriptors is logical.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedNativeTableEntries;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedNativeTableEntryCount,CyberpunkVR_RetainedNativeTableEntryBytes;
// Native same-heap ring overflow reservations: see native_descriptors::Tables.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedNativeTableRings;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedNativeTableRingCount,CyberpunkVR_RetainedNativeTableRingBytes;
// Captured at CPU Seal, BEFORE payload readback processing.20Q per eye:
// frame,eye,sealOrder,list,generation,eyeSources,totalSources,peerSealed,
// peerSubmitted,peerCertified,peerQueue,peerFence,peerSignal,peerCompleted,
// finalizedIA,finiteTables,currentNativeQueue,cbvFinalized,nativeInstanceViews,payloadsProcessed.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceBoundaryRows[40];
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSourceBoundaryCount;
// frame,queue,peerList,peerGeneration,mainList,mainGeneration,peerIndex,mainIndex,
// batchCount,separateCalls,peerAlreadySubmitted,peerCertified,mainCertified,
// inputArenasFinalized,order(0 none/1 earlier submit/2 separate calls),executed.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceQueueOrder[16];
// Material24Q: source,eye,parameter,offset,type,resource,counter,cookie,status,
// failure,owner,nativeHandle,contentReceipt,allocation,list,generation,occurrence,
// sourceSubmissionValid,afterSourceValid,dimension,format,three reserved zeros.
// Status1 null SRV,2 deferred buffer,3 UAV,4 rejected asset,5 pinned immutable,
// 6 completed private T5 override. No destination-execution permission.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedMaterialRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedMaterialCount,CyberpunkVR_RetainedMaterialRowBytes;
// Source5Q: source,eye,dependencyCount,materialInputsReady,truncated.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedMaterialSources;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedMaterialSourceCount,CyberpunkVR_RetainedMaterialSourceBytes;
// Exact PS t4/t6 source nominations40Q; no copy or future read permission.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedMaterialBufferRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedMaterialBufferCount,CyberpunkVR_RetainedMaterialBufferRowBytes;
// Copies16Q: source,eye,register,nominationStatus,failure,owner,privateResource,
// sourceOffset,privateOffset,bytes,recorded,submissionValid,complete,list,generation,occurrence.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedMaterialBufferCopyRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedMaterialBufferCopyCount,CyberpunkVR_RetainedMaterialBufferCopyRowBytes;
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedGeometryDifferent[12],CyberpunkVR_RetainedGeometryBytes[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_RetainedSourceStreamEnable;
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceStreamStats[16],CyberpunkVR_RetainedSourceStreamRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSourceStreamCount,CyberpunkVR_RetainedSourceStreamRowBytes;
// Producer40Q: source,eye,visibility,register,parameter,offset,logical,reserved,
// completedProducer,creator,resource,CBVaddress,sourceToken,list,generation,
// context,registry,creationToken,list,generation,context,registry,directEligible,zero,
// writerNonce,writerBytes,writerDestination,writerToken,list,generation,context,
// registry,transientEligible,seven reserved zeros.
// These are actual held recording identities, not a copy/epoch certificate.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceCbvProducerRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSourceCbvProducerCount,CyberpunkVR_RetainedSourceCbvProducerRowBytes;
// Coverage28Q: selector-input index,source,eye,querySucceeded,24Q exact current
// generic cache diagnostics. No historical bytes, GPU epoch or copy permission.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSelectorCoverageRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSelectorCoverageCount,CyberpunkVR_RetainedSelectorCoverageRowBytes;
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSelectorCacheEventRows;
// Events36Q: input index,source,eye,querySucceeded,32Q bounded metadata history.
// Truncated/missing history stays unknown and never supplies reusable bytes.
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSelectorCacheEventCount,CyberpunkVR_RetainedSelectorCacheEventRowBytes;
// Stats16Q: stage,frame,commands,builds,buildFailures,ready,missingCbvs,tables,
// originalDescriptors,cpuAssemblies,bankCreated,firstFailure,truncated,
// bankConsumed,executionPermission,nativeCommandsSuppressed. Last three stay zero.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceAssemblyStats[16];
// Unique descriptor extents, unique CPU tables, hits, misses. Source-coordinate
// counts stay in Stats16Q; these describe CPU allocation reuse only.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceAssemblyCacheStats[4];
// Attempted,created,source draws,resource descriptors,HRESULT,consumed (zero).
// The original-root shader-visible bank is prepared but not executed here.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceAssemblyBankStats[6];
// Stage,input ordinal,parameter,reason bits from the actual bank rejection.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceAssemblyBankFailure[4];
// Current CBV copies8Q: source,eye,list,generation,occurrence,owner,count,bytes.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceCbvCopyOwnerRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSourceCbvCopyOwnerCount,CyberpunkVR_RetainedSourceCbvCopyOwnerRowBytes;
// Private structured-view preparation only; no native draw is consumed.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedT5OverlayStats[16],CyberpunkVR_RetainedT5OverlayRows,CyberpunkVR_RetainedT5OverlayBlob;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedT5OverlayCount,CyberpunkVR_RetainedT5OverlayRowBytes,CyberpunkVR_RetainedT5OverlayBlobBytes;
// Native IA preparation only, preserving full original view coordinates.
// Gate is CyberpunkVR_NativeIaSnapshotsEnable (default0); requires the original
// retained source stream. Unsupported whole sets remain original and deferred.
// Commands16Q: source,eye,observed,viewCount,admitted,prepared,recorded,failure,
// copiedCount,copiedBytes,list,generation,occurrence,preparedOwner,finalized,
// actuallySubmitted (after validated native Execute and successful fence signal).
// Observations67Q: source,eye,viewOrdinal,ia_snapshot::Observation POD64Q.
// Views16Q: source,eye,role,slot,resource,owner,sourceOffset,privateOffset,bytes,
// reservedBytes,viewGpu,viewBytes,strideOrFormat,finalized,actuallySubmitted,
// consumed (zero). These receipts do not freeze finite/bindless shader resources
// or grant full-input readiness, a future original epoch or draw suppression.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedIaCommands,CyberpunkVR_RetainedIaObservations,CyberpunkVR_RetainedIaViews;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedIaCommandCount,CyberpunkVR_RetainedIaObservationCount,CyberpunkVR_RetainedIaViewCount;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedIaCommandBytes,CyberpunkVR_RetainedIaObservationBytes,CyberpunkVR_RetainedIaViewBytes;
// Reuse8Q per view: source,eye,role,slot,originOccurrence,reused,privateOffset,bytes.
// Transfer5Q per command: source,eye,actualCopiedBytes,reusedBytes,arenaBytes.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedIaReuseRows,CyberpunkVR_RetainedIaTransferRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedIaReuseCount,CyberpunkVR_RetainedIaTransferCount;
// IA selection5Q: source,eye,separateSlots(0 or128),originalMask,copiedMask.
// An IA receipt with separate slot7 is incomplete without its GPU-matched
// compact instance version. Command copiedBytes counts the union of ranges.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedIaSelections;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedIaSelectionCount,CyberpunkVR_RetainedIaSelectionBytes;
// Private geometry16Q: source,eye,complete,originalMask,list,generation,
// occurrence,IAowner,IAviews,instanceResource,instanceOwner,instanceViewGpu,
// firstInstance,instanceCount,drawsRecorded0,permission0.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedPrivateGeometryRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedPrivateGeometryCount,CyberpunkVR_RetainedPrivateGeometryRowBytes;
// Instance rows48Q: source/eye/kind/queried/captured/owner/blobFirst/blobBytes,
// pipeline/shaderId/resource/gpu/viewGpu/viewBytes/stride/validAtSourceSubmit,
// followed by the observer32Q. Completed CPU bytes remain provisional: no
// source GPU-byte equality, original no-overwrite or native draw permission.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedInstanceRows,CyberpunkVR_RetainedInstanceBlob;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedInstanceCount,CyberpunkVR_RetainedInstanceRowBytes,CyberpunkVR_RetainedInstanceBlobBytes;
// GPU rows24Q: source,eye,recorded,allowed,submitted,byteMatch(0/1/2),
// readback,predicate,originalResource,sourceOffset,destinationOffset,bytes,
// predicateOffset,list,generation,occurrence,owner,cpuBlobFirst,gpuBlobFirst,
// sourceFence,signalValue,completed,0,0. Rejected predicates carry no GPU bytes.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedInstanceGpuRows,CyberpunkVR_RetainedInstanceGpuBlob;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedInstanceGpuCount,CyberpunkVR_RetainedInstanceGpuRowBytes,CyberpunkVR_RetainedInstanceGpuBlobBytes;
// Overlay24Q: source,eye,version,root,list,generation,occurrence,pipeline,
// shaderId,originalGpu,originalSize,stride,privateResource,owner,viewGpu,
// targetOffset,copiedBytes,backingWidth,recorded,completed,GPUmatch,
// originalGpuBlobFirst,privateCopyList,privateCopyGeneration.
// Stats8Q: state,versions,copiedBytes,backingBytes,signal,completed,consumed0,permission0.
// Mode2 uses one32MiB GPU arena per eye, copying at the original draw. Versions
// count all recorded copies; rows/blob contain completed matches. The historical
// backingBytes field is the sum of required view extents, NOT allocated capacity.
// Row version indices are per-eye; copyList/generation equal the source recording.
// signal/completed refer to ORIGINAL source fences, not an extra copy submission.
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedInstanceOverlayMode;
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedInstanceOverlayStats[8],CyberpunkVR_RetainedInstanceOverlayRows,CyberpunkVR_RetainedInstanceOverlayBlob;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedInstanceOverlayCount,CyberpunkVR_RetainedInstanceOverlayRowBytes,CyberpunkVR_RetainedInstanceOverlayBlobBytes;
// Command32Q: source,eye,built,failure,ready,missing,originalTables,
// originalDescriptors,privateCbvs,dynamicCoverage,arenaFinalized,list,generation,
// occurrence,root,family,selectorStatus,selectorByteMatch,finite,sparse,
// sparseDescriptors,cpuAssembled,bankCreated,bankConsumed,executionPermission,
// suppressed,HRESULT,tableFirst,tableCount,missingFirst,missingCount,requiredCbvs.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedSourceAssemblyCommands,CyberpunkVR_RetainedSourceAssemblyTables,CyberpunkVR_RetainedSourceAssemblyMissingCbvs;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSourceAssemblyCommandCount,CyberpunkVR_RetainedSourceAssemblyTableCount,CyberpunkVR_RetainedSourceAssemblyMissingCbvCount;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedSourceAssemblyCommandBytes,CyberpunkVR_RetainedSourceAssemblyTableBytes,CyberpunkVR_RetainedSourceAssemblyMissingCbvBytes;
// Coverage20Q: source,eye,querySucceeded,reserved,16Q current-cache diagnostic.
// Describes exact observed intervals only; grants no ownership or permission.
__declspec(dllexport) extern uint64_t CyberpunkVR_RetainedT5CoverageRows;
__declspec(dllexport) extern uint32_t CyberpunkVR_RetainedT5CoverageCount,CyberpunkVR_RetainedT5CoverageRowBytes;
// Table16Q: source,eye,parameter,originalExtent,kind(1 finite/2 sparse),
// capturedDescriptors,cpuHeap,owner,identity,version,sourceGpu,sourceHeap,
// assembledCpuHeap,bankCreated,bankConsumed,reserved.
// Missing16Q: source,eye,visibility,register,space,parameter,offset,reflected,
// originalPadded,heapType,reason(1 no private owner/2 insufficient copied bytes),
// originalResource,stampGpu,stampHeap,originalReadEpochCertified,UPLOADNotAdmitted.
}
