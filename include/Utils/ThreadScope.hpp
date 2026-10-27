#pragma once

namespace cvr {
// Bind caller-owned data only for synchronous native callbacks on this thread.
// Even an inactive nested call masks the outer scope until it returns.
template<class T> class ThreadScope {
    inline static thread_local T* current{};
    T* previous;
public:
    explicit ThreadScope(T* value):previous(current){current=value;}
    ~ThreadScope(){current=previous;}
    ThreadScope(const ThreadScope&)=delete;
    ThreadScope& operator=(const ThreadScope&)=delete;
    static T* Get(){return current;}
};
}
