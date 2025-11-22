#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"
#include "../oauth/oauth.hpp"

YouTubeHomeResult youtube_load_home_page() {
	YouTubeHomeResult res;

	if (OAuth::is_authenticated()) {
		OAuth::refresh_access_token();
	}

	std::string browse_id = OAuth::is_authenticated() ? "FEwhat_to_watch" : "FEtrending";

	std::string post_content;
	if (OAuth::is_authenticated()) {
		post_content =
		    R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "ANDROID_VR", "clientVersion": "1.65.10", "deviceMake": "Oculus", "deviceModel": "Quest 3", "osName": "Android", "osVersion": "14", "androidSdkVersion": "34"}}, "browseId": "$2"})";
	} else {
		post_content =
		    R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00"}}, "browseId": "$2"})";
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
		    for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
			    if (tab["tabRenderer"]["content"].has_key("sectionListRenderer")) {
				    auto sections = tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items();
				    for (auto section : sections) {
					    if (section.has_key("shelfRenderer")) {
						    for (auto item :
						         section["shelfRenderer"]["content"]["verticalListRenderer"]["items"].array_items()) {
							    if (item.has_key("compactVideoRenderer")) {
								    res.videos.push_back(parse_succinct_video(item["compactVideoRenderer"]));
							    }
						    }
					    } else if (section.has_key("itemSectionRenderer")) {
						    for (auto item : section["itemSectionRenderer"]["contents"].array_items()) {
							    if (item.has_key("videoWithContextRenderer")) {
								    res.videos.push_back(parse_succinct_video(item["videoWithContextRenderer"]));
							    }
						    }
					    } else if (section.has_key("continuationItemRenderer")) {
						    res.continue_token = section["continuationItemRenderer"]["continuationEndpoint"]
						                                ["continuationCommand"]["token"]
						                                    .string_value();
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
	    },
	    [&](const std::string &error) {
		    res.error = "[home] " + error;
		    debug_error(res.error);
	    });

	return res;
}
void YouTubeHomeResult::load_more_results() {
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
				    } else if (section.has_key("itemSectionRenderer")) {
					    for (auto item : section["itemSectionRenderer"]["contents"].array_items()) {
						    if (item.has_key("videoWithContextRenderer")) {
							    videos.push_back(parse_succinct_video(item["videoWithContextRenderer"]));
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
		    } else if (yt_result.has_key("onResponseReceivedActions")) {
			    for (auto action : yt_result["onResponseReceivedActions"].array_items()) {
				    if (!action.has_key("appendContinuationItemsAction")) {
					    continue;
				    }

				    auto continuation_items = action["appendContinuationItemsAction"]["continuationItems"];

				    for (auto item : continuation_items.array_items()) {
					    if (item.has_key("continuationItemRenderer")) {
						    continue_token =
						        item["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
						            .string_value();
					    } else if (item.has_key("compactVideoRenderer")) {
						    videos.push_back(parse_succinct_video(item["compactVideoRenderer"]));
					    } else if (item.has_key("itemSectionRenderer")) {
						    for (auto section_item : item["itemSectionRenderer"]["contents"].array_items()) {
							    if (section_item.has_key("videoWithContextRenderer")) {
								    videos.push_back(parse_succinct_video(section_item["videoWithContextRenderer"]));
							    }
						    }
					    }
				    }
			    }
		    }
	    },
	    [&](const std::string &error) { debug_error((this->error = "[home-c] " + error)); });
}
