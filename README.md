# 💻 OwpenGram for Desktop

**One familiar app. Any server you trust.**

OwpenGram for Desktop is a multi-server messenger for Windows, macOS and Linux,
built on a fast, familiar experience. Use the official network, your own private
server, or any community node — each account independent, all in one app.
Private by design, comfortable to use, and free from lock-in.

> 📸 _Screenshot placeholder — to be added._

---

## ✨ Why you'll like it

- 🌐 **Multi-server** — add accounts on different servers and switch between them freely.
- 🏠 **Bring your own server** — connect to a server you host and fully control.
- 🧠 **Familiar & comfortable** — the experience you already know, no learning curve.
- 🔒 **Private** — talk on infrastructure you trust, away from the cloud.
- 🛡️ **Censorship-resistant** — your own server stays reachable when others are blocked.
- 🆓 **Open source** — read it, audit it, build it yourself.

## 🌐 How multi-server works

Every account is tied to a server, and you choose that server when you sign in.
OwpenGram comes with ready-to-use options:

- **Telegram** — the official network (use your normal Telegram account)
- **OwpenGram** — the project's public server
- **Custom** — any server you or your community runs

Add several accounts on different servers and they stay cleanly separated —
different identities, different data, one app.

## 🔌 Connect your own server

On the **server selection screen** (shown when you log in or add a new account),
click **➕ Add server** and fill in:

- **Name** — any label you like (e.g. *My Server*)
- **Host** — your server's IP or domain (e.g. `203.0.113.10` or `chat.example.com`)
- **Port** — `10443` (the default OwpenGram MTProto port)
- **Type** — choose **single-server** for a self-hosted server (pick **Multi-DC (Telegram)** only for true multi-datacenter networks)
- **Main data center** — leave as `1` for a self-hosted server
- **RSA key** — leave **empty** unless your server uses a custom key

Then save, select the server, and log in as usual.

> The default OwpenGram server key is already built in, so the RSA field stays
> blank in almost all cases. Only paste a PEM public key if the server operator
> replaced the server's key with their own.

Don't have a server yet? Spin one up in one command:
👉 [owpengram-server](https://github.com/owpengram/owpengram-server)

## 🛠️ Build (Windows)

Run the interactive build script — double-click it or run from a terminal:

```bat
build-desktop.bat
```

It guides you through API credentials, submodules, `prepare`, `configure` and the
MSBuild step, and remembers your answers in `.owpengram-build.local.json`
(gitignored).

**Requirements:** Visual Studio 2022 (C++ x64), Python 3.10, Git. For manual
steps and other platforms, see `docs/building-win-x64.md` and the upstream
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop) build docs.

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

---

⭐ If OwpenGram is useful to you, a star on GitHub helps a lot.
