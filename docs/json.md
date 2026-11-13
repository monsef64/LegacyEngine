# JSON parsing
Project/settings JSON is parsed with **simdjson 3.12.3**, which selects an available
SIMD implementation at runtime and has a portable fallback. The parsed DOM is
converted directly to the existing editable Json structure; there is no second
text parse. nlohmann/json is retained for schema access/editing and serialization.
Existing JSON project files remain readable. Size, nesting, schema and light/input
bounds are still validated. End-to-end loading also includes DOM conversion and
project validation; no measured speedup is claimed without profiling.
