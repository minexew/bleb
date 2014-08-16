![Bleb](http://i.imgur.com/5hycZj5.png)

### Structured Object Repository
---
>In medicine, a bleb is a large blister (usually approximately hemispherical) filled with serous fluid.
>&mdash; Wikipedia

(not to be confused with any [similarly sounding words](http://www.urbandictionary.com/define.php?term=pleb))

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
[Work in progress](doc/ondisk.txt)

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
```

### Assorted implementation ideas
- fully load small objects (< 4k? < 64k? use a pool?) when opened as a stream
- allocation strategy:
    - depending on expected data size, reserve first 256..4k+ for small objects
    - solves: attempt to place metadata (and related directory entries in first 4k)
