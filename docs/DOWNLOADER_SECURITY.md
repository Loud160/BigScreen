# Downloader update security

Big Screen checks the official `yt-dlp/yt-dlp` GitHub release channel over
HTTPS. Stable is the default; nightly is opt-in and clearly warned as higher
risk. The release package is checked against the SHA-256 list published with
that same release, inspected as an archive, checked for the expected module,
and staged without replacing the working package.

On the next initialization, the candidate is imported inside the bundled
CPython runtime and its public `YoutubeDL` entry point is checked. Only then is
it accepted. If import or internal startup fails, Big Screen marks the candidate
as rejected, restores the one retained previous version (or shipped baseline),
and retries a download once. Ordinary network, private-video, login, parental
control, and content errors do not cause rollback.

SHA-256 verification is integrity checking, not a digital signature and not a
guarantee that upstream code is harmless. The updater deliberately limits its
authority to the official upstream release artifact, keeps a shipped fallback,
tests compatibility before use, and does not update CPython itself.
