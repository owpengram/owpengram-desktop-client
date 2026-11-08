/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_server_time.h"

#include "base/unixtime.h"
#include "logs.h"

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <map>
#include <optional>
#include <vector>

namespace MTP::details {
namespace {

// Clocks within this distance count as agreeing. Real servers with NTP are
// a second or two apart; a server without it drifts by minutes or hours.
constexpr auto kAgreeDistance = TimeId(60);

// Below this the app-wide shift is left alone, like base::unixtime does.
constexpr auto kIgnoreChange = TimeId(3);

struct Source {
	std::optional<TimeId> shift; // server time minus local time
	bool trusted = false;
	uint64 order = 0; // when the shift was last set, bigger is newer
};

QMutex Mutex;
std::map<const void*, Source> Sources;
uint64 Counter = 0;

[[nodiscard]] TimeId LocalNow() {
	return TimeId(::time(nullptr));
}

[[nodiscard]] TimeId GlobalShift() {
	return base::unixtime::now() - LocalNow();
}

[[nodiscard]] bool Agree(TimeId a, TimeId b) {
	return std::abs(a - b) <= kAgreeDistance;
}

// Must be called with Mutex locked.
[[nodiscard]] TimeId ChooseShiftLocked() {
	// 1. The newest report of a Telegram server.
	const Source *telegram = nullptr;
	for (const auto &[key, source] : Sources) {
		if (source.trusted
			&& source.shift
			&& (!telegram || source.order > telegram->order)) {
			telegram = &source;
		}
	}
	if (telegram) {
		return *telegram->shift;
	}

	// 2. The biggest group of agreeing servers, the local clock voting too.
	auto servers = std::vector<TimeId>();
	for (const auto &[key, source] : Sources) {
		if (source.shift) {
			servers.push_back(*source.shift);
		}
	}
	if (servers.size() < 2) {
		return 0; // 3. One server alone never outvotes the local clock.
	}
	auto bestCenter = TimeId(0);
	auto bestVotes = 0;
	auto bestHasLocal = false;
	auto candidates = servers;
	candidates.push_back(0);
	for (const auto center : candidates) {
		const auto hasLocal = Agree(center, 0);
		auto votes = hasLocal ? 1 : 0;
		for (const auto shift : servers) {
			if (Agree(center, shift)) {
				++votes;
			}
		}
		if (votes > bestVotes
			|| (votes == bestVotes && hasLocal && !bestHasLocal)) {
			bestCenter = center;
			bestVotes = votes;
			bestHasLocal = hasLocal;
		}
	}
	if (bestHasLocal) {
		return 0;
	}
	auto group = std::vector<TimeId>();
	for (const auto shift : servers) {
		if (Agree(bestCenter, shift)) {
			group.push_back(shift);
		}
	}
	if (group.size() < 2) {
		return 0;
	}
	std::sort(group.begin(), group.end());
	return group[group.size() / 2];
}

// Must be called with Mutex locked.
void ApplyChosenLocked() {
	const auto chosen = ChooseShiftLocked();
	if (std::abs(chosen - GlobalShift()) < kIgnoreChange) {
		return;
	}
	DEBUG_LOG(("MTP Info: app time shift %1 -> %2"
		).arg(GlobalShift()
		).arg(chosen));
	base::unixtime::update(LocalNow() + chosen, true);
}

} // namespace

void SetServerTimeTrusted(not_null<const Instance*> instance, bool trusted) {
	QMutexLocker lock(&Mutex);
	auto &source = Sources[instance.get()];
	if (source.trusted == trusted) {
		return;
	}
	source.trusted = trusted;
	ApplyChosenLocked();
}

void ReportServerTime(
		not_null<const Instance*> instance,
		TimeId serverTime,
		bool force) {
	if (serverTime <= 0) {
		return;
	}
	QMutexLocker lock(&Mutex);
	auto &source = Sources[instance.get()];
	if (source.shift && !force) {
		return;
	}
	const auto shift = serverTime - LocalNow();
	if (source.shift && std::abs(*source.shift - shift) < kIgnoreChange) {
		return;
	}
	if (source.shift && !Agree(*source.shift, shift)) {
		LOG(("MTP Info: server clock of an instance moved %1 -> %2 seconds "
			"from the local clock").arg(*source.shift).arg(shift));
	} else if (!source.shift && !Agree(shift, 0)) {
		LOG(("MTP Info: server clock is %1 seconds from the local clock"
			).arg(shift));
	}
	source.shift = shift;
	source.order = ++Counter;
	ApplyChosenLocked();
}

void ForgetServerTime(not_null<const Instance*> instance) {
	QMutexLocker lock(&Mutex);
	if (Sources.erase(instance.get())) {
		ApplyChosenLocked();
	}
}

TimeId ServerNow(not_null<const Instance*> instance) {
	QMutexLocker lock(&Mutex);
	const auto i = Sources.find(instance.get());
	return (i != end(Sources) && i->second.shift)
		? (LocalNow() + *i->second.shift)
		: base::unixtime::now();
}

mtpMsgId NextMsgId(not_null<const Instance*> instance) {
	const auto id = base::unixtime::mtproto_msg_id();
	QMutexLocker lock(&Mutex);
	const auto i = Sources.find(instance.get());
	if (i == end(Sources) || !i->second.shift) {
		return id;
	}
	// The top 32 bits of a msg_id are unixtime seconds: move them from the
	// app-wide time to this server's time. The low bits stay as generated,
	// so ids keep their client bits and stay unique.
	const auto delta = int64(*i->second.shift - GlobalShift());
	return mtpMsgId(int64(id) + (delta << 32));
}

} // namespace MTP::details
