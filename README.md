![Bleb](http://i.imgur.com/5hycZj5.png)

### Hierarchical Object Repository
---
>In medicine, a bleb is a large blister (usually approximately hemispherical) filled with serous fluid.
>&mdash; Wikipedia

## Goals

### Primary Goals
- store data of different sizes and shapes efficiently
- reference data objects by a given name
- fast read access (up to the point of direct memory mapping)
- allow external storage of data objects
- implementation: good memory allocation efficiency (consider 3-level embedding)
- scale well from tiny to huge repositories

### Optional Goals
- built-in encryption/compression/filtering support
- advanced security (signing)

### Usage cases
- as a virtual file system (read-only or read/write)
- as an efficient key-value storage
- as a universal rich container format (see: Zombie Media File)

## Logical structure
TODO

## On-disk format
```
Header
    char magic[4] = 0x89 'ble'
    uint32_t formatVersion

    (Content Directory Stream Descriptor)
    uint64_t location   (offset in file)
    uint64_t size

Directory Stream
    (Prologue)
    uint16_t flags (1=has storage descriptor)
    uint32_t numObjects

    (Storage Descriptor - specifies external storage of object payload)

    Object Entries
        (Prologue - padded to multiple of 16 bytes)
        uint16_t flags (1=is a directory, 2=has stream descriptor, 4=has storage descriptor, 8=has md5 hash, 16=has inline payload, 32=is text)
        uint16_t+uint8_t[] name     (empty = deleted entry ... TODO)

        (Stream Descriptor - specifies the stream containing object payload - 16 bytes)
        uint64_t location   (offset in file)
        uint64_t size

        (Storage Descriptor - specifies external storage of object payload)

        (MD5 hash - 16 bytes)
        uint8_t[16] md5

        (Inline Payload - embeds object payload in directory - padded to multiple of 16 bytes)
        uint8_t length
        uint8_t[] payload

Stream encoding
    (Span Header)
    uint32_t reservedLength
    uint32_t usedLength
    uint64_t nextSpanLocation
    ubyte[reservedLength] data

    <jump to nextSpanLocation if != 0>
    <repeat>
```

## Example repository
### sample.bleb
```
Content Directory
|-- Metadata (entries typically use inline payloads)
|   |-- authored_using  => "Example Application v1.0"
|   |-- original_author => "Company Employee <employee@company.com>"
|   '-- source_file     => "house.blend"
|-- Model
|   |-- vertex_data     => binary blob
|   '-- material        => cfx2
'-- Textures
    |-- 12345...bcdef   => jpg image
    '-- 23456...cdef0   => png image

System Directory
'-- Index of 'Content Directory'/'Textures' => B-tree index for faster searching in directory
```

### Assorted implementation ideas
- fully load small objects (< 4k? < 64k? use a pool?) when opened as a stream
- align big objects on 4k boundary
- allocation strategy:
    - depending on expected data size, reserve first 256..4k+ for small objects
    - solves: attempt to place metadata (and related directory entries in first 4k)
