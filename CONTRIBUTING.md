# Contributing to Big Screen

Big Screen is distributed outbound under **GPL-3.0-only**, with additional
terms and an interoperability permission under GPLv3 section 7. See
[LICENSE](LICENSE) and
[LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md).

## Inbound MIT license grant

By intentionally submitting a contribution to Big Screen, you license that
contribution to **Loud160 (AKA Whisp)**, the Big Screen project, and its
maintainer under the [MIT License for inbound contributions](INBOUND_LICENSE.md)
in addition to any license applicable to the distributed Big Screen project.

This separate inbound MIT grant permits the maintainer to use, copy, modify,
merge, publish, distribute, sublicense, relicense, dual-license, sell, grant
exceptions for, and otherwise exercise the rights granted by the MIT License
over the submitted contribution. Big Screen's outbound GPL-3.0-only plus
section 7 licensing does not restrict the maintainer's separate rights received
from contributors under this inbound MIT grant. When exercising that separate
inbound MIT license, the maintainer is not required to apply Big Screen's GPLv3
section 7 attribution requirements to the maintainer's independent use of the
contributor material.

This is a license grant, not a copyright assignment. Contributors retain any
copyright ownership they otherwise hold. By opening or submitting a pull
request, the contributor acknowledges these inbound contribution terms.

## Developer Certificate of Origin 1.1

Every contribution must also be certified under the
[Developer Certificate of Origin 1.1](DCO.txt). Sign each commit with:

```text
git commit -s
```

which adds:

```text
Signed-off-by: Contributor Name <email@example.com>
```

The DCO sign-off certifies provenance and the contributor's right to submit the
change. It does **not** create or grant the inbound MIT license. The inbound MIT
grant above is a separate condition of intentional submission.

## Engineering expectations

Keep changes narrow, commented, and compatible with the documented Beat Saber
build. Open an issue before changing persistence formats, downloader trust
boundaries, thread ownership, or mapper compatibility. Keep Unity/Beat Saber
access on the main thread and file, network, and decode work on background
workers. Do not delete or move user-owned map-folder or Video Import files.
Preserve Beat Saber's song clock as the sole playback authority.

Before submitting a change, run host tests, complete a clean Quest build,
explain new network or storage behavior, update user-facing text and
documentation, and report what was and was not tested on-headset. Do not commit
generated game headers, copyrighted media, Beat Saber files, credentials,
cookies, or private headset logs.
