# Mapper video metadata

Big Screen reads Cinema-compatible video metadata as an interoperability input.
A map may provide a local MP4 filename or a downloadable video URL plus timing
and screen placement fields. User assignments take precedence without changing
the map, and removing a user assignment reveals the mapper's video again.

Local mapper video files must use an MP4 container with H.264/AVC video at
1920x1080 or lower. Big Screen rejects absolute paths and nested/traversal paths;
the configured filename must resolve directly inside the map directory.

Downloaded mapper videos are stored separately from user overrides so replacing
or removing one cannot destroy the other. Mapper files placed in the map folder
are user/map-owned and are never deleted by Big Screen.
