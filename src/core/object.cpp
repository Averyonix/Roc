#include "object.hpp"

#include "memory.hpp"

MemRegistry::MemRegistry()
{
    debug_assert(!mem_registry);

    slot_count = 1 << 20;

    data       = memory_map<          void*>(slot_count);
    size       = memory_map<            usz>(slot_count);
    version    = memory_map<     MemVersion>(slot_count);
    ref_count  = memory_map<            u32>(slot_count);
    unregister = memory_map<MemUnregisterFn>(slot_count);

    mem_registry = this;
}

MemRegistry::~MemRegistry()
{
    debug_assert(mem_registry == this);

    memory_unmap(data,       slot_count);
    memory_unmap(size,       slot_count);
    memory_unmap(version,    slot_count);
    memory_unmap(ref_count,  slot_count);
    memory_unmap(unregister, slot_count);

    mem_registry = nullptr;
}

MemRegistry* mem_registry;
