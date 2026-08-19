# Downloader update security

Big Screen checks the official `yt-dlp/yt-dlp` and, when applicable,
`yt-dlp/yt-dlp-nightly-builds` GitHub release channels over HTTPS. Stable
remains the normal update channel and nightly updates are opt-in and clearly
warned as higher risk. A once-per-session background check follows the package
actually loaded: stable checks stable only, while nightly checks stable first
and checks nightly only when stable has not caught up. Automatic checks never
move a stable installation to nightly. The QMOD temporarily ships pinned official
nightly 2026.08.18.122307 because upstream identified nightly as the fix for
stable 2026.07.04's Android-VR HTTP 403 regression. An explicit return from
nightly to stable is treated as a channel switch rather than an accidental
downgrade. The release package is checked against the SHA-256 list published with
that same release, inspected as an archive, checked for the expected module,
and staged without replacing the working package.

On the next initialization, the candidate is imported inside the bundled
CPython runtime, its separately versioned `yt_dlp_ejs` package is verified,
and its public `YoutubeDL` entry point is checked. Big Screen loads and parses
both actual EJS solver payloads through its compiled QuickJS-NG engine, then
checks custom provider registration. Only then is the candidate accepted. This
catches missing solver data and yt-dlp provider-API changes before a user starts
a download. Candidate promotion is a filesystem transaction: any unrelated
initialization failure restores the original active package and leaves the
candidate staged for a later retry. If the compatibility test itself fails,
Big Screen marks the candidate as rejected, restores the one retained previous
version (or shipped baseline), and retries a download once.
Ordinary network, private-video, login, parental control, and content errors do
not cause rollback.

Download, metadata-probe, and updater workers all use a cancellation marker and
finite network timeouts. Cancellation is checked between phases and between
streamed updater chunks; a network call already inside the platform TLS stack
returns at its timeout before the worker joins. C++ terminal failures remove a
stale progress record before publishing failure, preventing an old active state
from blocking all future work.

Metadata probing reports only compatible exact resolution tiers. A selected
tier is pinned into the job rather than delegated to yt-dlp's changing `best`
policy: H.264 is required through 1080p and VP9 at 1440p. Both probing and
transfer explicitly exclude the `android_vr` client. Current yt-dlp may deliver
the selected H.264 tier through fragmented HLS, whose concatenated MPEG-TS
payload is supported directly by Big Screen's private FFmpeg runtimes. A replacement
downloads to a sibling staging file and is atomically promoted only after the
transfer completes; the prior assignment is restored if publication or the
manifest commit fails.

The shipped baseline is also reproducible from pinned yt-dlp and yt-dlp-ejs
source archives. The source-build recipe checks both archives, rebuilds the EJS
payload using the upstream lockfile, verifies the generated solver hashes, and
compares every packaged byte with the pinned official release.

QuickJS-NG is compiled into `libbigscreen.so`; it is not downloaded or executed
from writable storage. Each challenge receives a new runtime with a 128 MiB
memory ceiling, 512 KiB JavaScript stack, 16 MiB input ceiling, 8 MiB captured-
output ceiling, and a 30-second interrupt deadline. The runtime exposes a
captured console but no direct file, process, or network APIs. C++ exceptions
are contained before crossing QuickJS C callbacks. yt-dlp downloads the player
data itself and passes only the solver script into this engine.

Thumbnail retrieval derives the image URL from yt-dlp's validated YouTube video
ID and pins it to `https://i.ytimg.com`. It writes through a temporary file and
does not discard an existing thumbnail when a transient refresh fails.

SHA-256 verification is integrity checking, not a digital signature and not a
guarantee that upstream code is harmless. The updater deliberately limits its
authority to the official upstream release artifact, keeps a shipped fallback,
caps a downloaded update at 32 MiB, tests compatibility before use, and does
not update CPython or QuickJS-NG itself. Updating either native runtime requires
a new reviewed Big Screen QMOD.
