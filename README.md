<p align="center">
  <img src="media/readme/owpengram_splash.png" alt="OwpenGram" width="440">
</p>

# 💻 OwpenGram for Desktop

**One familiar app. Any server you trust.**

OwpenGram for Desktop is a multi-server messenger built on a fast, familiar
experience. Use the official network, your own private server, or any community
node — each account independent, all in one app. Private by design, comfortable
to use, and free from lock-in.

> 🪟🐧 **Available now for Windows and Linux** — grab a build from the
> [Releases](https://github.com/owpengram/owpengram-desktop-client/releases)
> tab. macOS is planned.

> 🔗 Built on **MTProto API layer 229**, on top of Telegram Desktop `v7.2.2`.

<p align="center">
  <img src="media/readme/desktop_hero.png" alt="OwpenGram Desktop — chats" width="880">
</p>

---

## 📥 Download

Builds live in the
[Releases](https://github.com/owpengram/owpengram-desktop-client/releases) tab.

| Platform | File | What it is |
|---|---|---|
| 🪟 Windows | Windows x64 build | Unpack and run. Built against Qt 5.15.19, the same Qt the official Windows client uses. |
| 🐧 Ubuntu / Debian | `owpengram-desktop_<version>_amd64.deb` | A real distro package for **Ubuntu 18.04+ / Debian 10+** — installs to `/usr/bin`, adds the app-menu entry with proper icons, and a D-Bus activation service. Install with `sudo apt install ./owpengram-desktop_<version>_amd64.deb`. |
| 🐧 Any modern distro | `OwpenGram` | **Not an installer — the client binary itself.** `chmod +x OwpenGram && ./OwpenGram` and you're in. Qt, WebRTC and the X11/EGL client libraries are bundled and lazy-loaded, so it needs nothing but a current glibc. |

Both Linux files come out of the same portable build, so pick by taste: the
`.deb` if you want OwpenGram registered as an installed app, the bare binary if
you'd rather keep it self-contained or your distro isn't Debian-based. On a
Debian-based distro you can use either.

`tg://` and `owpg://` links work in both cases — the app registers itself as
their handler on first launch, no install step involved. What the `.deb` adds is
the desktop integration around that: an entry in the application menu with the
right name and icons, `OwpenGram` on your `PATH`, D-Bus activation, and a fixed
install path, so the link handler doesn't break the day you move the binary
somewhere else.

Want that integration without a package — on Arch, Fedora, anything — run
[`install-linux.sh`](install-linux.sh) next to the binary. It does the same
thing under `~/.local`, no root needed, and re-running it after an update
refreshes the installed copy.

## 🚀 Try it without setting up anything

We run a **public OwpenGram server**, and it is already built into this client —
no address to type, no key to paste, no Docker. Install the app, and on the
server-selection screen at login pick **OwpenGram** instead of Telegram, then
sign in as usual.

That account sits alongside your Telegram one, so you can try the project
without leaving anything behind. When you want your own server later, you add
it in the same app and keep both.

## ✨ Why you'll like it

- 🌐 **Multi-server** — add accounts on different servers and switch between them freely.
- 🏠 **Bring your own server** — connect to a server you host and fully control.
- 🔎 **Adding one takes an address** — type `host:port`, the client fetches the server's key, DC and identity itself.
- 🧠 **Familiar & comfortable** — the experience you already know, no learning curve.
- 🔒 **Private** — talk on infrastructure you trust, away from the cloud.
- 🛡️ **Censorship-resistant** — your own server stays reachable when others are blocked.
- 🆓 **Open source** — read it, audit it, build it yourself.

## 🌐 How multi-server works

Every account is tied to a server, and you choose that server when you sign in.
OwpenGram comes with ready-to-use options:

- **Telegram** — the official network (use your normal Telegram account)
- **OwpenGram** — our public server, live and ready to use, already in the list
- **Custom** — any server you or your community runs

Add several accounts on different servers and they stay cleanly separated —
different identities, different data, one app. Separation is real, not cosmetic:
each account's cached peers, files and ids are scoped to the server they came
from, so two servers that happen to hand out the same numeric id never bleed
into each other. Official Telegram accounts still count against Telegram's own
limit (3, or 6 with Premium); self-hosted ones aren't subject to that — the only
ceiling is the app's own **50 accounts in total**, across every server combined.

Remove a server and every account on it goes with it — no orphaned logins left
behind pointing at a host that no longer exists.

<p align="center">
  <img src="media/readme/desktop_multiserver.png" alt="Server selection and accounts grouped by server" width="880">
</p>

## 🔌 Connect your own server

On the **server selection screen** (shown when you log in or add a new account),
click **➕ Add server** and type the address — `chat.example.com:2398`, or
`203.0.113.10:2398`. That is the only field you have to fill in.

The client then asks the server who it is (`/owpengram/server-info` on that same
port) and fills in the rest by itself: RSA public key, data-centre id, and the
name, description and icon the server's operator set. Rename it or swap the icon
if you like — those are yours to edit. Save, pick the server, log in as usual.

Everything it fetched is still editable under **Advanced** — server type, DC id,
RSA key — for a server that doesn't answer that endpoint, or when you want to
pin the key yourself.

> Only `host:port` is ever taken from a link or typed in. The identity a server
> claims is fetched from that address directly, so nobody can hand you a link
> that misrepresents whose server you're about to trust.

Operators can hand out a ready-made **`owpg://addserver?host=...&port=...`**
link: opening it pops the Add server box pre-filled with the address. By design
it carries nothing else — no key, name or DC.

Server name, description and icon refresh on their own whenever a server list is
shown, so a rebrand on the operator's side appears without you re-adding
anything. Only those cosmetics — host, port, key and DC id are never touched
after you've saved them.

Don't have a server yet? Spin one up in one command:
👉 [owpengram-server](https://github.com/owpengram/owpengram-server)

## 🛠️ Build (Windows)

Run the interactive build script — double-click it or run from a terminal:

```bat
build-windows.bat
```

It guides you through API credentials, submodules, `prepare`, `configure` and the
MSBuild step, and remembers your answers in `.owpengram-build.local.json`
(gitignored).

**Requirements:** Visual Studio 2022 (C++ x64), Python 3.10, Git. x64 builds
against **Qt 5.15.19**, the same Qt the official Windows client uses — Qt 6 on
Windows x64 silently breaks clicks in nested popup submenus (Mute forever, Add
to folder), so the fork stays on 5.15. For manual steps and other platforms,
see `docs/building-win.md` and the upstream
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop) build docs.

## 🐧 Build (Linux)

Two modes, same script:

```bash
scripts/build-linux.sh            # native, Release (default)
scripts/build-linux.sh --debug    # native, Debug
scripts/build-linux.sh --docker   # portable, Release
```

**Native** builds against this machine's system libraries (the same packaged
mode Arch's own `telegram-desktop` uses). Fast, and fine for local work — but
the binary is tied to this machine's exact Qt/glibc, so it is not what you
distribute. It installs missing pacman dependencies, builds `tde2e` locally,
initializes submodules, and uses `mold` when available. Output: `out/OwpenGram`.

**Portable** (`--docker`) builds inside the official Rocky Linux 8 image, so the
result runs on essentially any Linux from the last ~6 years (Ubuntu 18.04+,
Debian 10+). Slower — it compiles Qt and WebRTC from scratch once. Output:
`out-docker/OwpenGram`. This is the binary the Releases tab ships, and the one
the `.deb` is built from. From Windows, `build-linux.bat` runs the same thing
through WSL2 + Docker Desktop.

Then, optionally:

```bash
./install-linux.sh          # per-user install into ~/.local (no root)
./package-deb-linux.sh      # -> owpengram-desktop_<version>_amd64.deb
```

`install-linux.sh` gives you the app-menu entry, icons, D-Bus service and a
`~/.local/bin/OwpenGram` on `PATH` without touching system directories;
`package-deb-linux.sh` produces the release `.deb` and needs only `dpkg-deb` —
it runs fine on Arch.

**Requirements:** native mode — Arch/Manjaro (pacman-based) or a distro with
equivalent packages, CMake, Ninja, Git. Portable mode — Docker.

## 📦 Part of the OwpenGram project

- 🚀 [Server](https://github.com/owpengram/owpengram-server)
- 🤖 [Android client](https://github.com/owpengram/owpengram-android-client)
- 🌐 [GitHub organization](https://github.com/owpengram)

## 💬 Community

- 📢 Channel: [@owpengram](https://t.me/owpengram)
- 💬 Chat: [Join the discussion](https://t.me/+sVB6Ymv70jEwNTAy)

## 📄 License

Based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) —
**GPLv3 with the OpenSSL exception** ([LICENSE](LICENSE)).

### Third-party

* Qt 6 ([LGPL](http://doc.qt.io/qt-6/lgpl.html)) and Qt 5.15 ([LGPL](http://doc.qt.io/qt-5/lgpl.html)) slightly patched
* OpenSSL 3.2.1 ([Apache License 2.0](https://openssl-library.org/source/license/apache-license-2.0.txt))
* WebRTC ([New BSD License](https://github.com/desktop-app/tg_owt/blob/master/LICENSE))
* zlib ([zlib License](http://www.zlib.net/zlib_license.html))
* LZMA SDK 9.20 ([public domain](http://www.7-zip.org/sdk.html))
* liblzma ([public domain](http://tukaani.org/xz/))
* Google Breakpad ([License](https://chromium.googlesource.com/breakpad/breakpad/+/master/LICENSE))
* Google Crashpad ([Apache License 2.0](https://chromium.googlesource.com/crashpad/crashpad/+/master/LICENSE))
* GYP ([BSD License](https://github.com/bnoordhuis/gyp/blob/master/LICENSE))
* Ninja ([Apache License 2.0](https://github.com/ninja-build/ninja/blob/master/COPYING))
* OpenAL Soft ([LGPL](https://github.com/kcat/openal-soft/blob/master/COPYING))
* Opus codec ([BSD License](http://www.opus-codec.org/license/))
* FFmpeg ([LGPL](https://www.ffmpeg.org/legal.html))
* Guideline Support Library ([MIT License](https://github.com/Microsoft/GSL/blob/master/LICENSE))
* Range-v3 ([Boost License](https://github.com/ericniebler/range-v3/blob/master/LICENSE.txt))
* Open Sans font ([Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0.html))
* Vazirmatn font ([SIL Open Font License 1.1](https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt))
* Emoji alpha codes ([MIT License](https://github.com/emojione/emojione/blob/master/extras/alpha-codes/LICENSE.md))
* xxHash ([BSD License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE))
* QR Code generator ([MIT License](https://github.com/nayuki/QR-Code-generator#license))
* CMake ([New BSD License](https://github.com/Kitware/CMake/blob/master/Copyright.txt))
* Hunspell ([LGPL](https://github.com/hunspell/hunspell/blob/master/COPYING.LESSER))
* Ada ([Apache License 2.0](https://github.com/ada-url/ada/blob/main/LICENSE-APACHE))

---

⭐ If OwpenGram is useful to you, a star on GitHub helps a lot.
