/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/core_types.h"
#include "mtproto/mtproto_auth_key.h"

namespace MTP {
class Instance;
} // namespace MTP

namespace MTP::details {

class SerializedRequest;

enum class DcKeyBindState {
	Success,
	Failed,
	DefinitelyDestroyed,
};

class DcKeyBinder final {
public:
	explicit DcKeyBinder(AuthKeyPtr &&persistentKey);

	// msgId is in the server's own time: it goes inside the encrypted
	// body too, so it cannot be replaced after this.
	[[nodiscard]] SerializedRequest prepareRequest(
		const AuthKeyPtr &temporaryKey,
		uint64 sessionId,
		mtpMsgId msgId);
	[[nodiscard]] DcKeyBindState handleResponse(const mtpBuffer &response);
	[[nodiscard]] AuthKeyPtr persistentKey() const;

private:
	AuthKeyPtr _persistentKey;

};

} // namespace MTP::details
