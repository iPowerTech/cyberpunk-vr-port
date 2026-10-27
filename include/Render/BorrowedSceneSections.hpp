#pragma once
#include "Render/NativeSceneSections.hpp"
#include "Render/SceneGraphExpansion.hpp"
#include <memory>

namespace cvr::stereo::borrowed_scene_sections {
struct Code {uint32_t rva,bytes;uint64_t hash;};
// All six slots are fixed. Slot zero is observed only: it must NEVER run on a
// borrowed view, whose arrays have no native allocator trailer or shared refs.
inline constexpr std::array<uint32_t,6> VtableTargets{
    0x986590,0x1EC1D0,0x18E3F20,0x1ED074,0x1ED0C0,0x18E4120};
// IDA function chunks, including out-of-line tails. The execution owner must
// validate these and VtableTargets before any view reaches a native consumer.
inline constexpr std::array<Code,18> Contract{{
    {0x1EC1D0,564,0x85BD1B29D91363E0ull},{0x1E3CFD2,13,0x45AA008E5488D91Dull},
    {0x18E3F20,11,0x6742FAD141A0206Eull},{0x1ED074,73,0xA5C05D6FC50BA96Bull},
    {0x1ED0C0,65,0x24F470AA441F6484ull},{0x18E4120,6,0x2505EF84B8531C22ull},
    {0xAA02E0,168,0xAE83FAF375EBBE6Bull},{0xA98414,175,0x3D60EF291D8F6BC5ull},
    {0x1F56840,22,0x0234DDD8CBBC69C1ull},{0xA98398,122,0xFC489A11F1BDB31Full},
    {0x986590,57,0xFB001360F09560ECull},{0x1F360B8,19,0x6E792A3E7687D9C6ull},
    {0x9865CC,49,0x26D91F5F2A4E039Bull},{0x98598C,54,0xC96F84703458F656ull},
    {0x1444C4,41,0x86F34059C9A7E048ull},{0x14B4C4,107,0xF3B7741721FCEFF4ull},
    {0x1E297C2,62,0x84DEF70F3A5107A9ull},{0x985294,95,0x30C633724C0CACD1ull}
}};

// Identity of a separately admitted extraReader. These numbers are not an
// ownership receipt: the caller must join them to its live retirement state.
struct CallerLifetime {
    uint64_t consumerId{},epoch{},pairGeneration{},originalRoot{};
    uint64_t ownerHeader{},ownerGeneration{},ownerBornEpoch{},protectionReceipt{};
    bool operator==(const CallerLifetime&)const=default;
};
enum class Failure:uint32_t {None,AlreadyPrepared,Source,Binding,Sections,Allocation,NotPrepared,AlreadyBound,Changed};

// Only the C++ headers/arrays are owned. Native handles remain borrowed PODs:
// no AddRef, Release, native destructor, or native unique-owner insertion.
// Retain this object in the Consumer (or quarantine) until all native readers,
// including async range jobs carrying a raw parent pointer, have completed.
class Sections {
public:
    Sections()=default;
    Sections(const Sections&)=delete;
    Sections& operator=(const Sections&)=delete;
    Sections(Sections&&)noexcept=default;
    Sections& operator=(Sections&&)noexcept=default;

    // Preparation exposes stable addresses for topology compilation only. No
    // prospective reader identity is accepted in place of a real admission.
    bool Prepare(const scene_sections::Snapshot& source)try{
        if(storage_){failure_=Failure::AlreadyPrepared;return false;}
        failure_=Failure::Source;if(!source.Ready() || source.ExecutionPermission())return false;
        failure_=Failure::Sections;
        if(source.Handles().size()!=scene_sections::ChildCount || source.Children().size()!=scene_sections::ChildCount ||
            source.SplitIndex()!=scene_sections::PrefixChildren || source.PrefixCount()!=scene_sections::PrefixChildren ||
            source.TailCount()!=scene_sections::TailChildren)return false;
        auto next=std::make_shared<Storage>();next->source=source;
        const std::array<uint32_t,2> begins{0,source.SplitIndex()},counts{source.PrefixCount(),source.TailCount()};
        for(size_t i=0;i<2;++i){auto& section=next->sections[i];
            section.header=source.RawHeader();
            section.handles.assign(source.Handles().begin()+begins[i],source.Handles().begin()+begins[i]+counts[i]);
            Put(section.header,40,reinterpret_cast<uintptr_t>(section.handles.data()));
            Put(section.header,48,counts[i]);Put(section.header,52,counts[i]);
            auto& definition=next->definitions[i];definition.object=reinterpret_cast<uintptr_t>(section.header.data());
            std::copy_n(section.header.begin(),definition.prefix.size(),definition.prefix.begin());
        }
        binding_={};storage_=std::move(next);failure_=Failure::None;return true;
    }catch(...){failure_=Failure::Allocation;return false;}

    // The caller supplies the identity returned by actual extraReader admission
    // for the now-final graph. Binding does not mutate or relocate view storage.
    bool Bind(const scene_sections::Snapshot& source,const CallerLifetime& binding){
        if(!storage_){failure_=Failure::NotPrepared;return false;}
        if(Bound()){failure_=Failure::AlreadyBound;return false;}
        failure_=Failure::Binding;if(!source.Ready() || !BindingMatches(source,binding))return false;
        failure_=Failure::Changed;if(!SourceMatches(source))return false;
        binding_=binding;failure_=Failure::None;return true;
    }
    bool Create(const scene_sections::Snapshot& source,const CallerLifetime& binding){
        return Prepare(source) && Bind(source,binding);
    }

    // Pure identity comparison, not a fresh native-memory or lifetime check.
    // The caller separately requires source.MatchesBefore(read) and the exact
    // live extraReader, code contract, native graph and completion receipts.
    bool Matches(const scene_sections::Snapshot& source,const CallerLifetime& binding)const{
        return Bound() && binding_==binding && BindingMatches(source,binding) && SourceMatches(source);
    }
    bool Prepared()const{return bool(storage_);}
    bool Bound()const{return storage_ && binding_.protectionReceipt;}
    bool Ready()const{return Bound();}
    Failure Rejection()const{return failure_;}
    // Returned pointers remain borrowed from this owner. Prepared but unbound
    // definitions may be used for topology only, never native execution.
    std::array<scene_expansion::Definition,2> Definitions()const{return storage_?storage_->definitions:std::array<scene_expansion::Definition,2>{};}
    bool ExecutionPermission()const{return false;}
    bool NativeOwnership()const{return false;}
private:
    bool SourceMatches(const scene_sections::Snapshot& source)const{
        if(!storage_ || !source.Ready())return false;
        const auto& saved=storage_->source;
        if(saved.RootIdentity()!=source.RootIdentity() || saved.RawHeader()!=source.RawHeader() ||
            saved.Handles()!=source.Handles() || saved.Children()!=source.Children() ||
            saved.SplitIndex()!=source.SplitIndex() || saved.PrefixCount()!=source.PrefixCount() || saved.TailCount()!=source.TailCount())return false;
        for(size_t i=0;i<2;++i){const auto& a=saved.RenderElements()[i];const auto& b=source.RenderElements()[i];
            if(a.Object()!=b.Object() || a.TreeIndex()!=b.TreeIndex() || a.RawHeader()!=b.RawHeader() || a.Stages()!=b.Stages())return false;
            const auto& section=storage_->sections[i];const uint32_t begin=i?source.SplitIndex():0;
            const uint32_t count=i?source.TailCount():source.PrefixCount();
            if(section.handles.size()!=count || !std::equal(section.handles.begin(),section.handles.end(),source.Handles().begin()+begin) ||
                !std::equal(section.header.begin(),section.header.begin()+40,source.RawHeader().begin()) ||
                graph_snapshot::Field<uint64_t>(section.header,40)!=reinterpret_cast<uintptr_t>(section.handles.data()) ||
                graph_snapshot::Field<uint32_t>(section.header,48)!=count || graph_snapshot::Field<uint32_t>(section.header,52)!=count ||
                storage_->definitions[i].object!=reinterpret_cast<uintptr_t>(section.header.data()) ||
                !std::equal(storage_->definitions[i].prefix.begin(),storage_->definitions[i].prefix.end(),section.header.begin()))return false;
        }
        return true;
    }
    struct Section {alignas(8) scene_sections::Header header{};std::vector<definition_tree::NativeHandle> handles;};
    struct Storage {
        scene_sections::Snapshot source;
        std::array<Section,2> sections;std::array<scene_expansion::Definition,2> definitions;
    };
    template<class T>static void Put(scene_sections::Header& header,size_t offset,T value){std::memcpy(header.data()+offset,&value,sizeof(value));}
    static bool BindingMatches(const scene_sections::Snapshot& source,const CallerLifetime& binding){
        return binding.consumerId && binding.epoch && binding.pairGeneration && binding.ownerGeneration && binding.ownerBornEpoch &&
            binding.protectionReceipt && binding.ownerHeader && !(binding.ownerHeader&7) && binding.ownerHeader<=UINT64_MAX-16 &&
            binding.originalRoot && !(binding.originalRoot&7) && binding.originalRoot<=UINT64_MAX-56 &&
            binding.originalRoot==graph_snapshot::Field<uint64_t>(source.RootIdentity().bytes,16);
    }
    std::shared_ptr<const Storage> storage_;
    CallerLifetime binding_;
    Failure failure_=Failure::None;
};
}
