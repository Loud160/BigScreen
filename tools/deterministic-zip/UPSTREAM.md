# miniz source provenance

The files under `vendor/miniz-3.1.2/` are an unmodified source snapshot from
the official miniz 3.1.2 release tag at commit
`77d0dce8627735138c51770d1799a1ef48f2117d`:

<https://github.com/richgel999/miniz/releases/tag/3.1.2>

Big Screen compiles `miniz.c`, `miniz_tdef.c`, and `miniz_tinfl.c` only into a
small host-side build utility. The utility creates deterministic standard
ZIP/DEFLATE archives; it is not included in the Quest QMOD. Upstream's MIT
license is preserved verbatim in `vendor/miniz-3.1.2/LICENSE`.
