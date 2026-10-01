#pragma once
#include "Framegen/Inputs.hpp"

namespace cvr::framegen {
struct InputTag {ID3D12Resource* resource{};uint32_t state{},type{},x{},y{},width{},height{};};
bool CopyInput(ID3D12GraphicsCommandList* list,const InputTag& tag,Inputs& out);
}
