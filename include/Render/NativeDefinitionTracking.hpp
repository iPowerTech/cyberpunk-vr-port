#pragma once
#include "Render/NativeGraphSnapshot.hpp"

namespace cvr::stereo::definition_tracking {
void PrepareCodeGuards() noexcept;
void BeforeCacheLookup(uint64_t cache,uint32_t use,uint64_t key,uint32_t views,uint64_t missed,uint64_t caller) noexcept;
uint64_t BeginRoot(uint64_t renderer,uint64_t caller) noexcept;
void EndRoot(uint64_t scope) noexcept;
uint64_t BeforeDeclare(uint64_t graph,uint64_t context,uint64_t job,uint64_t caller) noexcept;
void AfterDeclare(uint64_t consumerId) noexcept;
bool CopyOwnedDeclarationSnapshot(uint64_t consumerId,graph_snapshot::Snapshot&) noexcept;
uint64_t BeforeOwnedResourceCompile(uint64_t pool,uint8_t mode,uint64_t job,uint64_t caller) noexcept;
void AfterOwnedResourceCompile(uint64_t consumerId,uint64_t pool) noexcept;
void CloseOwnedExecution() noexcept;
void Match(const graph_snapshot::Snapshot&,uint64_t graph,uint64_t frameOwner,uint64_t epoch,uint64_t generation);
void Poll() noexcept;
uint64_t BeginResourceQueue(uint64_t context,uint64_t graph) noexcept;
void EndResourceQueue(uint64_t scope) noexcept;
uint64_t BeforeResourceReset(uint64_t pool,uint8_t mode,uint64_t caller) noexcept;
void AfterResourceReset(uint64_t consumer,uint64_t pool,uint8_t mode) noexcept;
bool QueueIdentity(uint64_t payload,uint64_t graph,uint64_t caller,uint64_t& epoch,uint64_t& generation);
}
