# Contributing

Keep Unity/Beat Saber access on the main thread and file/network/decode work on
background workers. Do not delete or move user-owned map-folder or Video Import
files. Preserve Beat Saber's song clock as the sole playback authority.

Before submitting a change, run the host core tests and the Quest build. Explain
why any new hook is lifecycle-safe, document persistent-setting migration, and
add comments for ownership boundaries or non-obvious algorithms. Update the
failed-approach notes when live testing disproves an implementation strategy so
future contributors do not repeat it.
