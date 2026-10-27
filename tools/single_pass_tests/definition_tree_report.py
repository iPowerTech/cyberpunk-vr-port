"""Decode a native child-tree observation, never certify its destructor effects."""
import struct
from collections import Counter

NO_INDEX = 0xffffffff


def decode(stats, blocks, image_base, source_entries):
    if len(stats) != 12 or stats[0] != 2 or stats[9] or stats[4] > 8192 or stats[5] > 4096 or stats[6] > 8192 or stats[7] > 4096 or stats[8] != 2:
        raise ValueError('Incomplete or unbounded native child-tree receipt')
    layouts = dict(Roots=('<Q2I', stats[4]), Nodes=('<5Q6I', stats[5]),
                   Handles=('<3Q4I2i', stats[6]), Arrays=('<5Q4I', stats[7]), Allocators=('<3Q2I', stats[8]),
                   Strings=('<10Q32s4I', stats[10]))
    rows = {}
    for name, (layout, count) in layouts.items():
        raw=blocks.get(name,b'')
        if len(raw) != count * struct.calcsize(layout):
            raise ValueError('Native child-tree binary ABI/count mismatch')
        rows[name] = list(struct.iter_unpack(layout, raw))
    entries = [v[0] for v in struct.iter_unpack('<Q', source_entries)]
    if len(entries) != stats[4]:
        raise ValueError('Child tree has a different unique-owner slot inventory')
    nodes, objects, types = [], set(), Counter()
    for index, row in enumerate(rows['Nodes']):
        obj, vtable, destructor, work, control, kind, array, first, count, depth, reserved = row
        if not obj or obj in objects or not vtable or not destructor or not work or kind > 4 or reserved or depth > 64:
            raise ValueError('Invalid child-tree object identity')
        if array != NO_INDEX and array >= len(rows['Arrays']):
            raise ValueError('Child-tree node has an invalid owned-array index')
        if (count and (first == NO_INDEX or first + count > len(rows['Handles']))) or (not count and first != NO_INDEX and first > len(rows['Handles'])):
            raise ValueError('Child-tree node has an invalid handle range')
        objects.add(obj); types[(vtable - image_base, destructor - image_base, work - image_base, kind)] += 1
        nodes.append(dict(index=index, object=hex(obj), vtableRva=hex(vtable-image_base),
                          destructorRva=hex(destructor-image_base), workRva=hex(work-image_base),
                          controlBlock=hex(control), kind=kind, depth=depth, array=array,
                          firstHandle=first, handleCount=count))
    for slot, (obj, recorded_slot, node) in enumerate(rows['Roots']):
        if obj != entries[slot] or recorded_slot != slot or (not obj and node != NO_INDEX) or (obj and (node >= len(nodes) or int(nodes[node]['object'], 16) != obj)):
            raise ValueError('Child tree root differs from its native owner entry')
    handles = []
    for address, obj, control, parent, child, slot, reserved, strong, weak in rows['Handles']:
        if parent >= len(nodes) or reserved or (control and (strong <= 0 or weak <= 0)):
            raise ValueError('Invalid native shared child handle')
        if obj and (child >= len(nodes) or int(nodes[child]['object'], 16) != obj or int(nodes[child]['controlBlock'], 16) != control):
            raise ValueError('Child handle has no exact shared object/control pair')
        if not obj and child != NO_INDEX:
            raise ValueError('Null child handle claimed an object')
        handles.append(dict(address=hex(address), object=hex(obj), controlBlock=hex(control),
                            parent=parent, child=child, slot=slot, strong=strong, weak=weak))
    arrays = []
    for header, data, trailer, allocator, callback, capacity, count, stride, node in rows['Arrays']:
        if node >= len(nodes) or count > capacity or stride not in (16, 48) or not data or not allocator or not callback:
            raise ValueError('Invalid native owned child array')
        expected_offset, expected_stride = {1:(0x28,16),2:(0x30,48),3:(0x20,48)}.get(nodes[node]['kind'],(0,0))
        if header != int(nodes[node]['object'], 16) + expected_offset or stride != expected_stride:
            raise ValueError('Child array has a different native layout')
        arrays.append(dict(header=hex(header), data=hex(data), trailer=hex(trailer), allocator=hex(allocator),
                           freeCallbackRva=hex(callback-image_base), capacity=capacity, count=count, stride=stride, node=node))
    allocators = []
    for address, raw, allocator, kind, reserved in rows['Allocators']:
        if not allocator or allocator != raw & ~7 or kind > 1 or reserved:
            raise ValueError('Invalid native allocator identity')
        allocators.append(dict(address=hex(address), raw=hex(raw), allocator=hex(allocator), kind=kind))
    strings=[]
    for row in rows['Strings']:
        storage,data,size,slot,allocator,callback,fallback_slot,fallback_raw,fallback_allocator,adapter,raw,node,mode,length,capacity=row
        if node>=len(nodes) or nodes[node]['kind']!=4 or storage!=int(nodes[node]['object'],16)+0x20 or mode>2:
            raise ValueError('Invalid native string storage identity')
        strings.append(dict(storage=hex(storage),data=hex(data),allocationBytes=size,allocatorSlot=hex(slot),allocator=hex(allocator),
                            freeCallbackRva=hex(callback-image_base) if callback else None,fallbackGlobal=hex(fallback_slot),
                            fallbackRaw=hex(fallback_raw),fallbackAllocator=hex(fallback_allocator),
                            fallbackAdapterRva=hex(adapter-image_base) if adapter else None,raw=raw.hex(),node=node,mode=mode,length=length,capacity=capacity))
    return dict(consumerId=stats[3], epoch=stats[1], pairGeneration=stats[2], nodes=nodes, handles=handles,
                arrays=arrays, allocators=allocators,strings=strings,
                types=[dict(vtableRva=hex(v), destructorRva=hex(d), workRva=hex(w), kind=k, objects=n)
                       for (v, d, w, k), n in sorted(types.items())],
                destructorSafetyProved=False, nativeOwnershipProved=False)
