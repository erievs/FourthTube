#include <regex>
#include "internal_common.hpp"
#include "proto_builder.hpp"
#include "parser.hpp"
#include <iostream>
#include "../variables.hpp"
#include <iomanip>
#include <algorithm>
#include <random>

static bool extract_player_data(Document &json_root, RJson player_response, YouTubeVideoDetail &res) {
	res.playability_status = player_response["playabilityStatus"]["status"].string_value();
	res.playability_reason = player_response["playabilityStatus"]["reason"].string_value();
	res.is_upcoming = player_response["videoDetails"]["isUpcoming"].bool_value();
	res.livestream_type = player_response["videoDetails"]["isLiveContent"].bool_value()
	                          ? YouTubeVideoDetail::LivestreamType::LIVESTREAM
	                          : YouTubeVideoDetail::LivestreamType::PREMIERE;

	// extract stream formats
	std::vector<RJson> formats;
	for (auto i : player_response["streamingData"]["formats"].array_items()) {
		formats.push_back(i);
	}
	for (auto i : player_response["streamingData"]["adaptiveFormats"].array_items()) {
		formats.push_back(i);
	}

	for (auto &i : formats) {
		i.set_str(json_root, "url",
		          url_decode(i["url"].string_value())
		              .c_str()); // something like %2C still appears in the url, so decode them back
	}

	res.stream_fragment_len = -1;
	res.is_livestream = false;
	std::vector<RJson> audio_formats, video_formats;
	for (auto i : formats) {
		// std::cerr << i["itag"].int_value() << " : " << (i["contentLength"].string_value().size() ? "Yes" : "No") <<
		// std::endl; if (i["contentLength"].string_value() == "") continue;
		if (i.has_key("targetDurationSec")) {
			int new_stream_fragment_len = i["targetDurationSec"].int_value();
			if (res.stream_fragment_len != -1 && res.stream_fragment_len != new_stream_fragment_len) {
				debug_warning("[unexp] diff targetDurationSec for diff streams");
			}
			res.stream_fragment_len = new_stream_fragment_len;
			res.is_livestream = true;
		}
		if (i["approxDurationMs"].string_value() != "") {
			res.duration_ms = stoll(i["approxDurationMs"].string_value());
		}

		if (i["type"].string_value() == "FORMAT_STREAM_TYPE_OTF") {
			continue;
		}
		auto mime_type = i["mimeType"].string_value();
		if (mime_type.substr(0, 5) == "video") {
			// H.264 is virtually the only playable video codec
			if (mime_type.find("avc1") != std::string::npos) {
				video_formats.push_back(i);
			}
		} else if (mime_type.substr(0, 5) == "audio") {
			// We can modify ffmpeg to support opus
			if (mime_type.find("mp4a") != std::string::npos) {
				audio_formats.push_back(i);
			}
		} else {
		} // ???
	}
	// audio
	{
		res.audio_stream_url = "";
		for (auto i : audio_formats) {
			if (i["itag"].int_value() == 140) {
				res.audio_stream_url = i["url"].string_value();
			}
		}

		if (res.audio_stream_url == "" && audio_formats.size()) {
			res.audio_stream_url = audio_formats[0]["url"].string_value();
		}
	}
	// video
	{
		std::map<int, int> itag_to_resolution = {{160, 144}, {133, 240}, {134, 360}, {135, 480}};

		for (auto i : video_formats) {
			int cur_itag = i["itag"].int_value();
			std::string url = i["url"].string_value();
			int height = i["height"].int_value(); // Video resolution height

			// Check if the height matches any value in itag_to_resolution
			int resolution =
			    (std::find_if(itag_to_resolution.begin(), itag_to_resolution.end(),
			                  [&](const auto &pair) { return pair.second == height; }) != itag_to_resolution.end())
			        ? height
			        : (itag_to_resolution.count(cur_itag) ? itag_to_resolution[cur_itag] : 0);

			// Store the stream URL by resolution
			if (resolution > 0) {
				res.video_stream_urls[resolution] = url;
			}
		}
		// both_stream_url : search for itag 18
		for (auto i : video_formats) {
			if (i["itag"].int_value() == 18) {
				res.both_stream_url = i["url"].string_value();
				break;
			}
		}
	}

	// extract caption data
	std::string captions_content =
	    R"({"context": {"client": {"hl": "%0","gl": "%1","clientName": "MWEB","clientVersion": "2.20241202.07.00"}}, "videoId": "%2"})";
	captions_content = std::regex_replace(captions_content, std::regex("%0"), language_code);
	captions_content = std::regex_replace(captions_content, std::regex("%1"), country_code);
	captions_content = std::regex_replace(captions_content, std::regex("%2"), res.id);

	access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("player"), captions_content); },
	                      [&](Document &, RJson mweb_data) {
		                      RJson captions = mweb_data["captions"]["playerCaptionsTracklistRenderer"];

		                      for (auto base_lang : captions["captionTracks"].array_items()) {
			                      YouTubeVideoDetail::CaptionBaseLanguage cur_lang;
			                      cur_lang.name = get_text_from_object(base_lang["name"]);
			                      cur_lang.id = base_lang["languageCode"].string_value();
			                      cur_lang.base_url = base_lang["baseUrl"].string_value();
			                      cur_lang.is_translatable = base_lang["isTranslatable"].bool_value();
			                      res.caption_base_languages.push_back(cur_lang);
			                      logger.info("Caption Data", cur_lang.base_url);
		                      }

		                      for (auto translation_lang : captions["translationLanguages"].array_items()) {
			                      YouTubeVideoDetail::CaptionTranslationLanguage cur_lang;
			                      cur_lang.name = get_text_from_object(translation_lang["languageName"]);
			                      cur_lang.id = translation_lang["languageCode"].string_value();
			                      res.caption_translation_languages.push_back(cur_lang);
		                      }
	                      },
	                      [&](const std::string &error) { debug_error((res.error = "[v-cap-mweb] " + error)); });

	return true;
}

static void extract_like_dislike_counts(RJson buttons, YouTubeVideoDetail &res) {
	res.like_count_str = "0";    // Default to "0" for like count
	res.dislike_count_str = "0"; // Default to "0" for dislike count

	for (auto button : buttons.array_items()) {
		if (button.has_key("slimMetadataToggleButtonRenderer")) { // legacy?
			auto get_text = [](RJson button) -> std::string {
				auto content = get_text_from_object(button["button"]["toggleButtonRenderer"]["defaultText"]);
				if (content.size() && !isdigit(content[0])) {
					return "0"; // Default to "0" if not a number
				}
				return content;
			};

			if (button["slimMetadataToggleButtonRenderer"]["isLike"].bool_value()) {
				res.like_count_str = get_text(button["slimMetadataToggleButtonRenderer"]);
			}
			if (button["slimMetadataToggleButtonRenderer"]["isDislike"].bool_value()) {
				res.dislike_count_str = get_text(button["slimMetadataToggleButtonRenderer"]);
			}
			if (button["slimMetadataToggleButtonRenderer"]["target"]["videoId"].is_valid()) {
				res.id = button["slimMetadataToggleButtonRenderer"]["target"]["videoId"].string_value();
			}
		}

		if (button.has_key("slimMetadataButtonRenderer") &&
		    button["slimMetadataButtonRenderer"]["button"].has_key("segmentedLikeDislikeButtonRenderer")) { // old?
			auto renderer = button["slimMetadataButtonRenderer"]["button"]["segmentedLikeDislikeButtonRenderer"];
			auto get_text = [](RJson button) -> std::string {
				auto text = get_text_from_object(button["toggleButtonRenderer"]["defaultText"]);
				if (text.size() && !isdigit(text[0])) {
					return "0"; // Default to "0" if not a number
				}
				return text;
			};
			res.like_count_str = get_text(renderer["likeButton"]);
			res.dislike_count_str = get_text(renderer["dislikeButton"]);
		}

		if (button.has_key("slimMetadataButtonRenderer") &&
		    button["slimMetadataButtonRenderer"]["button"].has_key("segmentedLikeDislikeButtonViewModel")) {
			auto get_text_from_view_model = [](RJson button) -> std::string {
				auto text = button["slimMetadataButtonRenderer"]["button"]["segmentedLikeDislikeButtonViewModel"]
				                  ["likeButtonViewModel"]["likeButtonViewModel"]["toggleButtonViewModel"]
				                  ["toggleButtonViewModel"]["defaultButtonViewModel"]["buttonViewModel"]["title"]
				                      .string_value();
				if (text.size() && !isdigit(text[0])) {
					return "0"; // Default to "0" if not a number
				}
				return text;
			};
			res.like_count_str = get_text_from_view_model(button);
			res.dislike_count_str = get_text_from_view_model(button); // Update the dislike count similarly
		}
	}
}

static void extract_owner(RJson slimOwnerRenderer, YouTubeVideoDetail &res) {
	res.author.id = slimOwnerRenderer["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
	// !!! res.author.url = slimOwnerRenderer["channelUrl"].string_value();
	res.author.name = slimOwnerRenderer["channelName"].string_value();
	res.author.subscribers = get_text_from_object(slimOwnerRenderer["expandedSubtitle"]);
	res.author.icon_url = get_thumbnail_url_exact(slimOwnerRenderer["thumbnail"]["thumbnails"], 72);
}

static void extract_item(RJson content, YouTubeVideoDetail &res) {
	if (content.has_key("slimVideoMetadataRenderer")) {
		RJson metadata_renderer = content["slimVideoMetadataRenderer"];
		res.title = get_text_from_object(metadata_renderer["title"]);
		res.description = get_text_from_object(metadata_renderer["description"]);
		res.views_str = get_text_from_object(metadata_renderer["expandedSubtitle"]);
		res.publish_date = get_text_from_object(metadata_renderer["dateText"]);
		extract_like_dislike_counts(metadata_renderer["buttons"], res);
		extract_owner(metadata_renderer["owner"]["slimOwnerRenderer"], res);
	} else if (content.has_key("compactAutoplayRenderer")) {
		for (auto j : content["compactAutoplayRenderer"]["contents"].array_items()) {
			if (j.has_key("videoWithContextRenderer")) {
				res.suggestions.push_back(parse_succinct_video(j["videoWithContextRenderer"]));
			}
		}
	} else if (content.has_key("videoWithContextRenderer")) {
		res.suggestions.push_back(parse_succinct_video(content["videoWithContextRenderer"]));
	} else if (content.has_key("continuationItemRenderer")) {
		res.suggestions_continue_token =
		    content["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
	} else if (content.has_key("compactRadioRenderer") || content.has_key("compactPlaylistRenderer")) {
		auto playlist_renderer = content.has_key("compactRadioRenderer") ? content["compactRadioRenderer"]
		                                                                 : content["compactPlaylistRenderer"];

		YouTubePlaylistSuccinct cur_list;
		cur_list.title = get_text_from_object(playlist_renderer["title"]);
		cur_list.video_count_str = get_text_from_object(playlist_renderer["videoCountText"]);
		for (auto thumbnail : playlist_renderer["thumbnail"]["thumbnails"].array_items()) {
			if (thumbnail["url"].string_value().find("/default.jpg") != std::string::npos) {
				cur_list.thumbnail_url = thumbnail["url"].string_value();
			}
		}

		cur_list.url = convert_url_to_mobile(playlist_renderer["shareUrl"].string_value());
		if (!starts_with(cur_list.url, "https://m.youtube.com/watch", 0)) {
			if (starts_with(cur_list.url, "https://m.youtube.com/playlist?", 0)) {
				auto params = parse_parameters(
				    cur_list.url.substr(std::string("https://m.youtube.com/playlist?").size(), cur_list.url.size()));
				auto playlist_id = params["list"];
				auto video_id = get_video_id_from_thumbnail_url(cur_list.thumbnail_url);
				cur_list.url = "https://m.youtube.com/watch?v=" + video_id + "&list=" + playlist_id;
			} else {
				debug_warning("unknown playlist url");
				return;
			}
		}

		res.suggestions.push_back(YouTubeSuccinctItem(cur_list));
	}
}

std::string format_count(int count) {
	std::stringstream ss;
	if (count >= 1000000) {
		ss << std::fixed << std::setprecision(1) << (count / 1000000.0) << "M";
	} else if (count >= 1000) {
		ss << std::fixed << std::setprecision(1) << (count / 1000.0) << "k";
	} else {
		ss << count;
	}
	return ss.str();
}

std::string format_with_commas(int number) {
	std::string num_str = std::to_string(number);
	std::string result;
	int count = 0;
	std::string::size_type length = num_str.length();

	for (auto it = num_str.rbegin(); it != num_str.rend(); ++it) {
		result += *it;
		++count;
		if (count % 3 == 0 && count != static_cast<int>(length)) {
			result += ',';
		}
	}

	std::reverse(result.begin(), result.end());
	return result;
}

void fetch_like_dislike_counts(const std::string &video_id, YouTubeVideoDetail &res) {
	std::string api_url = "https://returnyoutubedislikeapi.com/votes?videoId=" + video_id;

	std::map<std::string, std::string> headers;
	auto response = http_get(api_url, headers);

	logger.info("Raw JSON response", response.second);

	if (response.first) {
		rapidjson::Document document;
		std::string error;

		RJson data = RJson::parse(document, response.second.c_str(), error);

		if (data.is_valid()) {

			if (data.has_key("likes")) {
				int likes = data["likes"].int_value();
				res.like_count_str = var_full_dislike_like_count ? format_with_commas(likes) : format_count(likes);
				logger.info("Like count", res.like_count_str);
			}

			if (data.has_key("dislikes")) {
				int dislikes = data["dislikes"].int_value();
				res.dislike_count_str =
				    var_full_dislike_like_count ? format_with_commas(dislikes) : format_count(dislikes);
				logger.info("Dislike count", res.dislike_count_str);
			}
		} else {
			res.like_count_str = "Error";
			res.dislike_count_str = "Error";
			std::cerr << "JSON Parsing Error: " << error << std::endl;
		}
	} else {
		res.like_count_str = "N/A";
		res.dislike_count_str = "N/A";
	}
}

static void extract_metadata(RJson data, YouTubeVideoDetail &res) {
	{
		auto contents = data["contents"]["singleColumnWatchNextResults"]["results"]["results"]["contents"];
		for (auto content : contents.array_items()) {
			if (content.has_key("itemSectionRenderer")) {
				for (auto i : content["itemSectionRenderer"]["contents"].array_items()) {
					extract_item(i, res);
				}
			} else if (content.has_key("slimVideoMetadataSectionRenderer")) {
				for (auto i : content["slimVideoMetadataSectionRenderer"]["contents"].array_items()) {
					if (i.has_key("slimVideoInformationRenderer")) {
						res.title = get_text_from_object(i["slimVideoInformationRenderer"]["title"]);
					}
					if (i.has_key("slimOwnerRenderer")) {
						extract_owner(i["slimOwnerRenderer"], res);
					}
					if (i.has_key("slimVideoDescriptionRenderer")) {
						res.description = get_text_from_object(i["slimVideoDescriptionRenderer"]["description"]);
					}
				}
			}
		}
	}

	fetch_like_dislike_counts(res.id, res);

	RJson playlist_object = data["contents"]["singleColumnWatchNextResults"]["playlist"]["playlist"];
	if (playlist_object.is_valid()) {
		res.playlist.id = playlist_object["playlistId"].string_value();
		res.playlist.selected_index = -1;
		res.playlist.author_name = get_text_from_object(playlist_object["ownerName"]);
		res.playlist.title = playlist_object["title"].string_value();
		res.playlist.total_videos = playlist_object["totalVideos"].int_value();
		for (auto playlist_item : playlist_object["contents"].array_items()) {
			if (playlist_item.has_key("playlistPanelVideoRenderer")) {
				auto renderer = playlist_item["playlistPanelVideoRenderer"];
				YouTubeVideoSuccinct cur_video = parse_succinct_video(renderer);
				if (cur_video.url != "") {
					cur_video.url += "&list=" + res.playlist.id;
				}
				if (renderer["selected"].bool_value()) {
					res.playlist.selected_index = res.playlist.videos.size();
				}
				res.playlist.videos.push_back(cur_video);
			}
		}
	}
	res.comment_continue_type = -1;
	res.comments_disabled = true;
	for (auto i : data["engagementPanels"].array_items()) {
		for (auto j :
		     i["engagementPanelSectionListRenderer"]["content"]["sectionListRenderer"]["continuations"].array_items()) {
			if (j.has_key("reloadContinuationData")) {
				res.comment_continue_token = j["reloadContinuationData"]["continuation"].string_value();
				res.comment_continue_type = 0;
				res.comments_disabled = false;
			}
		}
		for (auto j :
		     i["engagementPanelSectionListRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
			for (auto k : j["itemSectionRenderer"]["contents"].array_items()) {
				if (k.has_key("continuationItemRenderer")) {
					res.comment_continue_token =
					    k["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
					        .string_value();
					res.comment_continue_type = 1;
					res.comments_disabled = false;
				}
			}
		}
		for (auto j :
		     i["engagementPanelSectionListRenderer"]["content"]["structuredDescriptionContentRenderer"]["items"]
		         .array_items()) {
			if (j["expandableVideoDescriptionBodyRenderer"].has_key("descriptionBodyText")) {
				res.description =
				    get_text_from_object(j["expandableVideoDescriptionBodyRenderer"]["descriptionBodyText"]);
			}
			if (j["expandableVideoDescriptionBodyRenderer"].has_key("attributedDescriptionBodyText")) {
				res.description =
				    j["expandableVideoDescriptionBodyRenderer"]["attributedDescriptionBodyText"]["content"]
				        .string_value();
			}
			if (j.has_key("videoDescriptionHeaderRenderer")) {
				res.publish_date = get_text_from_object(j["videoDescriptionHeaderRenderer"]["publishDate"]);
				res.views_str = get_text_from_object(j["videoDescriptionHeaderRenderer"]["views"]);
			}
		}
	}
}

static std::string extractVisitorData() {
	std::string url = "https://www.youtube.com/sw.js_data";
	std::map<std::string, std::string> headers = {{"Origin", "https://www.youtube.com"},
	                                              {"Referer", "https://www.youtube.com/"}};
	auto response = http_get(url, headers);

	if (response.first) {
		std::string json_content = response.second;
		const std::string unwanted_prefix = ")]}'\n";
		if (json_content.find(unwanted_prefix) == 0) {
			json_content = json_content.substr(unwanted_prefix.size());
		}

		rapidjson::Document document;
		std::string error;
		RJson data = RJson::parse(document, json_content.c_str(), error);

		if (data.is_valid()) {
			std::string visitor_data = data[static_cast<size_t>(0)][static_cast<size_t>(2)][static_cast<size_t>(0)]
			                               [static_cast<size_t>(0)][static_cast<size_t>(13)]
			                                   .string_value();
			logger.info("Visitor Data", "Fetched new visitor data: " + visitor_data);
			return visitor_data;
		} else {
			logger.error("Visitor Data", "JSON Parsing Error: " + error);
			return "";
		}
	} else {
		logger.error("Visitor Data", "Failed to fetch visitor data");
		return "";
	}
}

std::string randomVisitorData(const std::string &country_code) {
	ProtoBuilder pbE2, pbE, pb;
	pbE2.string(2, "");
	pbE2.varint(4, rand() % 256);
	pbE.string(1, country_code);
	pbE.bytes(2, pbE2.toBytes());

	std::string nonce;
	static const char alphanum[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	for (int i = 0; i < 11; ++i) {
		nonce += alphanum[rand() % (sizeof(alphanum) - 1)];
	}

	pb.string(1, nonce);
	pb.varint(5, std::time(nullptr) - (rand() % 600000));
	pb.bytes(6, pbE.toBytes());

	return pb.toUrlencodedBase64();
}

std::string getVisitorData(const std::string &country_code) {
	std::string visitor_data = extractVisitorData();
	if (visitor_data.empty()) {
		logger.warning("Visitor Data", "Using random visitor data");
		visitor_data = randomVisitorData(country_code);
	}
	return visitor_data;
}

YouTubeVideoDetail youtube_load_video_page(std::string url) {
	YouTubeVideoDetail res;

	res.id = youtube_get_video_id_by_url(url);
	if (res.id.empty()) {
		res.error = "Not a video URL: " + url;
		return res;
	}

	std::string playlist_id = youtube_get_playlist_id_by_url(url);

	std::string video_content;
	std::string visitor_data = getVisitorData(country_code);

	if (var_player_response == 0) {
		video_content =
		    R"({"videoId": "%0", %1"context": {"client": {"hl": "%2","gl": "%3","clientName": "IOS","clientVersion": "20.10.4","deviceMake": "Apple","deviceModel": "iPhone16,2","osName": "iPhone","userAgent": "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)\"","osVersion": "18.3.2.22D82", "visitorData": "%4"}}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}}})";
	} else if (var_player_response == 1) {
		// For testing. By default iOS is used
		video_content =
		    R"({"videoId": "%0", %1"context": {"client": {"hl": "%2","gl": "%3","clientName": "ANDROID_VR","clientVersion": "1.62.27","deviceMake": "Oculus","deviceModel": "Quest 3","androidSdkVersion": "32","osName": "Android", "userAgent": "com.google.android.apps.youtube.vr.oculus/1.62.27 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip\"","osVersion": "12L", "visitorData": "%4"}}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}}})";
	} else if (var_player_response == 2) {
		// Unused client in InnerTube. Has some similar quirks to iOS, but might still be useful.
		// Yes, we identify as an iPhone in some places. This is intentional and it works. Don't ask why.
		video_content =
		    R"({"videoId": "%0", %1"context": {"client": {"hl": "%2","gl": "%3","clientName": "VISIONOS","clientVersion": "0.1","deviceMake": "Apple","deviceModel": "iPhone16,2","osName": "iPhone","userAgent": "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)\"","osVersion": "2.2.22N842", "visitorData": "%4"}}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}}})";
	} else {
		// User messed with something. Fix that.
		logger.error("appdata", "Invalid value for player_response has been set. Falling back to 0 (iOS)!");
		video_content =
		    R"({"videoId": "%0", %1"context": {"client": {"hl": "%2","gl": "%3","clientName": "IOS","clientVersion": "20.10.4","deviceMake": "Apple","deviceModel": "iPhone16,2","osName": "iPhone","userAgent": "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)\"","osVersion": "18.3.2.22D82", "visitorData": "%4"}}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}}})";
		var_player_response = 0;
		misc_tasks_request(TASK_SAVE_SETTINGS);
	}
	video_content = std::regex_replace(video_content, std::regex("%0"), res.id);
	video_content = std::regex_replace(video_content, std::regex("%1"),
	                                   playlist_id.empty() ? "" : "\"playlistId\": \"" + playlist_id + "\", ");
	video_content = std::regex_replace(video_content, std::regex("%2"), language_code);
	video_content = std::regex_replace(video_content, std::regex("%3"), country_code);
	video_content = std::regex_replace(video_content, std::regex("%4"), visitor_data);

	std::string post_content =
	    R"({"videoId": "%0", %1"context": {"client": {"hl": "%2","gl": "%3","clientName": "MWEB","clientVersion": "2.20241202.07.00"}}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}}})";
	post_content = std::regex_replace(post_content, std::regex("%0"), res.id);
	post_content = std::regex_replace(post_content, std::regex("%1"),
	                                  playlist_id.empty() ? "" : "\"playlistId\": \"" + playlist_id + "\", ");
	post_content = std::regex_replace(post_content, std::regex("%2"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%3"), country_code);

	std::string urls[2] = {get_innertube_api_url("next"), get_innertube_api_url("player")};

	debug_info("accessing(multi)...");
	std::vector<NetworkResult> results;
	bool success = true;
	{
		std::vector<HttpRequest> requests;
		requests.push_back(http_post_json_request(urls[0], post_content));
		requests.push_back(http_post_json_request(urls[1], video_content));

		results = thread_network_session_list.perform(requests);
		for (int i = 0; i < 2; i++) {
			if (results[i].fail) {
				res.error = "[v-#" + std::to_string(i) + "] Network request failed";
				debug_error(res.error);
				success = false;
			}
		}
	}

	if (success) {
		for (int i = 0; i < 2; i++) {
			if (!results[i].data.empty()) {
				results[i].data.push_back('\0');
				parse_json_destructive((char *)&results[i].data[0],
				                       [&](Document &json_root, RJson data) {
					                       if (i == 0) {
						                       extract_metadata(data, res);
					                       } else {
						                       extract_player_data(json_root, data, res);
					                       }
				                       },
				                       [&](const std::string &error) {
					                       res.error = "[v-#" + std::to_string(i) + "] " + error;
					                       debug_error(res.error);
				                       });
			} else {
				res.error = "Empty response data for URL index: " + std::to_string(i);
				debug_error(res.error);
				success = false;
			}
		}
	}

	if (res.id != "") {
		res.succinct_thumbnail_url = youtube_get_video_thumbnail_url_by_id(res.id);
	}
	if (res.title != "" && res.id != "") {
		HistoryVideo video;
		video.id = res.id;
		video.title = res.title;
		video.author_name = res.author.name;
		video.length_text = Util_convert_seconds_to_time((double)res.duration_ms / 1000);
		video.my_view_count = 1;
		video.last_watch_time = time(NULL);
		add_watched_video(video);
		misc_tasks_request(TASK_SAVE_HISTORY);
	}

	if (success) {
		debug_info(res.title.empty() ? "preason: " + res.playability_reason : res.title);
	}

	return res;
}

void YouTubeVideoDetail::load_more_suggestions() {
	if (suggestions_continue_token == "") {
		error = "suggestion continue token empty";
		return;
	}

	// POST to get more results
	std::string post_content =
	    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")" +
	    suggestions_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);

	access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("next"), post_content); },
	                      [&](Document &, RJson yt_result) {
		                      suggestions_continue_token = "";
		                      for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) {
			                      for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
				                      extract_item(j, *this);
			                      }
		                      }
	                      },
	                      [&](const std::string &error) { debug_error((this->error = "[v-sug+] " + error)); });
}

YouTubeVideoDetail::Comment extract_comment_from_comment_renderer(RJson comment_renderer, int thumbnail_height) {
	YouTubeVideoDetail::Comment cur_comment;
	// get the icon of the author with minimum size
	cur_comment.id = comment_renderer["commentId"].string_value();
	cur_comment.content = get_text_from_object(comment_renderer["contentText"]);
	cur_comment.reply_num =
	    comment_renderer["replyCount"].int_value(); // RJson.int_value() defaults to zero, so... it works
	cur_comment.author.id = comment_renderer["authorEndpoint"]["browseEndpoint"]["browseId"].string_value();
	// !!! cur_comment.author.url = "https://m.youtube.com" +
	// comment_renderer["authorEndpoint"]["browseEndpoint"]["canonicalBaseUrl"].string_value();
	cur_comment.author.name = get_text_from_object(comment_renderer["authorText"]);
	cur_comment.publish_date = get_text_from_object(comment_renderer["publishedTimeText"]);
	cur_comment.upvotes_str = get_text_from_object(comment_renderer["voteCount"]);
	cur_comment.author.icon_url =
	    get_thumbnail_url_exact(comment_renderer["authorThumbnail"]["thumbnails"], thumbnail_height);
	return cur_comment;
}
void YouTubeVideoDetail::load_more_comments() {
	if (comment_continue_type == -1) {
		error = "No more comments available";
		return;
	}

	auto parse_comment_thread_renderer = [&](RJson comment_thread_renderer) {
		auto cur_comment = extract_comment_from_comment_renderer(
		    comment_thread_renderer["commentThreadRenderer"]["comment"]["commentRenderer"], 48);
		// get the icon of the author with minimum size
		for (auto i : comment_thread_renderer["commentThreadRenderer"]["replies"]["commentRepliesRenderer"]["contents"]
		                  .array_items()) {
			if (i.has_key("continuationItemRenderer")) {
				cur_comment.replies_continue_token =
				    i["continuationItemRenderer"]["button"]["buttonRenderer"]["command"]["continuationCommand"]["token"]
				        .string_value();
			}
		}
		return cur_comment;
	};

	if (comment_continue_type == 0) {
		access_and_parse_json(
		    [&]() {
			    return http_get("https://m.youtube.com/watch_comment?action_get_comments=1&pbj=1&ctoken=" +
			                        comment_continue_token,
			                    {{"X-YouTube-Client-Version", "2.20210714.00.00"}, {"X-YouTube-Client-Name", "2"}});
		    },
		    [&](Document &, RJson yt_result) {
			    comment_continue_type = -1;
			    comment_continue_token = "";
			    for (auto i : yt_result.array_items()) {
				    for (auto comment :
				         i["response"]["continuationContents"]["commentSectionContinuation"]["items"].array_items()) {
					    if (comment.has_key("commentThreadRenderer")) {
						    comments.push_back(parse_comment_thread_renderer(comment));
					    }
				    }
				    for (auto j : i["response"]["continuationContents"]["commentSectionContinuation"]["continuations"]
				                      .array_items()) {
					    if (j.has_key("nextContinuationData")) {
						    comment_continue_token = j["nextContinuationData"]["continuation"].string_value();
						    comment_continue_type = 0;
					    }
				    }
			    }
		    },
		    [&](const std::string &error) { debug_error((this->error = "[v-com+0] " + error)); });
	} else {
		std::string post_content =
		    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")" +
		    comment_continue_token + "\"}";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);

		access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("next"), post_content); },
		                      [&](Document &, RJson yt_result) {
			                      comment_continue_type = -1;
			                      comment_continue_token = "";
			                      for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) {
				                      RJson continuation_items =
				                          i.has_key("reloadContinuationItemsCommand")
				                              ? i["reloadContinuationItemsCommand"]["continuationItems"]
				                              : i["appendContinuationItemsAction"]["continuationItems"];

				                      for (auto comment : continuation_items.array_items()) {
					                      if (comment.has_key("commentThreadRenderer")) {
						                      comments.push_back(parse_comment_thread_renderer(comment));
					                      }
					                      if (comment.has_key("continuationItemRenderer")) {
						                      comment_continue_token =
						                          comment["continuationItemRenderer"]["continuationEndpoint"]
						                                 ["continuationCommand"]["token"]
						                                     .string_value();
						                      comment_continue_type = 1;
					                      }
				                      }
			                      }
		                      },
		                      [&](const std::string &error) { debug_error((this->error = "[v-com+1] " + error)); });
	}
}

void YouTubeVideoDetail::Comment::load_more_replies() {
	if (!this->has_more_replies()) {
		debug_caution("load_more_replies on a comment that has no replies to load");
		return;
	}

	std::string post_content =
	    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")" +
	    replies_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);

	access_and_parse_json(
	    [&]() { return http_post_json(get_innertube_api_url("next"), post_content); },
	    [&](Document &, RJson yt_result) {
		    replies_continue_token = "";
		    for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) {
			    for (auto item : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
				    if (item.has_key("commentRenderer")) {
					    replies.push_back(extract_comment_from_comment_renderer(item["commentRenderer"], 32));
				    }
				    if (item.has_key("continuationItemRenderer")) {
					    replies_continue_token = item["continuationItemRenderer"]["button"]["buttonRenderer"]["command"]
					                                 ["continuationCommand"]["token"]
					                                     .string_value();
				    }
			    }
		    }
	    },
	    [&](const std::string &error) { debug_error((this->error = "[v-rep+] " + error)); });
}

void YouTubeVideoDetail::load_caption(const std::string &base_lang_id, const std::string &translation_lang_id) {
	int base_lang_index = -1;
	for (size_t i = 0; i < caption_base_languages.size(); i++) {
		if (caption_base_languages[i].id == base_lang_id) {
			base_lang_index = i;
			break;
		}
	}
	if (base_lang_index == -1) {
		debug_error("[caption] unknown base language");
		return;
	}

	int translation_lang_index = -1;
	if (translation_lang_id != "") {
		for (size_t i = 0; i < caption_translation_languages.size(); i++) {
			if (caption_translation_languages[i].id == translation_lang_id) {
				translation_lang_index = i;
				break;
			}
		}
		if (translation_lang_index == -1) {
			debug_error("[caption] unknown translation language");
			return;
		}
	}

	std::string url = "https://m.youtube.com" + caption_base_languages[base_lang_index].base_url;
	if (translation_lang_id != "") {
		url += "&tlang=" + translation_lang_id;
	}
	url += "&fmt=json3&xorb=2&xobt=3&xovt=3"; // the meanings of xorb, xobt, xovt are unknown, and these three
	                                          // parameters seem to be unnecessary

	access_and_parse_json([&]() { return http_get(url); },
	                      [&](Document &, RJson yt_result) {
		                      std::vector<YouTubeVideoDetail::CaptionPiece> cur_caption;
		                      for (auto caption_piece : yt_result["events"].array_items()) {
			                      if (!caption_piece.has_key("segs")) {
				                      continue;
			                      }
			                      YouTubeVideoDetail::CaptionPiece cur_caption_piece;
			                      cur_caption_piece.start_time = caption_piece["tStartMs"].int_value() / 1000.0;
			                      cur_caption_piece.end_time =
			                          cur_caption_piece.start_time + caption_piece["dDurationMs"].int_value() / 1000.0;
			                      for (auto seg : caption_piece["segs"].array_items()) {
				                      cur_caption_piece.content += seg["utf8"].string_value();
			                      }

			                      cur_caption.push_back(cur_caption_piece);
		                      }
		                      caption_data[{base_lang_id, translation_lang_id}] = cur_caption;
	                      },
	                      [&](const std::string &error) { debug_error((this->error = "[v-cap+] " + error)); });
}
