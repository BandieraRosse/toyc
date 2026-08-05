# v0.1 binary formats

All integers are little-endian. Header bytes are written field-by-field; no C struct
is serialized. `version` is currently 1 and unknown versions are rejected.

| format | fixed header | fields | payload |
|---|---:|---|---|
| TTEX | 32 bytes | magic `TTEX`; version u16; header_size u16; width/height u32; channels u16 (=3); format u16 (=RGB888); data_offset/data_size/reserved u32 | tightly packed RGB bytes |
| TSND | 32 bytes | magic `TSND`; version u16; header_size u16; rate u32; channels u16 (1/2); bits u16 (=16); frames/data_offset/data_size/reserved u32 | interleaved signed PCM16 little-endian |
| TMES | 40 bytes | magic `TMES`; version u16; header_size u16; vertex_count/index_count/vertex_stride/index_stride/vertices_offset/indices_offset/reserved0/reserved1 u32 | vertex `{x,y,z,color}` as four explicit u32 fields, then u32 triangle indices |

Runtime limits are 8192×8192 for textures, 64 MiB per file, 1,048,576 vertices,
3,145,728 indices, and every mesh index must be less than `vertex_count`.
