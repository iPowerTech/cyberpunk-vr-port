#pragma once
#include <d3d12.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <variant>
#include <type_traits>
#include <array>
struct ID3D11Resource;

namespace cvr::foveated::ngx {
using Result = uint32_t;
struct Handle;
// Public NGX parameter ABI. The game owns the object; no private layout is read.
struct Parameters {
    virtual void Set(const char*, unsigned long long) = 0;
    virtual void Set(const char*, float) = 0;
    virtual void Set(const char*, double) = 0;
    virtual void Set(const char*, unsigned) = 0;
    virtual void Set(const char*, int) = 0;
    virtual void Set(const char*, ID3D11Resource*) = 0;
    virtual void Set(const char*, ID3D12Resource*) = 0;
    virtual void Set(const char*, void*) = 0;
    virtual Result Get(const char*, unsigned long long*) const = 0;
    virtual Result Get(const char*, float*) const = 0;
    virtual Result Get(const char*, double*) const = 0;
    virtual Result Get(const char*, unsigned*) const = 0;
    virtual Result Get(const char*, int*) const = 0;
    virtual Result Get(const char*, ID3D11Resource**) const = 0;
    virtual Result Get(const char*, ID3D12Resource**) const = 0;
    virtual Result Get(const char*, void**) const = 0;
    virtual void Reset() = 0;
};
inline bool Success(Result result) { return (result & 0xFFF00000u) != 0xBAD00000u; }
template<class T> T Read(const Parameters* p, const char* name, T fallback = {}) {
    T value{}; return p && Success(p->Get(name, &value)) ? value : fallback;
}
inline unsigned Integer(const Parameters* p, const char* name, unsigned fallback = 0) {
    unsigned value{}; if (p && Success(p->Get(name, &value))) return value;
    int signedValue{}; return p && Success(p->Get(name, &signedValue)) ? unsigned(signedValue) : fallback;
}
struct FeatureConfig {
    unsigned outputW{}, outputH{}, flags{}, quality{};
    std::array<unsigned,6> presets{};
    bool Matches(const FeatureConfig& other) const {
        return outputW==other.outputW && outputH==other.outputH && flags==other.flags && quality==other.quality;
    }
};
// NGX dispatch can substitute a handle or parameter object between Create and
// Evaluate. Prefer exact identities, then accept only unambiguous configurations.
class CreationRegistry {
    struct Entry { const Handle* handle; const Parameters* parameters; unsigned view; FeatureConfig config; };
    std::vector<Entry> entries;
public:
    void Remember(const Handle* handle,const Parameters* parameters,unsigned view,const FeatureConfig& config) {
        if(entries.size()>=128)entries.erase(entries.begin());
        entries.push_back({handle,parameters,view,config});
    }
    bool Find(const Handle* handle,const Parameters* parameters,unsigned view,FeatureConfig& config) const {
        const Entry* best{};unsigned bestRank{};bool ambiguous=false;
        for(auto it=entries.rbegin();it!=entries.rend();++it) {
            if(!it->config.Matches(config))continue;
            const unsigned rank=it->handle==handle?3:it->parameters==parameters?2:(view && it->view==view)?1:0;
            if(!best || rank>bestRank){best=&*it;bestRank=rank;ambiguous=false;}
            else if(rank==0 && bestRank==0 && it->config.presets!=best->config.presets)ambiguous=true;
        }
        if(!best || ambiguous)return false;
        config=best->config;return true;
    }
};
// Restore every touched parameter even when private creation/evaluation fails.
class Override {
    using Value = std::variant<unsigned, float, ID3D12Resource*>;
    struct Entry { const char* name; Value value; };
    Parameters* parameters;
    std::vector<Entry> entries;
public:
    explicit Override(Parameters* p) : parameters(p) { entries.reserve(32); }
    template<class T> void Set(const char* name, T value) {
        bool seen = false;
        for (const auto& entry : entries) if (!std::strcmp(name, entry.name)) { seen = true; break; }
        if (!seen) {
            if constexpr (std::is_same_v<T,unsigned>) entries.push_back({name,Integer(parameters,name)});
            else entries.push_back({name,Read<T>(parameters,name)});
        }
        parameters->Set(name,value);
    }
    ~Override() { for (auto it=entries.rbegin();it!=entries.rend();++it)
        std::visit([&](auto value){parameters->Set(it->name,value);},it->value); }
};
using Create = Result (*)(ID3D12GraphicsCommandList*, unsigned, Parameters*, Handle**);
using Evaluate = Result (*)(ID3D12GraphicsCommandList*, const Handle*, const Parameters*, void*);
using Release = Result (*)(Handle*);
}
