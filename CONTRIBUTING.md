# Contributing

Keep changes narrow, commented, and compatible with the documented Beat Saber build. Open an issue before changing persistence formats, downloader trust boundaries, thread ownership, or mapper compatibility. Keep Unity/Beat Saber access on the main thread and file/network/decode work on background workers. Do not delete or move user-owned map-folder or Video Import files. Preserve Beat Saber's song clock as the sole playback authority.

Before submitting a change, run host tests, complete a clean Quest build, explain any new network/storage behavior, update user-facing hover text and documentation, and report what was and was not tested on-headset. Preserve existing library/settings migration behavior. Explain why new hooks are lifecycle-safe, and update failed-approach comments when live testing disproves a strategy so it is not retried later. Never solve an error by catching it silently without a log or safe user-facing path.

Generated game API headers and downloaded build artifacts should not be committed unless the repository explicitly tracks them. Do not include copyrighted videos, Beat Saber game files, credentials, cookies, or headset logs containing private data.
