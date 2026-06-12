/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

namespace Branding {

constexpr auto AppName = "OwpenGram Desktop"_cs;
constexpr auto ShortAppName = "OwpenGram"_cs;
constexpr auto ApplicationName = "OwpengramDesktop"_cs;
constexpr auto CompanyName = "OwpenGram"_cs;

#ifdef _DEBUG
constexpr auto WindowsIconPath = "Resources/OwpenGram/art/icon256_debug.ico"_cs;
#else
constexpr auto WindowsIconPath = "Resources/OwpenGram/art/icon256.ico"_cs;
#endif
constexpr auto MacOSIconName = "Icon.icns"_cs;
constexpr auto LinuxIconBase = "Resources/OwpenGram/art/icon"_cs;

constexpr auto LogoPath = ":/gui/art/logo_256.png"_cs;
constexpr auto LogoNoMarginPath = ":/gui/art/logo_256_no_margin.png"_cs;
constexpr auto PlaneWhitePath = ":/gui/plane_white.svg"_cs;
constexpr auto BusinessLogoPath = ":/gui/art/business_logo.png"_cs;
constexpr auto AffiliateLogoPath = ":/gui/art/affiliate_logo.png"_cs;

constexpr auto TrayMonochromePath = ":/gui/icons/tray/monochrome.svg"_cs;
constexpr auto TrayMonochromeAttentionPath = ":/gui/icons/tray/monochrome_attention.svg"_cs;
constexpr auto TrayMonochromeMutePath = ":/gui/icons/tray/monochrome_mute.svg"_cs;

constexpr auto BundleIdentifier = "org.owpengram.desktop"_cs;

} // namespace Branding
