# PS4 Cloud Save by Znichka

PS4 Cloud Save by Znichka moves PS4 saves between consoles through Google
Drive. Back up a save on the source PS4, then download and restore it on the
other PS4.

This is an independent project by Znichka / Andrey based on
[Apollo Save Tool](https://github.com/bucanero/apollo-ps4/) by Bucanero.
The original developer does not endorse this fork.

Project website: [znichka.xyz](https://www.znichka.xyz/).

## Move a save between PS4s

1. On the source PS4, open **Settings > Connect Google Drive** and follow the
   displayed URL and code. In **HDD Saves**, select the save and choose
   **Back up to Google Drive**.
2. On the destination PS4, connect to the same Google Drive account. Open
   **Google Drive**, choose the backup, and press **×** to download it. The app
   validates the download, ZIP, and save metadata before offering restore.
3. Choose the restore action for the destination:
   - **Save already exists:** press **△ Replace**. You do not need to
     delete the existing save.
   - **Empty save slot:** press **× empty-slot restore**.

Matching offline Account IDs on both PS4s can be needed for a cross-console
restore; their local user IDs may differ. This app includes **User Tools >
Activate PS4 Accounts** for an inactive profile. Enter the matching offline
Account ID and reboot when prompted. The feature needs no PSN sign-in.

Replace was tested successfully on PS4, and the restored save progress loaded.
Keep the console awake until the operation finishes. Replace is not atomic, so
keep a separate backup of the current save before using it. Backups remain in
Google Drive; a failed restore retains its downloaded ZIP.
If a restore fails, read the on-screen error and check
`/data/ps4-save-sync/google_restore.log`. Keep the downloaded ZIP; if the app
shows a pending recovery, use **R1** to retry it before another save operation.

For setup, recovery behavior, and troubleshooting, see
[Google Drive instructions](docs/google-drive.md). The same **How to use**
guide is available in Settings and on the Google Drive screen.

## Build and test

The [Makefile](Makefile) builds the PS4 package with the OpenOrbis toolchain
and its linked libraries, including Bucanero's `apollo-lib`. See the
[CI workflow](.github/workflows/build.yml) for the full dependency setup.
Set `OO_PS4_TOOLCHAIN` to the installed toolchain path. Configure a Google
OAuth TV/device client with `python3 tools/configure_google.py` using
external credentials or `GDRIVE_CLIENT_ID` and `GDRIVE_CLIENT_SECRET`;
the generated `include/google_build_config.h` is ignored by Git. Then run
`make`. Host checks run with `sh tools/test_google_host.sh`; CI lists their
host dependencies.

## License and attribution

This project retains the [GNU GPL version 3 or later](LICENSE) license.
Apollo Save Tool (PS4) is Copyright (C) 2020-2026
[Damian Parrino](https://twitter.com/dparrino) (Bucanero). Its source and
original notices are available in the
[Apollo Save Tool repository](https://github.com/bucanero/apollo-ps4/).

Original Apollo acknowledgments retained here:

- [Dnawrkshp](https://github.com/Dnawrkshp/) — Artemis PS3.
- [hzh](https://github.com/hzhreal) / [Team-Alua](https://github.com/Team-Alua/cecie.nim) — vsh-utils.
- [Berion](https://www.psx-place.com/members/berion.1431/) — GUI design.
- [flatz](https://github.com/flatz) — SFO tools.
- [aldostools](https://aldostools.org/) — Bruteforce Save Data.
- [jimmikaelkael](https://github.com/jimmikaelkael) — ps3mca tool.
- [ShendoXT](https://github.com/ShendoXT) — MemcardRex.
- [Nobody / Wild Light](https://github.com/nobodo) — upstream background
  music (not bundled in this fork).

Original translator credits: Akela (Russian), Algol (French), Bucanero
(Spanish), TheheroGAC (Italian), yyoossk (Japanese), Phoenixx1202
(Portuguese), Gabor Sari (Hungarian), SpyroMancer (Greek), SoftwareRat
(German), and Sun Zhonglei (Chinese).

Third-party CA and JSON notices are retained under `assets/google/`.
