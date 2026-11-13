# simdjson 3.12.3
Single-header distribution from https://github.com/simdjson/simdjson/tree/v3.12.3/singleheader.
Runtime CPU dispatch selects a supported SIMD implementation; portable fallback is
included. The engine parses with simdjson and converts the DOM directly to its
existing editable Json representation. nlohmann/json is retained for writing,
schema access and editing, not for parsing input. See LICENSE.
