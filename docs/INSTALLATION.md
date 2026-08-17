# Installation and first run

## Requirements

- Meta Quest 2 or Quest 3.
- A modded standalone Quest installation of **Beat Saber 1.40.8 (`1.40.8_7379`)**.
- Enough free internal storage for the QMOD runtime and any downloaded videos. Big Screen reserves 512 MB before beginning a new download.

The current QMOD is version-specific. A newer or older Beat Saber APK can change native APIs and must use a separately tested build. Beat Saber 1.37.x users must install the QMOD built from the maintained `release/bs-1.37-alpha` branch instead.

## Install

1. Download the compatible `Big Screen.qmod` release file.
2. Open the Quest Beat Saber mod manager used for that Beat Saber installation.
3. Install/import the QMOD and allow its declared dependencies to resolve.
4. Fully restart Beat Saber.
5. Open **Mods > Big Screen**. If the page opens and the blank placement preview appears, the native mod and UI loaded.

The package installs its native libraries through the mod loader and places the embedded downloader runtime under `/sdcard/ModData/com.beatgames.beatsaber/BigScreen/Runtime`. QuickJS-NG is already compiled into Big Screen; users do not install a separate JavaScript runtime, Termux package, or executable.

## Recommended first-run setup

Keep the defaults initially: 720p, 30 FPS, Big Mirror override, Chroma override, and the three obstruction-hiding light controls. Use the Screen tab to place Layout 1. Test one known H.264 video before enabling 1080p, 60 FPS, transparency, or map-specific authored environments.

## Updating Big Screen

Install a newer QMOD built for the same Beat Saber version. Do not manually delete ModData before updating: that folder contains settings, assignments, timing data, downloaded videos, thumbnails, and library backups.

The Update tab shows the installed Big Screen version and checks for the latest public stable release once per Beat Saber session. A newer release produces a popup with both versions; **Check Big Screen Update** can repeat the check manually. The checker is notification-only and never replaces the installed QMOD. If the repository has no publicly visible release, the manual result explains that no public release information was available.

The separate **Check yt-dlp** control updates only the embedded downloader Python package. It does not update Big Screen itself or Beat Saber. The Update tab also shows the yt-dlp version currently active in the runtime.

## Removing or disabling

- To stop all behavior but keep the mod installed, turn off **Big Screen Enabled**. The menu remains available so it can be re-enabled.
- To uninstall, remove Big Screen through the mod manager. ModData may intentionally remain so assignments and downloaded files survive reinstalling.
- Before manually deleting ModData, copy out any downloaded video you want to keep. Map-folder and Video Import files are independent user-owned files.
