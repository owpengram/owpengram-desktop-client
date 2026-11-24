/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "owpengram/owpengram_cross_forward.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "crl/crl_on_main.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_premium_limits.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "owpengram/owpengram_servers.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/layers/show.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QUuid>

namespace Owpengram {
namespace {

struct Attachment {
	PhotoData *photo = nullptr;
	DocumentData *document = nullptr;
	FullMsgId origin;
};

// Everything needed to rebuild the message elsewhere, taken when the menu
// item is clicked: the source item may be gone by the time a chat is picked.
struct Snapshot {
	TextWithTags text;
	std::vector<Attachment> attachments;
};

[[nodiscard]] HistoryItemsList ItemOrItsGroup(not_null<HistoryItem*> item) {
	if (const auto group = item->history()->owner().groups().find(item)) {
		return group->items;
	}
	return { item };
}

[[nodiscard]] TextWithEntities WithoutCustomEmoji(TextWithEntities text) {
	// Custom emoji entities point at documents of the source server; the
	// target server can't resolve them. The emoji fallback text stays.
	text.entities.erase(
		std::remove_if(
			text.entities.begin(),
			text.entities.end(),
			[](const EntityInText &entity) {
				return (entity.type() == EntityType::CustomEmoji);
			}),
		text.entities.end());
	return text;
}

[[nodiscard]] QString OriginalLink(not_null<HistoryItem*> item) {
	// Only channels and supergroups have post links. A private one gets the
	// t.me/c/<id>/<msg> form, which opens for its members only.
	return item->hasDirectLink()
		? item->history()->session().api().exportDirectMessageLink(
			item,
			false)
		: QString();
}

[[nodiscard]] Snapshot TakeSnapshot(not_null<HistoryItem*> item) {
	const auto items = ItemOrItsGroup(item);

	// In an album the caption lives on one of the items.
	auto text = item->originalText();
	if (text.text.isEmpty()) {
		for (const auto &part : items) {
			if (!part->originalText().text.isEmpty()) {
				text = part->originalText();
				break;
			}
		}
	}
	auto full = WithoutCustomEmoji(std::move(text));
	if (const auto link = OriginalLink(item); !link.isEmpty()) {
		if (!full.text.isEmpty()) {
			full.append(u"\n\n"_q);
		}
		full.append(link);
	}

	auto result = Snapshot{
		.text = TextWithTags{
			full.text,
			TextUtilities::ConvertEntitiesToTextTags(full.entities),
		},
	};
	for (const auto &part : items) {
		const auto media = part->media();
		if (!media || media->webpage()) {
			continue;
		} else if (const auto photo = media->photo()) {
			result.attachments.push_back({
				.photo = photo,
				.origin = part->fullId(),
			});
		} else if (const auto document = media->document()) {
			result.attachments.push_back({
				.document = document,
				.origin = part->fullId(),
			});
		}
	}
	return result;
}

[[nodiscard]] QString NewTempFolder() {
	const auto path = QDir::tempPath()
		+ u"/owpengram-forward/"_q
		+ QUuid::createUuid().toString(QUuid::WithoutBraces);
	return QDir().mkpath(path) ? path : QString();
}

[[nodiscard]] QString SafeFileName(const QString &name, int index) {
	auto result = QFileInfo(name).fileName();
	if (result.isEmpty()) {
		result = u"file"_q;
	}
	// Keep files with equal names apart inside one album.
	return QString::number(index) + u"_"_q + result;
}

// Makes every attachment available as a local file in the SOURCE session,
// downloading what isn't loaded yet, then calls done() with the paths, or
// fail() if a download was cancelled or a file couldn't be written.
void PrepareFiles(
		not_null<Main::Session*> source,
		std::vector<Attachment> attachments,
		Fn<void(QStringList)> done,
		Fn<void()> fail) {
	struct Entry {
		Attachment attachment;
		std::shared_ptr<Data::PhotoMedia> photo;
		std::shared_ptr<Data::DocumentMedia> document;
	};
	struct State {
		std::vector<Entry> entries;
		bool finished = false;
		rpl::lifetime lifetime;
	};
	const auto state = std::make_shared<State>();
	for (const auto &attachment : attachments) {
		auto entry = Entry{ .attachment = attachment };
		const auto origin = Data::FileOrigin(attachment.origin);
		if (const auto photo = attachment.photo) {
			entry.photo = photo->createMediaView();
			entry.photo->wanted(Data::PhotoSize::Large, origin);
		} else if (const auto document = attachment.document) {
			entry.document = document->createMediaView();
			if (document->filepath(true).isEmpty()
				&& !entry.document->loaded(true)) {
				document->save(origin, QString());
			}
		}
		state->entries.push_back(std::move(entry));
	}

	// Returns true once finished, successfully or not.
	const auto check = [=]() -> bool {
		if (state->finished) {
			return true;
		}
		for (const auto &entry : state->entries) {
			if (entry.photo) {
				if (!entry.photo->loaded()) {
					return false;
				}
			} else if (const auto document = entry.attachment.document) {
				if (!document->filepath(true).isEmpty()
					|| entry.document->loaded(true)) {
					continue;
				} else if (document->cancelled()) {
					state->finished = true;
					fail();
					return true;
				}
				return false;
			}
		}
		state->finished = true;

		const auto folder = NewTempFolder();
		if (folder.isEmpty()) {
			fail();
			return true;
		}
		auto paths = QStringList();
		auto index = 0;
		for (const auto &entry : state->entries) {
			++index;
			if (entry.photo) {
				const auto video = !entry.photo->videoContent(
					Data::PhotoSize::Large).isEmpty();
				const auto path = folder
					+ '/'
					+ SafeFileName(
						video ? u"photo.mp4"_q : u"photo.jpg"_q,
						index);
				if (!entry.photo->saveToFile(path)) {
					fail();
					return true;
				}
				paths.push_back(path);
			} else if (const auto document = entry.attachment.document) {
				if (const auto local = document->filepath(true)
					; !local.isEmpty()) {
					paths.push_back(local);
					continue;
				}
				const auto bytes = entry.document->bytes();
				const auto path = folder
					+ '/'
					+ SafeFileName(document->filename(), index);
				auto file = QFile(path);
				if (bytes.isEmpty()
					|| !file.open(QIODevice::WriteOnly)
					|| file.write(bytes) != bytes.size()) {
					fail();
					return true;
				}
				paths.push_back(path);
			}
		}
		done(std::move(paths));
		return true;
	};

	if (check()) {
		return;
	}
	source->downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (check()) {
			// The posted lambda keeps the state alive while its own
			// subscription is destroyed.
			crl::on_main([state] {
				state->lifetime.destroy();
			});
		}
	}, state->lifetime);
}

void SendText(
		not_null<Data::Thread*> thread,
		const TextWithTags &text) {
	if (text.text.isEmpty()) {
		return;
	}
	auto message = Api::MessageToSend(Api::SendAction(thread));
	message.textWithTags = text;
	message.action.clearDraft = false;
	thread->session().api().sendMessage(std::move(message));
}

// Sends the files grouped like the "send files" box does by default
// (images and videos as an album), with the caption on the album.
[[nodiscard]] bool SendFiles(
		not_null<Data::Thread*> thread,
		const QStringList &paths,
		TextWithTags caption) {
	const auto session = &thread->session();
	auto list = Storage::PrepareMediaList(
		paths,
		st::sendMediaPreviewSize,
		session->user()->isPremium());
	if (list.error != Ui::PreparedList::Error::None || list.files.empty()) {
		return false;
	}
	auto way = Ui::SendFilesWay();
	way.setGroupFiles(true);
	way.setSendImagesAsPhotos(true);
	auto groups = Ui::DivideByGroups(std::move(list), way, false);
	if (groups.empty()) {
		return false;
	}
	if (!caption.text.isEmpty()) {
		auto &group = groups.back();
		auto &files = group.list.files;
		if (!files.empty()) {
			auto &captioned = (group.type == Ui::AlbumType::PhotoVideo)
				? files.front()
				: files.back();
			captioned.caption = std::move(caption);
		}
	}
	auto action = Api::SendAction(thread);
	action.clearDraft = false;
	for (auto &group : groups) {
		const auto album = (group.type != Ui::AlbumType::None)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		session->api().sendFiles(
			std::move(group.list),
			SendMediaType::Photo,
			album,
			action);
	}
	return true;
}

// Sends the copy right away from the target account, in the background:
// the user stays in the current account (as with a regular Forward).
void Deliver(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> source,
		not_null<Data::Thread*> thread,
		std::shared_ptr<Snapshot> snapshot) {
	const auto name = thread->peer()->name();
	const auto sent = [=] {
		if (show->valid()) {
			show->showToast(tr::lng_owpengram_forward_to_sent(
				tr::now,
				lt_chat,
				name));
		}
	};
	if (snapshot->attachments.empty()) {
		SendText(thread, snapshot->text);
		sent();
		return;
	}

	show->showToast(tr::lng_owpengram_forward_to_downloading(tr::now));
	const auto weakThread = base::make_weak(thread);
	PrepareFiles(source, snapshot->attachments, [=](QStringList paths) {
		const auto thread = weakThread.get();
		if (!thread) {
			return;
		}
		// A text longer than a caption may be goes as its own message
		// right before the media.
		const auto limit = Data::PremiumLimits(
			&thread->session()).captionLengthCurrent();
		auto caption = snapshot->text;
		if (caption.text.size() > limit) {
			SendText(thread, caption);
			caption = {};
		}
		if (SendFiles(thread, paths, std::move(caption))) {
			sent();
		} else if (show->valid()) {
			show->showToast(tr::lng_owpengram_forward_to_failed(tr::now));
		}
	}, [=] {
		if (show->valid()) {
			show->showToast(tr::lng_owpengram_forward_to_failed(tr::now));
		}
	});
}

void StartCrossForward(
		std::shared_ptr<Ui::Show> show,
		not_null<HistoryItem*> item,
		not_null<Main::Account*> target) {
	if (!target->sessionExists()) {
		return;
	}
	const auto source = &item->history()->session();
	const auto snapshot = std::make_shared<Snapshot>(TakeSnapshot(item));
	const auto weakSource = base::make_weak(source);
	auto chosen = [=](not_null<Data::Thread*> thread) -> bool {
		const auto source = weakSource.get();
		if (!source) {
			return true;
		} else if (!snapshot->attachments.empty()
			&& !Data::CanSendAnyOf(thread, Data::FilesSendRestrictions())) {
			show->showToast(tr::lng_forward_send_files_cant(tr::now));
			return false;
		} else if (!Data::CanSendTexts(thread)) {
			show->showToast(tr::lng_share_cant(tr::now));
			return false;
		}
		// Deliver after the picker has closed itself.
		const auto weakThread = base::make_weak(thread);
		crl::on_main([=] {
			const auto source = weakSource.get();
			const auto thread = weakThread.get();
			if (source && thread) {
				Deliver(show, source, thread, snapshot);
			}
		});
		return true;
	};
	show->showBox(Window::PrepareChooseRecipientBox(
		&target->session(),
		std::move(chosen),
		tr::lng_owpengram_forward_to_choose()));
}

} // namespace

bool AddForwardToAccountAction(
		not_null<Ui::PopupMenu*> menu,
		std::shared_ptr<Ui::Show> show,
		not_null<HistoryItem*> item) {
	if (!item->isRegular()
		|| !item->allowsForward()
		|| item->forbidsSaving()) {
		return false;
	}
	const auto current = &item->history()->session().account();
	const auto grouped = AuthedAccountsByServer();
	const auto hasOther = ranges::any_of(grouped, [&](const auto &entry) {
		return (entry.account != current);
	});
	if (!hasOther) {
		return false;
	}

	auto submenu = std::make_unique<Ui::PopupMenu>(menu.get(), menu->st());
	const auto itemId = item->fullId();
	const auto owner = &item->history()->owner();
	auto lastServerId = QString();
	for (const auto &entry : grouped) {
		const auto account = entry.account;
		if (account == current) {
			continue;
		}
		if (entry.server.id != lastServerId) {
			if (!lastServerId.isEmpty()) {
				submenu->addSeparator();
			}
			lastServerId = entry.server.id;
			const auto title = !entry.server.name.isEmpty()
				? entry.server.name
				: entry.server.host;
			submenu->addAction(title, [] {})->setEnabled(false);
		}
		const auto weakAccount = base::make_weak(account);
		submenu->addAction(account->session().user()->name(), [=] {
			const auto account = weakAccount.get();
			const auto item = owner->message(itemId);
			if (account && item) {
				StartCrossForward(show, item, account);
			}
		});
	}
	menu->addAction(
		tr::lng_owpengram_forward_to(tr::now),
		std::move(submenu),
		&st::menuIconForward);
	return true;
}

} // namespace Owpengram
