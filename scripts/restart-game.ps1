adb shell am force-stop com.beatgames.beatsaber
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
adb shell am start com.beatgames.beatsaber/com.unity3d.player.UnityPlayerActivity
exit $LASTEXITCODE
