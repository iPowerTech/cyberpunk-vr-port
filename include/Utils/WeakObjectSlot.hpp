#pragma once
#include <cstdint>
#include <mutex>
#include <utility>

namespace cvr {
// Bind only from a callback whose caller already owns the object. The slot
// keeps a weak reference, never recreates a handle from a remembered address.
template<class Weak> class WeakObjectSlot {
    using Strong=decltype(std::declval<const Weak&>().Lock());
    mutable std::mutex mutex;
    Weak value{};
    uintptr_t identity{},reference{};
public:
    void Bind(uintptr_t address,uintptr_t control,const Weak& observed) {
        if(!address || !control)return;
        std::lock_guard lock(mutex);
        if(identity==address && reference==control)return;
        value=observed;identity=address;reference=control;
    }
    Strong Lock(uintptr_t expected) const {
        Strong live{};
        {
            std::lock_guard lock(mutex);
            if(!expected || identity!=expected)return {};
            live=value.Lock();
        }
        // Destruction of a last strong reference must happen outside our lock.
        if(!live || reinterpret_cast<uintptr_t>(live.GetPtr())!=expected)return {};
        return live;
    }
    void Clear(){std::lock_guard lock(mutex);value=Weak{};identity=reference=0;}
};
}
