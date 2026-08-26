# In-process WebPO/BotGuard token-minter plan

Status: deferred planning only; no implementation is present.

Last reviewed: August 26, 2026

## Purpose

This document records what would be required to add a minimal, self-contained
Web Proof-of-Origin (WebPO) token provider to Big Screen. The intended design
would reuse the CPython and QuickJS-NG runtimes already packaged with the mod
instead of requiring a browser, Node.js, Deno, Docker, a companion application,
or a remote token service.

The immediate motivation is YouTube's intermittent **Sign in to confirm you're
not a bot** response. That response can affect public, unrestricted videos when
YouTube challenges a guest session or public IP address. Big Screen currently
classifies some of those responses as if the individual video required a
YouTube account, which is not always accurate.

A PO token can help satisfy the client-attestation requirements associated
with some YouTube clients. It is not a guaranteed bypass for every HTTP 403,
account restriction, regional restriction, or IP-level bot challenge.

## Recommended scope

Build a **minimal guest-session WebPO token minter**, not a general browser.

The first supported configuration should be:

- yt-dlp's `mweb` client;
- GVS video-download tokens;
- public videos without account cookies;
- one token request associated with one yt-dlp extraction/download job;
- Big Screen's existing downloader worker, cancellation, timeout, status,
  logging, and rollback infrastructure;
- the current non-token downloader route as a safe fallback.

Do not include account login, cookie extraction, Meta Quest Browser integration,
arbitrary web browsing, browser UI, or a remotely hosted token service in the
first implementation.

## Why a normal embedded browser is the wrong solution

A Python HTTP client and HTML parser are not enough to mint WebPO tokens.
BotGuard/WebPO relies on JavaScript attestation behavior and session-bound
challenge data. Recreating an entire browser would introduce a very large
HTML, DOM, JavaScript, networking, storage, cookie, and security surface that
Big Screen does not need.

Reusing the installed Meta Quest Browser is also not a viable supported design.
Android application sandboxing prevents one application from reading another
application's private browser cookies or session storage, and the browser does
not expose a supported automation/token-minting API to a Beat Saber mod.

The narrower solution is to reproduce only the request/attestation sequence
needed by yt-dlp:

```text
yt-dlp YouTube extractor
    -> Big Screen PoTokenProvider plug-in
    -> Python obtains guest session and challenge data
    -> QuickJS executes the bounded BotGuard/WebPO calculation
    -> Python submits the attestation result
    -> provider returns a base64url PO token
    -> yt-dlp uses the token with the matching mweb request
```

## Existing Big Screen pieces that can be reused

Big Screen already has most of the hosting and safety infrastructure needed
around the new component:

- CPython runs yt-dlp in-process on a dedicated downloader worker.
- `python/bigscreen_jsc_provider.py` registers Big Screen's current JavaScript
  challenge provider.
- `src/QuickJsPythonModule.cpp` exposes the compiled QuickJS engine to Python as
  `bigscreen_quickjs`.
- `src/QuickJsEngine.cpp` creates a fresh QuickJS runtime for each evaluation.
- The current QuickJS sandbox has no file, process, or network APIs and applies
  memory, stack, source-size, output-size, and execution-time limits.
- yt-dlp candidates are staged, imported, compatibility-tested, promoted, and
  rolled back transactionally.
- Downloader work already has cancellation, progress mailboxes, finite network
  timeouts, UI-safe state publication, and sensitive-log sanitization.

The existing JavaScript challenge provider and the proposed PO-token provider
solve different problems. They should remain separate modules with separate
compatibility tests even though both use QuickJS.

## Components to add

### 1. A first-party yt-dlp PO-token provider

Add a Python module such as:

```text
python/bigscreen_pot_provider.py
```

It should register through yt-dlp's public
`yt_dlp.extractor.youtube.pot.provider` API and implement a provider class whose
name ends in `PTP`, as required by yt-dlp.

The provider should initially advertise only the client and context that Big
Screen has actually implemented and tested:

```text
client: MWEB
context: GVS
authenticated requests: unsupported
```

Its `is_available()` implementation must be cheap, deterministic, and must not
perform network work. Actual token generation belongs in `_real_request_pot()`.

The provider should return yt-dlp's `PoTokenResponse`, not manually insert a
token into media URLs. This preserves yt-dlp's request binding and built-in
WebPO memory-cache behavior.

### 2. A narrow Python-to-QuickJS token-minter bridge

The current `bigscreen_quickjs.execute(source)` API accepts one script and
returns captured output. That is appropriate only if an entire WebPO
calculation can be assembled into one bounded evaluation.

If the selected WebPO implementation needs multiple host callbacks, add a
separate, purpose-built API rather than expanding the generic evaluator into a
browser. For example:

```text
bigscreen_quickjs.mint_webpo(request_json) -> response_json
```

The input and output should use a versioned, size-limited JSON contract. The
native bridge should validate required fields before invoking QuickJS and
should return structured failure categories such as:

- invalid input;
- unsupported client or token context;
- challenge fetch failed;
- JavaScript exception;
- timeout;
- memory limit;
- malformed attestation result;
- token submission rejected;
- cancellation.

Do not expose general filesystem, process-launching, socket, or HTTP APIs to
JavaScript. Python should remain responsible for all HTTPS traffic.

### 3. Minimal BotGuard/WebPO JavaScript support

The JavaScript side needs only the pieces required to execute the selected
guest-session WebPO flow. Depending on the upstream implementation selected
for reference or reuse, this may require narrowly scoped shims for:

- byte arrays and base64url conversion;
- text encoding and decoding;
- cryptographic primitives used by the attestation flow;
- timing or entropy sources;
- the specific browser-like globals examined by the challenge;
- message exchange between the Python orchestrator and the attestation code.

Each shim should be justified by an observed requirement. Do not build a
general DOM, navigation model, persistent cookie jar, local storage, canvas,
or browser extension system.

YouTube can change the downloaded challenge and the environment it expects.
The implementation therefore needs explicit size and execution limits plus a
clean failure path when an unknown challenge is encountered.

### 4. Python-owned network orchestration

The provider should use yt-dlp's own request helper where practical so it
inherits the extractor's headers, proxy behavior, source address, TLS policy,
and cancellation expectations.

The Python orchestration layer must keep these values consistent across:

1. guest visitor/session creation;
2. BotGuard challenge retrieval;
3. attestation submission;
4. PO-token return;
5. the yt-dlp request that consumes the token.

A token may be bound to visitor/session data or to an individual video. It
must not be reused with a different visitor identity, content binding, client,
or network context.

Network destinations should be restricted to the exact HTTPS YouTube/Google
endpoints required by the selected flow. Redirects must still be validated;
TLS verification must remain enabled; response bodies must have strict size
limits; and every request must have a finite timeout.

### 5. Client selection and fallback policy

The provider should not replace Big Screen's working downloader path
unconditionally.

Recommended order:

1. If the WebPO provider passes its startup compatibility check, try the
   verified `mweb` plus GVS-token route.
2. If the provider is unavailable, rejects the request, times out, or produces
   an invalid token, record the reason and fall back once to Big Screen's
   existing `default,-android_vr` client policy.
3. Do not loop repeatedly between client policies.
4. If both routes fail, present the most accurate terminal error and preserve
   the detailed sanitized diagnostics for support.

Fallback must remain inside the same logical download operation so a failed
token attempt cannot leave stale status, partial media, an incorrect map
assignment, or a second simultaneous yt-dlp worker.

The fallback policy should be independently controllable by an internal
development flag while the feature is being tested. It should not initially
be exposed as an ordinary user setting.

### 6. Token caching

Use yt-dlp's built-in WebPO memory cache unless testing proves a separate cache
is necessary. Persistent token storage should not be added initially.

If Big Screen later adds its own cache, its key must include every binding that
can change token validity, including at minimum:

- token context;
- Innertube client;
- visitor/session identity;
- content binding or video ID;
- provider implementation/version.

The cache must respect yt-dlp's `bypass_cache` request and token expiration.
Entries must be invalidated after a token rejection, visitor/session change,
provider update, channel switch, or Beat Saber restart unless persistence has
been explicitly proven safe.

### 7. User-visible progress and cancellation

Token generation can involve several network requests and a nontrivial
JavaScript calculation. It must run on the existing downloader worker, never
on Unity's main thread.

Publish concise phases through the current download status system, for example:

```text
Preparing secure YouTube request
Verifying YouTube session
Requesting video stream
```

Do not expose BotGuard jargon in normal UI. Detailed phase information belongs
in sanitized diagnostic logs.

Cancellation should be checked between every network and JavaScript stage.
QuickJS's interrupt callback must also observe the active download's
cancellation state rather than waiting for the full timeout.

## Error handling changes

The current broad sign-in classifier should be split before or alongside this
work. At minimum, distinguish:

- the video genuinely requires an account;
- the video is private, removed, region-blocked, or age-restricted;
- YouTube requested bot verification for the guest session or public IP;
- a PO token was requested but the provider was unavailable;
- PO-token generation failed;
- YouTube rejected the generated token;
- the ordinary fallback route also failed.

A dedicated support code such as `BS-DL-YOUTUBE-BOT-CHECK` should explain that
the video may still be public and that repeated retries may make the challenge
last longer. The message can suggest waiting, changing networks, checking
yt-dlp updates, or assigning a local video. It should not claim that signing
into Meta Quest Browser will sign Big Screen into YouTube.

Provider failures should use their own stable support code and should not be
misreported as a removed or account-only video.

## Security and privacy requirements

This feature processes untrusted, remotely supplied challenge data. Treat it
as a security-sensitive downloader subsystem.

Required boundaries:

- Keep the QuickJS runtime process-isolated by capability: no filesystem,
  process, socket, Android JNI, Unity, or arbitrary native-object access.
- Retain a fresh runtime per token operation unless a future persistent-runtime
  design receives a separate security and lifecycle review.
- Keep strict memory, stack, source, output, and wall-time limits.
- Add response-size and redirect limits to every new HTTP step.
- Do not support YouTube username/password entry.
- Do not import or extract cookies from Meta Quest Browser.
- Do not write visitor data, session identifiers, tokens, attestation blobs,
  signed URLs, cookies, or authorization headers to logs.
- Extend `DiagnosticSessionLogger` sanitization tests for every new field name
  and serialized representation.
- Never send session or token material to a third-party token service.
- Clear transient token/challenge buffers after the download job ends.
- Keep Unity interaction on the main thread; the provider must communicate
  only through the existing worker mailbox/status mechanisms.

Because YouTube changes this protocol, an incompatible challenge must fail
closed and fall back. It must never disable QuickJS limits or add broad native
capabilities merely to make one challenge pass.

## Packaging, updating, and licensing

The first-party provider module and JavaScript resources would need to be
packaged in the QMOD's embedded downloader runtime. The package manifest,
runtime hash manifest, reproducible downloader build, third-party notices, and
repository invariant tests must all be updated.

The provider's API compatibility is coupled to yt-dlp. Big Screen's existing
candidate activation test should therefore verify, before accepting a stable
or nightly yt-dlp update:

- all required public PO-provider imports;
- provider registration and discovery;
- supported-client and supported-context declarations;
- construction of a mocked `PoTokenRequest`;
- rejection of unsupported authenticated/client/context requests;
- parsing of a mocked valid `PoTokenResponse`;
- continued operation of the existing EJS JavaScript challenge provider.

Do not copy an upstream BotGuard/WebPO implementation into Big Screen without
an exact provenance and license audit. The featured BgUtils provider is a
useful behavioral reference, but it normally depends on a Node/Deno service
and carries its own GPL terms and transitive dependencies. If its code or
another implementation is reused, preserve all required copyright and license
notices and review every bundled dependency. If only behavior is studied and a
first-party implementation is written, document that provenance clearly.

Updater rollback needs to cover the combined yt-dlp/provider compatibility
boundary. A new yt-dlp package that changes the public provider API must be
rejected before it becomes active; a transient online token-minting failure is
not evidence that the package itself is incompatible and must not trigger an
automatic downloader rollback.

## Proposed implementation sequence

### Phase 0: Freeze evidence and make failures accurate

1. Save reproducible examples of public videos receiving the bot-check error.
2. Preserve the raw sanitized yt-dlp diagnostics for those attempts.
3. Split bot-check classification from genuine account-required failures.
4. Add unit tests proving that a previous failed video cannot poison the next
   download's error state.

This phase improves support quality even if the token minter is never shipped.

### Phase 1: Host-side protocol proof

1. Use a pinned upstream provider externally on a development PC only to
   establish the expected request, token context, client, and successful
   yt-dlp behavior.
2. Capture sanitized protocol fixtures that contain no reusable credentials,
   visitor IDs, tokens, or signed media URLs.
3. Identify the smallest JavaScript/runtime surface actually required.
4. Record upstream versions, commit hashes, licenses, and expected outputs.

The external provider is a comparison oracle for development. It is not part
of the shipping design.

### Phase 2: Provider plug-in with a mocked minter

1. Add `bigscreen_pot_provider.py` against yt-dlp's public provider API.
2. Validate mweb/GVS guest requests and reject everything outside the initial
   scope.
3. Return deterministic fake tokens only in host tests.
4. Verify provider preference, cache bypass, cancellation, and fallback logic.
5. Extend stable/nightly compatibility tests before changing live downloads.

### Phase 3: In-process token-minter prototype

1. Add a versioned Python/native request contract.
2. Port only the required BotGuard/WebPO calculation and shims to QuickJS.
3. Keep HTTP in Python and pass only bounded structured inputs to QuickJS.
4. Enforce cancellation and resource limits at every stage.
5. Compare generated tokens and request behavior against the Phase 1 oracle.
6. Confirm that no token or session material reaches logs or disk.

### Phase 4: Downloader integration

1. Enable `mweb` only when the provider passes startup validation.
2. Feed the token through yt-dlp's provider response path.
3. Add one bounded fallback to the current client policy.
4. Integrate status messages, cancellation, partial-file cleanup, replacement
   rollback, resolution probing, exact-format selection, and MP4 remuxing.
5. Keep the feature behind an internal development switch until Quest tests
   demonstrate that it improves real failures without regressing ordinary
   downloads.

### Phase 5: Quest validation and hardening

Test first on Quest 2, then Quest 3/3S, using the same QMOD packaging path as a
release install. Exercise:

- public videos that previously triggered bot verification;
- ordinary public videos that already work without a token;
- 480p, 720p, 1080p, and 1440p format probing and downloads;
- H.264 direct MP4 and HLS/MPEG-TS-to-MP4 preparation;
- VP9/WebM where supported;
- stable and nightly yt-dlp packages;
- repeated downloads in one game session;
- cancellation during each token stage;
- Wi-Fi loss, reconnect, public-IP change, and headset sleep/resume;
- switching songs while a download is active;
- a provider timeout followed by successful ordinary fallback;
- a genuine private, removed, age-restricted, or region-blocked video;
- menu exit, Beat Saber shutdown, and a hard restart after interrupted work;
- support-log collection and secret-redaction verification.

Token work should occur only while probing or downloading. Verify that it adds
no playback, decoder, gameplay, menu-idle, CPU, battery, or memory overhead
after the download worker has finished.

## Host and repository tests

Add tests covering at minimum:

- provider import and registration against the pinned stable yt-dlp;
- provider import and registration against a candidate nightly package;
- `is_available()` performs no network work;
- exact client/context/authentication acceptance rules;
- base64url response validation;
- token expiration and `bypass_cache` behavior;
- no retry loop between token and fallback paths;
- cancellation and every timeout/resource limit;
- stale status and partial-file cleanup;
- accurate bot-check versus sign-in error classification;
- diagnostics redact tokens, visitor/session data, headers, and signed URLs;
- runtime manifests contain every new first-party and licensed third-party file;
- reproducible Windows/Linux QMOD builds remain byte-identical;
- normal build, host tests, repository invariant tests, and Quest ARM64 build.

Live network tests should not be the only proof because YouTube responses are
nondeterministic. Keep sanitized fixture tests for protocol parsing and run a
small, explicitly identified online smoke test only when network access is
available.

## Acceptance criteria

The feature is ready to consider for release only when all of the following
are true:

1. Big Screen can mint and use a guest-session mweb/GVS token entirely
   in-process on Quest 2 and Quest 3/3S.
2. No browser, Node.js, Deno, Docker, companion application, account login, or
   remote provider is required.
3. A provider failure falls back once without freezing the menu, corrupting a
   map assignment, or leaving stale progress.
4. Videos that do not need the provider still download at least as reliably as
   before.
5. Stable/nightly yt-dlp updates cannot activate if the public provider API is
   incompatible with Big Screen's plug-in.
6. Logs provide enough phase and error information for support while exposing
   no token, session, cookie, authorization, or signed-URL secrets.
7. The provider is inactive outside probe/download work and has no measurable
   playback or gameplay cost.
8. QMOD packaging, dependency notices, source reproducibility, MBF
   installation, and rollback validation all pass.
9. The user-facing error remains honest that a PO token may help but does not
   guarantee YouTube will accept a challenged public IP or session.

## Principal risks

| Risk | Consequence | Required mitigation |
|---|---|---|
| YouTube changes BotGuard/WebPO | Provider stops generating accepted tokens | Fail closed, retain fallback, pin provenance, and make compatibility failures visible |
| Token/session binding mismatch | Valid-looking token is rejected with 403 | Keep visitor identity, client, content binding, headers, and request context consistent |
| Remote JavaScript escapes intended capabilities | Native process or private data exposure | Fresh constrained QuickJS runtime; no generic native, file, process, or network bindings |
| Token or visitor data reaches logs | Privacy/security incident | Field-aware redaction tests and no raw token logging at any level |
| Token work runs on Unity thread | Menu hang or gameplay hitch | Use only the existing downloader worker and atomic status publication |
| yt-dlp provider API changes | Update activates but downloads fail | Extend candidate compatibility tests and transactional rollback boundary |
| Excessive retries during bot challenge | Longer block and poor UX | One token attempt, one ordinary fallback, then accurate terminal guidance |
| Upstream code copied without full audit | License or supply-chain problem | Pin commit/hash, audit provenance and dependencies, retain notices, reproduce build |
| Feature is assumed to solve all access errors | Misleading support and regressions | Preserve distinct errors for private, removed, age, region, account, IP, and token failures |

## Rough effort estimate

This is a substantial downloader feature, not a small yt-dlp argument change.
A realistic sequence is:

- several days for a host-side protocol proof and accurate error split;
- one to two weeks for the provider API, minimal QuickJS minter, tests, and
  packaging;
- another one to two weeks for Quest lifecycle testing, fallback hardening,
  stable/nightly update compatibility, and user-facing error handling.

Those estimates assume a reusable upstream WebPO algorithm can be adapted to
QuickJS without needing a broad browser emulation layer. YouTube changes can
invalidate the estimate or the approach.

## API and implementation references

- yt-dlp PO Token Guide:
  <https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide>
- yt-dlp public PO-token provider API:
  <https://github.com/yt-dlp/yt-dlp/blob/master/yt_dlp/extractor/youtube/pot/README.md>
- Featured BgUtils provider used as a behavioral reference:
  <https://github.com/Brainicism/bgutil-ytdlp-pot-provider>
- Browser-backed provider illustrating the heavier alternative:
  <https://github.com/coletdjnz/yt-dlp-getpot-wpc>
- Big Screen downloader security boundary:
  [DOWNLOADER_SECURITY.md](../../DOWNLOADER_SECURITY.md)
- Big Screen architecture:
  [ARCHITECTURE.md](../../ARCHITECTURE.md)

## Final recommendation

Implement this later on a dedicated feature branch, beginning with accurate
bot-check classification and a host-side protocol proof. The production target
should be a first-party, guest-session `mweb`/GVS provider that keeps HTTPS in
Python, performs only the bounded attestation calculation in QuickJS, and
falls back once to Big Screen's current downloader path.

Do not ship a custom general browser, browser-account login, Meta Quest Browser
cookie extraction, or an external token service. Those approaches add much
more security and maintenance risk than the narrow token-minter Big Screen
actually needs.
