"""yt-dlp JavaScript challenge provider backed by Big Screen's native engine.

Android does not allow Beat Saber to launch a downloaded qjs executable. Big
Screen therefore registers this provider explicitly and evaluates the official
yt-dlp EJS scripts through a built-in CPython module linked into the mod.

EJSBaseJCP is currently yt-dlp's shared implementation for assembling and
validating its official solver scripts. It is an internal API, so Big Screen's
startup compatibility test imports this module against every downloader update.
An incompatible update is rejected and rolled back before a user can download.
"""

import bigscreen_quickjs

from yt_dlp.extractor.youtube.jsc._builtin.ejs import EJSBaseJCP
from yt_dlp.extractor.youtube.jsc.provider import (
    JsChallengeProvider,
    JsChallengeProviderError,
    JsChallengeRequest,
    register_preference,
    register_provider,
)


@register_provider
class BigScreenQuickJSJCP(EJSBaseJCP):
    """Runs yt-dlp's verified EJS payload without spawning another process."""

    PROVIDER_VERSION = bigscreen_quickjs.version
    BUG_REPORT_LOCATION = "the Big Screen log folder"
    JS_RUNTIME_NAME = f"Big Screen QuickJS-NG {bigscreen_quickjs.version}"

    def is_available(self, /) -> bool:
        # Importing this module proves the compiled bridge is available. Keep
        # EJSBaseJCP's state flag so script hash/version rejection still makes
        # this provider unavailable for the remainder of the extraction.
        return self._available

    def _run_js_runtime(self, source: str, /) -> str:
        try:
            return bigscreen_quickjs.execute(source)
        except Exception as error:
            raise JsChallengeProviderError(
                f"Big Screen's QuickJS-NG engine could not solve the challenge: {error}"
            ) from error


@register_preference(BigScreenQuickJSJCP)
def prefer_bigscreen_quickjs(
    provider: JsChallengeProvider,
    requests: list[JsChallengeRequest],
) -> int:
    """Prefer the in-process engine over providers needing an executable."""

    return 2000
