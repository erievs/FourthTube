#include "headers.hpp"
#include "subscription_util.hpp"
#include "util/util.hpp"
#include "ui/ui.hpp"
#include "system/file.hpp"
#include "youtube_parser/internal_common.hpp"
#include "oauth/oauth.hpp"
#include <set>

using namespace rapidjson;

static std::vector<SubscriptionChannel> subscribed_channels;
static Mutex resource_lock;

#define SUBSCRIPTION_VERSION 0
#define SUBSCRIPTION_FILE_PATH (DEF_MAIN_DIR + "subscription.json")
#define SUBSCRIPTION_FILE_TMP_PATH (DEF_MAIN_DIR + "subscription_tmp.json")

static AtomicFileIO atomic_io(SUBSCRIPTION_FILE_PATH, SUBSCRIPTION_FILE_TMP_PATH);

static bool is_valid_subscription_channel(const SubscriptionChannel &channel) {
	return is_youtube_url(channel.url) && is_youtube_thumbnail_url(channel.icon_url);
}

void load_subscription() {
	auto tmp = atomic_io.load([](const std::string &data) {
		Document json_root;
		std::string error;
		RJson data_json = RJson::parse(json_root, data.c_str(), error);

		int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;
		return version >= 0;
	});
	Result_with_string result = tmp.first;
	std::string data = tmp.second;
	if (result.code != 0) {
		logger.error("subsc/load", result.string + result.error_description + " " + std::to_string(result.code));
		return;
	}

	Document json_root;
	std::string error;
	RJson data_json = RJson::parse(json_root, data.c_str(), error);

	int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;

	std::vector<SubscriptionChannel> loaded_channels;
	if (version >= 0) {
		for (auto video : data_json["channels"].array_items()) {
			SubscriptionChannel cur_channel;
			cur_channel.id = video["id"].string_value();
			cur_channel.url = video["url"].string_value();
			cur_channel.icon_url = video["icon_url"].string_value();
			cur_channel.name = video["name"].string_value();
			cur_channel.subscriber_count_str = video["subscriber_count_str"].string_value();
			// "invalid" channels will not be shown in the subscription list but will still be kept in the subscription
			// file
			cur_channel.valid = is_valid_subscription_channel(cur_channel);
			if (!cur_channel.valid) {
				logger.caution("subsc/load", "invalid channel : " + cur_channel.name);
			}

			loaded_channels.push_back(cur_channel);
		}
		std::sort(loaded_channels.begin(), loaded_channels.end(),
		          [](const auto &i, const auto &j) { return i.name < j.name; });
	} else {
		logger.error("subsc/load", "json err : " + data.substr(0, 40));
		return;
	}

	resource_lock.lock();
	subscribed_channels = loaded_channels;
	resource_lock.unlock();
	logger.info("subsc/load", "loaded subsc(" + std::to_string(loaded_channels.size()) + " items)");
}

void save_subscription() {
	resource_lock.lock();
	auto channels_backup = subscribed_channels;
	resource_lock.unlock();

	Document json_root;
	auto &allocator = json_root.GetAllocator();

	json_root.SetObject();
	json_root.AddMember("version", std::to_string(SUBSCRIPTION_VERSION), allocator);

	Value channels(kArrayType);
	for (auto channel : channels_backup) {
		Value cur_json(kObjectType);
		cur_json.AddMember("id", channel.id, allocator);
		cur_json.AddMember("url", channel.url, allocator);
		cur_json.AddMember("icon_url", channel.icon_url, allocator);
		cur_json.AddMember("name", channel.name, allocator);
		cur_json.AddMember("subscriber_count_str", channel.subscriber_count_str, allocator);
		channels.PushBack(cur_json, allocator);
	}
	json_root.AddMember("channels", channels, allocator);

	std::string data = RJson(json_root).dump();

	auto result = atomic_io.save(data);
	if (result.code != 0) {
		logger.warning("subsc/save", result.string + result.error_description, result.code);
	} else {
		logger.info("subsc/save", "subscription saved.");
	}
}

bool subscription_is_subscribed(const std::string &id) {
	resource_lock.lock();
	bool found = false;
	for (auto channel : subscribed_channels) {
		if (channel.valid && channel.id == id) {
			found = true;
			break;
		}
	}
	resource_lock.unlock();
	return found;
}

void subscription_subscribe(const SubscriptionChannel &new_channel) {
	resource_lock.lock();
	bool found = false;
	for (auto &channel : subscribed_channels) {
		if (channel.id == new_channel.id) {
			found = true;
			channel = new_channel;
			break;
		}
	}
	if (!found) {
		subscribed_channels.push_back(new_channel);
	}
	std::sort(subscribed_channels.begin(), subscribed_channels.end(),
	          [](const auto &i, const auto &j) { return i.name < j.name; });
	resource_lock.unlock();
}
void subscription_unsubscribe(const std::string &id) {
	resource_lock.lock();
	std::vector<SubscriptionChannel> new_subscribed_channels;
	for (auto channel : subscribed_channels) {
		if (channel.id != id) {
			new_subscribed_channels.push_back(channel);
		}
	}
	subscribed_channels = new_subscribed_channels;
	resource_lock.unlock();
}

std::vector<SubscriptionChannel> get_valid_subscribed_channels() {
	resource_lock.lock();
	std::vector<SubscriptionChannel> res;
	for (auto &channel : subscribed_channels) {
		if (channel.valid) {
			res.push_back(channel);
		}
	}
	resource_lock.unlock();
	return res;
}

std::vector<SubscriptionChannel> get_oauth_subscribed_channels() {
	std::vector<SubscriptionChannel> result;

	// Wait for internet connection
	u8 wifi_state = *(u8 *)0x1FF81067;
	int wifi_tries = 0;
	while (wifi_state != 2) {
		wifi_tries++;
		if (wifi_tries > 20) {
			logger.error("oauth-subsc", "no internet connection");
			return result;
		}
		logger.info("oauth-subsc", "waiting for internet connection...");
		usleep(1000000);
		wifi_state = *(u8 *)0x1FF81067;
		if (wifi_state == 2) {
			usleep(500000);
		}
	}

	RJson data = OAuth::fetch_browse_data("FEchannels");
	if (!data.is_valid()) {
		logger.error("oauth-subsc", "fetch failed");
		return result;
	}

	std::set<std::string> seen_ids;

	auto contents = data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]["content"]
	                    ["sectionListRenderer"]["contents"];

	for (auto section : contents.array_items()) {
		if (!section.has_key("shelfRenderer")) {
			continue;
		}

		auto items = section["shelfRenderer"]["content"]["verticalListRenderer"]["items"];

		for (auto item : items.array_items()) {
			if (!item.has_key("compactChannelRenderer")) {
				continue;
			}

			auto renderer = item["compactChannelRenderer"];

			if (!renderer.has_key("channelId")) {
				continue;
			}

			SubscriptionChannel channel;
			channel.id = renderer["channelId"].string_value();

			if (seen_ids.count(channel.id)) {
				continue;
			}
			seen_ids.insert(channel.id);

			if (renderer.has_key("navigationEndpoint")) {
				auto browse_id = renderer["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
				if (!browse_id.empty()) {
					channel.url = "https://m.youtube.com/channel/" + browse_id;
				}
			}
			if (channel.url.empty()) {
				channel.url = "https://m.youtube.com/channel/" + channel.id;
			}

			if (renderer.has_key("displayName")) {
				channel.name = youtube_parser::get_text_from_object(renderer["displayName"]);
			}
			if (channel.name.empty() && renderer.has_key("title")) {
				channel.name = youtube_parser::get_text_from_object(renderer["title"]);
			}

			if (renderer.has_key("videoCountText")) {
				channel.subscriber_count_str = youtube_parser::get_text_from_object(renderer["videoCountText"]);
			}

			if (renderer.has_key("thumbnail")) {
				auto thumbnails = renderer["thumbnail"]["thumbnails"];
				if (thumbnails.array_items().size() > 0) {
					channel.icon_url = youtube_parser::get_thumbnail_url_exact(thumbnails, 72);
				}
			}

			channel.valid = is_valid_subscription_channel(channel);
			if (channel.valid && !channel.name.empty()) {
				result.push_back(channel);
			}
		}
	}

	std::sort(result.begin(), result.end(), [](const auto &i, const auto &j) { return i.name < j.name; });

	logger.info("oauth-subsc", "Loaded " + std::to_string(result.size()) + " OAuth channels");
	return result;
}
