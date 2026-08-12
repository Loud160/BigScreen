# Downloader update security

Big Screen checks the official `yt-dlp/yt-dlp` GitHub release channel over
HTTPS. Stable is the default; nightly is opt-in and clearly warned as higher
risk. The release package is checked against the SHA-256 list published with
that same release, inspected as an archive, checked for the expected module,
and staged without replacing the working package.

On the next initialization, the candidate is imported inside the bundled
CPython runtime, its separately versioned `yt_dlp_ejs` package is verified,
and its public `YoutubeDL` entry point is checked. Big Screen also imports its
custom JavaScript challenge provider and executes a real JSON round-trip
through the compiled QuickJS-NG engine. Only then is the candidate
accepted. This catches yt-dlp changes to the provider API before a user starts
a download. If import, provider registration, native evaluation, or internal
startup fails, Big Screen marks the candidate as rejected, restores the one
retained previous version (or shipped baseline), and retries a download once.
Ordinary network, private-video, login, parental control, and content errors do
not cause rollback.

The shipped baseline is also reproducible from pinned yt-dlp and yt-dlp-ejs
source archives. The source-build recipe checks both archives, rebuilds the EJS
payload using the upstream lockfile, verifies the generated solver hashes, and
compares every packaged byte with the pinned official release.

QuickJS-NG is compiled into `libbigscreen.so`; it is not downloaded or executed
from writable storage. Each challenge receives a new runtime with a 128 MiB
memory ceiling and a 30-second interrupt deadline. The runtime exposes a
captured console but no direct file, process, or network APIs. yt-dlp downloads
the player data itself and passes only the solver script into this engine.

SHA-256 verification is integrity checking, not a digital signature and not a
guarantee that upstream code is harmless. The updater deliberately limits its
authority to the official upstream release artifact, keeps a shipped fallback,
tests compatibility before use, and does not update CPython or QuickJS-NG
itself. Updating either native runtime requires a new reviewed Big Screen QMOD.
