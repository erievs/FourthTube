#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"
#include "../oauth/oauth.hpp"

YouTubeHomeResult youtube_load_home_page() {
	YouTubeHomeResult res;

	if (OAuth::is_authenticated()) {
		OAuth::refresh_access_token();
	}

	std::string browse_id = OAuth::is_authenticated() ? "FEwhat_to_watch" : "FEhype_leaderboard";

	std::string post_content;
	if (OAuth::is_authenticated()) {
		post_content =
		    R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "ANDROID_VR", "clientVersion": "1.65.10", "deviceMake": "Oculus", "deviceModel": "Quest 3", "osName": "Android", "osVersion": "14", "androidSdkVersion": "34"}}, "browseId": "$2"})";
	} else {
		post_content =
		    R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "ANDROID", "clientVersion": "19.51.37", "osName": "Android", "osVersion": "14", "androidSdkVersion": "34"}}, "browseId": "$2"})";
	}
	post_content = std::regex_replace(post_content, std::regex("\\$0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("\\$1"), country_code);
	post_content = std::regex_replace(post_content, std::regex("\\$2"), browse_id);

	std::map<std::string, std::string> headers;
	if (OAuth::is_authenticated()) {
		headers["Authorization"] = "Bearer " + OAuth::get_access_token();
	}

	access_and_parse_json(
	    [&]() { return http_post_json(get_innertube_api_url("browse"), post_content, headers); },
	    [&](Document &json_root, RJson yt_result) {
		    res.visitor_data = yt_result["responseContext"]["visitorData"].string_value();

		    if (OAuth::is_authenticated()) {
			    for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
				    if (tab["tabRenderer"]["content"].has_key("sectionListRenderer")) {
					    auto sections = tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items();
					    for (auto section : sections) {
						    if (section.has_key("shelfRenderer")) {
							    for (auto item : section["shelfRenderer"]["content"]["verticalListRenderer"]["items"]
							                         .array_items()) {
								    if (item.has_key("compactVideoRenderer")) {
									    res.videos.push_back(parse_succinct_video(item["compactVideoRenderer"]));
								    }
							    }
						    }
					    }
					    if (tab["tabRenderer"]["content"]["sectionListRenderer"].has_key("continuations")) {
						    auto continuations =
						        tab["tabRenderer"]["content"]["sectionListRenderer"]["continuations"].array_items();
						    for (auto continuation : continuations) {
							    if (continuation.has_key("nextContinuationData")) {
								    res.continue_token =
								        continuation["nextContinuationData"]["continuation"].string_value();
								    break;
							    }
						    }
					    }
				    }
			    }
		    } else {
			    // Unauthenticated: parse FEhype_leaderboard response format
			    for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
				    if (tab["tabRenderer"]["content"].has_key("sectionListRenderer")) {
					    auto sections = tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items();
					    for (auto section : sections) {
						    if (section.has_key("itemSectionRenderer")) {
							    for (auto item : section["itemSectionRenderer"]["contents"].array_items()) {
								    if (item.has_key("elementRenderer")) {
									    auto element = item["elementRenderer"]["newElement"]["type"]["componentType"];
									    if (element.has_key("model") && element["model"].has_key("compactVideoModel")) {
										    auto compact_video =
										        element["model"]["compactVideoModel"]["compactVideoData"];
										    YouTubeVideoSuccinct video;

										    auto on_tap = compact_video["onTap"]["innertubeCommand"]["watchEndpoint"];
										    std::string video_id = on_tap["videoId"].string_value();
										    video.url = "/watch?v=" + video_id;

										    video.title =
										        compact_video["videoData"]["metadata"]["title"].string_value();
										    video.thumbnail_url = youtube_get_video_thumbnail_url_by_id(video_id);

										    video.duration_text =
										        compact_video["videoData"]["thumbnail"]["timestampText"].string_value();
										    video.author = compact_video["videoData"]["metadata"]["byline"]
										                       .string_value(); // Parse accessibility text:
										                                        // parts[4]=views, parts[5]=publish_date
										    std::string access_text = compact_video["accessibilityText"].string_value();
										    std::vector<std::string> parts;
										    size_t start = 0;
										    size_t pos = 0;
										    while ((pos = access_text.find(" - ", start)) != std::string::npos) {
											    parts.push_back(access_text.substr(start, pos - start));
											    start = pos + 3;
										    }
										    if (start < access_text.size()) {
											    parts.push_back(access_text.substr(start));
										    }

										    if (parts.size() >= 6) {
											    video.views_str = parts[4];
											    video.publish_date = parts[5];
										    }

										    res.videos.push_back(video);
									    }
								    }
							    }
						    }
					    }
				    }
			    }
		    }
	    },
	    [&](const std::string &error) {
		    res.error = "[home] " + error;
		    debug_error(res.error);
	    });

	return res;
}
void YouTubeHomeResult::load_more_results() {
	if (!OAuth::is_authenticated()) {
		// Unauthenticated: FEhype_leaderboard returns all data at once, no continuation needed
		return;
	}

	if (continue_token == "") {
		error = "[home] continue token not set";
	}
	if (visitor_data == "") {
		error = "[home] visitor data not set";
	}
	if (error != "") {
		debug_error(error);
		return;
	}

	std::string post_content;
	if (OAuth::is_authenticated()) {
		post_content =
		    R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "ANDROID_VR", "clientVersion": "1.65.10", "deviceMake": "Oculus", "deviceModel": "Quest 3", "osName": "Android", "osVersion": "14", "androidSdkVersion": "34", "visitorData": "$2"}}, "continuation": "$3"})";
	} else {
		// Kept for future feature implementation. Currently unused as unauthenticated requests return early.
		post_content =
		    R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "visitorData": "$2"}}, "continuation": "$3"})";
	}
	post_content = std::regex_replace(post_content, std::regex("\\$0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("\\$1"), country_code);
	post_content = std::regex_replace(post_content, std::regex("\\$2"), visitor_data);
	post_content = std::regex_replace(post_content, std::regex("\\$3"), continue_token);

	continue_token = "";

	std::map<std::string, std::string> headers;
	if (OAuth::is_authenticated()) {
		headers["Authorization"] = "Bearer " + OAuth::get_access_token();
	}

	access_and_parse_json(
	    [&]() { return http_post_json(get_innertube_api_url("browse"), post_content, headers); },
	    [&](Document &json_root, RJson yt_result) {
		    if (yt_result["responseContext"]["visitorData"].string_value() != "") {
			    visitor_data = yt_result["responseContext"]["visitorData"].string_value();
		    }

		    if (yt_result.has_key("continuationContents")) {
			    auto section_continuation = yt_result["continuationContents"]["sectionListContinuation"];

			    for (auto section : section_continuation["contents"].array_items()) {
				    if (section.has_key("shelfRenderer")) {
					    for (auto item :
					         section["shelfRenderer"]["content"]["verticalListRenderer"]["items"].array_items()) {
						    if (item.has_key("compactVideoRenderer")) {
							    videos.push_back(parse_succinct_video(item["compactVideoRenderer"]));
						    }
					    }
				    }
			    }

			    if (section_continuation.has_key("continuations")) {
				    for (auto continuation : section_continuation["continuations"].array_items()) {
					    if (continuation.has_key("nextContinuationData")) {
						    continue_token = continuation["nextContinuationData"]["continuation"].string_value();
						    break;
					    }
				    }
			    }
		    }
	    },
	    [&](const std::string &error) { debug_error((this->error = "[home-c] " + error)); });
}
