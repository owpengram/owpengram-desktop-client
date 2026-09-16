# Updates

OwpenGram updates itself from its own GitHub releases. Upstream's updater is
not used, and must not be: see [Why not upstream's](#why-not-upstreams).

## How it works

1. The client reads the release feed — by default
   `https://api.github.com/repos/owpengram/owpengram-desktop-client/releases/latest`.
2. It parses the tag (`O7` → build `7`) and compares it with the
   `OWPENGRAM_BUILD` it was compiled with.
3. If the release is newer, it downloads the asset for this platform:
   `OwpenGram.exe` on Windows, the bare `OwpenGram` binary on Linux.
4. It checks the size and the SHA-256 that GitHub publishes for the asset,
   then stages the file in `<working dir>/owpengram-update/`.
5. The usual "Restart and update" button appears in Advanced settings. On
   restart the app renames its own executable aside and moves the staged one
   into place, then relaunches. The leftover `.old` file is removed on a
   later launch.

No helper process, no archive, no repacking: the file on the release *is* the
file that ends up installed.

Everything the user sees — the progress bar, the button, the check on startup
— is upstream's UI, driven through the usual `Core::UpdateChecker` signals.
Only the transport and the apply step are ours.

## Building an updatable client

`OWPENGRAM_BUILD` must carry the number of the release the build ships under:

```bash
cmake -S . -B out -DOWPENGRAM_BUILD=8    # for tag O8
```

Both build scripts read it from the environment, so a release build is:

```powershell
$env:OWPENGRAM_BUILD = "8"; .uild-windows.bat
```

```bash
OWPENGRAM_BUILD=8 ./scripts/build-linux.sh --docker
```

It defaults to `0`, which keeps self-update off — the right thing for a local
developer build. `AppVersion` cannot be used for this: it tracks the upstream
Telegram version (7.2.2) and has nothing to do with our release numbering.

## Testing an update locally

Nothing needs to be published. `OWPENGRAM_UPDATE_URL` replaces the feed, and
`scripts/fake-update-feed.py` serves one around any binary:

```bash
python scripts/fake-update-feed.py out/Release/OwpenGram.exe --build 99
```

It prints the SHA-256 it computed and the URL to point the client at:

```powershell
# Windows
$env:OWPENGRAM_UPDATE_URL = "http://127.0.0.1:8099/latest.json"
.\OwpenGram.exe
```

```bash
# Linux
OWPENGRAM_UPDATE_URL=http://127.0.0.1:8099/latest.json ./OwpenGram
```

Then Settings → Advanced → Check for updates. The client will report the
update, download it from the local server, stage it, and offer the restart.

What to watch in the log (`log.txt`):

```
Update Info: O99 available (running build 0), fetching OwpenGram.exe.
Update Info: staged OwpenGram.exe (build 99) for install.
Update Info: installed build 99 over '...\OwpenGram.exe'.
```

A build with `OWPENGRAM_BUILD=0` still updates while a test feed is set, so a
normal developer build is enough to exercise the whole path.

To test the failure branch, corrupt the served file after the feed has been
generated: the digest check rejects it and the update is not staged.

## What does not self-update

- **The Debian package.** `/usr/bin/OwpenGram` belongs to dpkg; replacing it
  behind the package manager leaves its database describing a file that is no
  longer there. Detected by the install path and by write access, and the
  updater stays off.
- **Flatpak and Snap.** Handled by upstream's own portal path.
- **Any read-only install**, for the same reason.

## Why not upstream's

Upstream's updater expects a signed `.tdupdate` archive that unpacks into a
directory tree, plus a companion `Updater` binary sitting next to the app to
perform the swap. An OwpenGram release is a single self-contained executable
and ships no `Updater`, so that path cannot apply anything we publish.

It also must never run here. Its HTTP feed is `https://td.telegram.org` and
its MTP checker reads Telegram's own channels: both serve genuine Telegram
Desktop builds. `AppVersion` in this fork is the upstream version it was
rebased onto, so a current upstream release compares as newer -- and
`_other/updater_win.cpp` renames `Telegram.exe` to whatever the running
executable is called, so the swap would go through. A user would end up
running Telegram Desktop with their multi-server setup gone.

That has never shipped, because the builds were configured with
`DESKTOP_APP_DISABLE_AUTOUPDATE=ON`, which compiles the whole updater out. But
that switch is also what the OwpenGram updater needs turned off: everything
hangs on `UpdaterDisabled()` -- the checker in `sandbox.cpp`, the apply step in
`launcher.cpp`, the update row in Advanced settings. Enabling our updater means
compiling upstream's back in.

So `Updater::start()` in this fork never starts `HttpChecker` or `MtpChecker`.
When the OwpenGram updater is not available for an install, the client checks
nothing at all rather than falling back. That guard is the precondition for
building with the flag off, not a fix for anything that was ever released.

The flag used to live only in the local `out/CMakeCache.txt`, so a fresh
checkout would have configured itself the other way. Both build scripts now
pass it explicitly.

## Integrity

Two things are checked before a download is staged: the size, and the SHA-256
that GitHub publishes for the asset (`"digest": "sha256:..."`). Both arrive
over TLS together with the download URL.

This stops a corrupted or truncated download and a tampering CDN. It does not
stop whoever controls the GitHub account, since they can replace the asset and
its digest together. Closing that gap needs a detached signature made with a
key that never touches CI — a `SHA256SUMS` and `SHA256SUMS.sig` pair next to
the assets, with the public key compiled into the client. The release files
stay ordinary files either way; only two small ones are added. Not implemented
yet.
