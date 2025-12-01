#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"

static RJson get_initial_data(Document &json_root, const std::string &html) {
	RJson res;
	if (fast_extract_initial(json_root, html, "ytInitialData", res)) {
		return res;
	}
	res = get_succeeding_json_regexes(
	    json_root, html, {"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]", "ytInitialData\\s*=\\s*['\\{]"});
	if (!res.is_valid()) {
		return get_error_json("did not match any of the ytInitialData regexes");
	}
	return res;
}

static void parse_channel_data(RJson data, YouTubeChannelDetail &res) {
	std::string channel_name = "stub channel name";

	auto metadata_renderer = data["metadata"]["channelMetadataRenderer"];
	res.name = metadata_renderer["title"].string_value();
	if (data["header"].has_key("c4TabbedHeaderRenderer")) {
		res.subscriber_count_str =
		    get_text_from_object(data["header"]["c4TabbedHeaderRenderer"]["subscriberCountText"]);
		res.banner_url = get_thumbnail_url_exact(data["header"]["c4TabbedHeaderRenderer"]["banner"]["thumbnails"], 320);
		res.icon_url = get_thumbnail_url_closest(data["header"]["c4TabbedHeaderRenderer"]["avatar"]["thumbnails"], 88);
	} else {
		auto model = data["header"]["pageHeaderRenderer"]["content"]["pageHeaderViewModel"];
		res.banner_url = get_thumbnail_url_exact(model["banner"]["imageBannerViewModel"]["image"]["sources"], 320);
		res.icon_url = get_thumbnail_url_closest(
		    model["image"]["decoratedAvatarViewModel"]["avatar"]["avatarViewModel"]["image"]["sources"], 88);
		auto tmp = model["metadata"]["contentMetadataViewModel"]["metadataRows"].array_items();
		if (tmp.size()) {
			tmp = tmp[1]["metadataParts"].array_items();
			if (tmp.size() >= 1) {
				res.subscriber_count_str = tmp[0]["text"]["content"].string_value();
			}
		}
	}
	res.id = metadata_renderer["externalId"].string_value();
	res.url = "https://m.youtube.com/channel/" + metadata_renderer["externalId"].string_value();
	res.description = metadata_renderer["description"].string_value();

	for (auto tab : data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
		std::string tab_url =
		    tab["tabRenderer"]["endpoint"]["commandMetadata"]["webCommandMetadata"]["url"].string_value();

		bool is_streams_tab = ends_with(tab_url, "/streams");
		bool is_videos_tab = ends_with(tab_url, "/videos");
		bool is_shorts_tab = ends_with(tab_url, "/shorts");

		if (tab["tabRenderer"]["content"].has_key("richGridRenderer")) {
			auto rich_grid = tab["tabRenderer"]["content"]["richGridRenderer"];
			if (rich_grid.has_key("header") && rich_grid["header"].has_key("feedFilterChipBarRenderer")) {
				auto chips = rich_grid["header"]["feedFilterChipBarRenderer"]["contents"].array_items();
				if (chips.size() >= 3) {
					if (is_videos_tab) {
						res.video_sort_token_newest =
						    chips[0]["chipCloudChipRenderer"]["navigationEndpoint"]["continuationCommand"]["token"]
						        .string_value();
						res.video_sort_token_popular =
						    chips[1]["chipCloudChipRenderer"]["navigationEndpoint"]["continuationCommand"]["token"]
						        .string_value();
						res.video_sort_token_oldest =
						    chips[2]["chipCloudChipRenderer"]["navigationEndpoint"]["continuationCommand"]["token"]
						        .string_value();

						// Detect which sort is currently selected
						for (size_t i = 0; i < chips.size() && i < 3; i++) {
							if (chips[i]["chipCloudChipRenderer"]["isSelected"].bool_value()) {
								res.current_video_sort_type = i;
								break;
							}
						}
					} else if (is_shorts_tab) {
						res.shorts_sort_token_newest =
						    chips[0]["chipCloudChipRenderer"]["navigationEndpoint"]["continuationCommand"]["token"]
						        .string_value();
						res.shorts_sort_token_popular =
						    chips[1]["chipCloudChipRenderer"]["navigationEndpoint"]["continuationCommand"]["token"]
						        .string_value();
						res.shorts_sort_token_oldest =
						    chips[2]["chipCloudChipRenderer"]["navigationEndpoint"]["continuationCommand"]["token"]
						        .string_value();

						// Detect which sort is currently selected
						for (size_t i = 0; i < chips.size() && i < 3; i++) {
							if (chips[i]["chipCloudChipRenderer"]["isSelected"].bool_value()) {
								res.current_shorts_sort_type = i;
								break;
							}
						}
					}
				}
			}

			for (auto i : tab["tabRenderer"]["content"]["richGridRenderer"]["contents"].array_items()) {
				if (i.has_key("continuationItemRenderer")) {
					std::string token =
					    i["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
					        .string_value();
					if (is_streams_tab) {
						res.streams_continue_token = token;
					} else if (is_videos_tab) {
						res.videos_continue_token = token;
					} else if (is_shorts_tab) {
						res.shorts_continue_token = token;
					}
				} else if (i["richItemRenderer"]["content"].has_key("compactVideoRenderer")) {
					auto video = parse_succinct_video(i["richItemRenderer"]["content"]["compactVideoRenderer"]);
					if (is_streams_tab) {
						res.streams.push_back(video);
					} else if (is_videos_tab) {
						res.videos.push_back(video);
					} else if (is_shorts_tab) {
						res.shorts.push_back(video);
					}
				} else if (i["richItemRenderer"]["content"].has_key("videoWithContextRenderer")) {
					auto video = parse_succinct_video(i["richItemRenderer"]["content"]["videoWithContextRenderer"]);
					if (is_streams_tab) {
						res.streams.push_back(video);
					} else if (is_videos_tab) {
						res.videos.push_back(video);
					} else if (is_shorts_tab) {
						res.shorts.push_back(video);
					}
				} else if (i["richItemRenderer"]["content"].has_key("videoRenderer")) {
					auto video = parse_succinct_video(i["richItemRenderer"]["content"]["videoRenderer"]);
					if (is_streams_tab) {
						res.streams.push_back(video);
					} else if (is_videos_tab) {
						res.videos.push_back(video);
					} else if (is_shorts_tab) {
						res.shorts.push_back(video);
					}
				} else if (i["richItemRenderer"]["content"].has_key("shortsLockupViewModel")) {

					auto shorts_data = i["richItemRenderer"]["content"]["shortsLockupViewModel"];
					YouTubeVideoSuccinct short_video;

					std::string video_id;
					if (shorts_data["onTap"]["innertubeCommand"].has_key("reelWatchEndpoint")) {
						video_id =
						    shorts_data["onTap"]["innertubeCommand"]["reelWatchEndpoint"]["videoId"].string_value();
					}

					if (video_id.empty() && shorts_data["onTap"]["innertubeCommand"].has_key("commandMetadata")) {
						std::string url =
						    shorts_data["onTap"]["innertubeCommand"]["commandMetadata"]["webCommandMetadata"]["url"]
						        .string_value();
						if (url.find("/shorts/") != std::string::npos) {
							video_id = url.substr(url.find("/shorts/") + 8);
							size_t param_pos = video_id.find('?');
							if (param_pos != std::string::npos) {
								video_id = video_id.substr(0, param_pos);
							}
						}
					}

					if (!video_id.empty()) {
						short_video.url = "https://m.youtube.com/watch?v=" + video_id;
					}

					if (shorts_data.has_key("overlayMetadata")) {
						auto overlay_metadata = shorts_data["overlayMetadata"];

						if (overlay_metadata.has_key("primaryText") &&
						    overlay_metadata["primaryText"].has_key("content")) {
							short_video.title = overlay_metadata["primaryText"]["content"].string_value();
						}

						if (overlay_metadata.has_key("secondaryText") &&
						    overlay_metadata["secondaryText"].has_key("content")) {
							short_video.views_str = overlay_metadata["secondaryText"]["content"].string_value();
						}
					}

					if (shorts_data["thumbnail"]["sources"].array_items().size() > 0) {
						std::string original_thumbnail_url =
						    shorts_data["thumbnail"]["sources"].array_items()[0]["url"].string_value();

						// Convert webp format to jpg if necessary
						std::string converted_thumbnail_url = convert_webp_thumbnail_to_jpg(original_thumbnail_url);

						std::string video_id = get_video_id_from_thumbnail_url(converted_thumbnail_url);
						if (!video_id.empty()) {
							short_video.thumbnail_url = youtube_get_video_thumbnail_url_by_id(video_id);
						} else {
							short_video.thumbnail_url = converted_thumbnail_url;
						}
					}

					short_video.duration_text = "";
					short_video.author = res.name;

					if (is_shorts_tab) {
						res.shorts.push_back(short_video);
					}
				} else {
					debug_warning("unknown item found in channel videos/streams/shorts");
				}
			}
		}
		tab_url = tab["tabRenderer"]["endpoint"]["commandMetadata"]["webCommandMetadata"]["url"].string_value();
		if (ends_with(tab_url, "/playlists")) {
			res.playlist_tab_browse_id = tab["tabRenderer"]["endpoint"]["browseEndpoint"]["browseId"].string_value();
			res.playlist_tab_params = tab["tabRenderer"]["endpoint"]["browseEndpoint"]["params"].string_value();
		}
	}
}

YouTubeChannelDetail youtube_load_channel_page(std::string url_or_id) {
	YouTubeChannelDetail res;

	if (starts_with(url_or_id, "http://") || starts_with(url_or_id, "https://")) {
		std::string &url = url_or_id;
		res.url_original = url;

		url = convert_url_to_mobile(url);

		// append "/videos" at the end of the url
		{
			bool ok = false;
			for (auto pattern : std::vector<std::string>{"https://m.youtube.com/channel/", "https://m.youtube.com/c/",
			                                             "https://m.youtube.com/user/", "https://m.youtube.com/@"}) {
				if (url.substr(0, pattern.size()) == pattern) {
					url = url.substr(pattern.size(), url.size());
					auto next_slash = std::find(url.begin(), url.end(), '/');
					url = pattern + std::string(url.begin(), next_slash) + "/videos";
					ok = true;
					break;
				}
			}
			if (!ok) {
				res.error = "invalid URL : " + url;
				return res;
			}
		}

		auto result = http_get(url);
		if (!result.first) {
			debug_error((res.error = "[ch-id] " + result.second));
		} else {
			auto html = result.second;
			if (!html.size()) {
				res.error = "[ch-id] html empty";
				return res;
			}
			Document json_root;
			parse_channel_data(get_initial_data(json_root, html), res);
		}
	} else {
		std::string &id = url_or_id;
		res.url_original = "https://m.youtube.com/channel/" + id;

		std::string post_content =
		    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00"}}, "browseId": "%2", "params":"EgZ2aWRlb3PyBgQKAjoA"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), id);

		access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
		                      [&](Document &, RJson json) { parse_channel_data(json, res); },
		                      [&](const std::string &error) {
			                      res.error = "[ch-id] " + error;
			                      debug_error(res.error);
		                      });
	}
	return res;
}
YouTubeChannelDetail youtube_load_channel_streams_page(std::string url_or_id) {
	YouTubeChannelDetail res;

	if (starts_with(url_or_id, "http://") || starts_with(url_or_id, "https://")) {
		std::string &url = url_or_id;
		res.url_original = url;

		url = convert_url_to_mobile(url);

		// append "/streams" at the end of the url
		{
			bool ok = false;
			for (auto pattern : std::vector<std::string>{"https://m.youtube.com/channel/", "https://m.youtube.com/c/",
			                                             "https://m.youtube.com/user/", "https://m.youtube.com/@"}) {
				if (url.substr(0, pattern.size()) == pattern) {
					url = url.substr(pattern.size(), url.size());
					auto next_slash = std::find(url.begin(), url.end(), '/');
					url = pattern + std::string(url.begin(), next_slash) + "/streams";
					ok = true;
					break;
				}
			}
			if (!ok) {
				res.error = "invalid URL : " + url;
				return res;
			}
		}

		auto result = http_get(url);
		if (!result.first) {
			debug_error((res.error = "[ch-streams] " + result.second));
		} else {
			auto html = result.second;
			if (!html.size()) {
				res.error = "[ch-streams] html empty";
				return res;
			}
			Document json_root;
			parse_channel_data(get_initial_data(json_root, html), res);
			res.streams_loaded = true;
		}
	} else {
		std::string &id = url_or_id;
		res.url_original = "https://m.youtube.com/channel/" + id;

		std::string post_content =
		    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00"}}, "browseId": "%2", "params":"EgdzdHJlYW1z8gYECgJ6AA%3D%3D"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), id);

		access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
		                      [&](Document &, RJson json) {
			                      parse_channel_data(json, res);
			                      res.streams_loaded = true;
		                      },
		                      [&](const std::string &error) {
			                      res.error = "[ch-streams] " + error;
			                      debug_error(res.error);
		                      });
	}
	return res;
}
YouTubeChannelDetail youtube_load_channel_shorts_page(std::string url_or_id) {
	YouTubeChannelDetail res;

	if (starts_with(url_or_id, "http://") || starts_with(url_or_id, "https://")) {
		std::string &url = url_or_id;
		res.url_original = url;

		url = convert_url_to_mobile(url);

		// append "/shorts" at the end of the url
		{
			bool ok = false;
			for (auto pattern : std::vector<std::string>{"https://m.youtube.com/channel/", "https://m.youtube.com/c/",
			                                             "https://m.youtube.com/user/", "https://m.youtube.com/@"}) {
				if (url.substr(0, pattern.size()) == pattern) {
					url = url.substr(pattern.size(), url.size());
					auto next_slash = std::find(url.begin(), url.end(), '/');
					url = pattern + std::string(url.begin(), next_slash) + "/shorts";
					ok = true;
					break;
				}
			}
			if (!ok) {
				res.error = "invalid URL : " + url;
				return res;
			}
		}

		auto result = http_get(url);
		if (!result.first) {
			debug_error((res.error = "[ch-shorts] " + result.second));
		} else {
			auto html = result.second;
			if (!html.size()) {
				res.error = "[ch-shorts] html empty";
				return res;
			}
			Document json_root;
			auto initial_data = get_initial_data(json_root, html);
			parse_channel_data(initial_data, res);
			res.shorts_loaded = true;
		}
	} else {
		std::string &id = url_or_id;
		res.url_original = "https://m.youtube.com/channel/" + id;

		std::string post_content =
		    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00"}}, "browseId": "%2", "params":"EgZzaG9ydHPyBgUKA5oBAA%3D%3D"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), id);

		access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
		                      [&](Document &, RJson json) {
			                      parse_channel_data(json, res);
			                      res.shorts_loaded = true;
		                      },
		                      [&](const std::string &error) {
			                      res.error = "[ch-shorts] " + error;
			                      debug_error(res.error);
		                      });
	}
	return res;
}

std::vector<YouTubeChannelDetail> youtube_load_channel_page_multi(std::vector<std::string> ids,
                                                                  std::function<void(int, int)> progress) {
	std::vector<YouTubeChannelDetail> res;
	if (progress) {
		progress(0, ids.size());
	}
	std::vector<HttpRequest> requests;
	int n = ids.size();
	int finished = 0;
	for (int i = 0; i < n; i++) {
		std::string post_content =
		    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00"}}, "browseId": "%2", "params":"EgZ2aWRlb3PyBgQKAjoA"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), ids[i]);
		requests.push_back(http_post_json_request(get_innertube_api_url("browse"), post_content)
		                       .with_on_finish_callback([&](NetworkResult &, int cur) {
			                       if (progress) {
				                       progress(++finished, n);
			                       }
		                       }));
	}
	debug_info("access(multi)...");
	auto results = thread_network_session_list.perform(requests);
	debug_info("ok");
	for (auto result : results) {
		result.data.push_back('\0');
		YouTubeChannelDetail cur_res;
		parse_json_destructive((char *)&result.data[0],
		                       [&](Document &, RJson data) { parse_channel_data(data, cur_res); },
		                       [&](const std::string &error) {
			                       cur_res.error = "[ch-mul] " + error;
			                       debug_error(cur_res.error);
		                       });
		res.push_back(cur_res);
	}
	return res;
}

void YouTubeChannelDetail::load_more_videos() {
	if (videos_continue_token == "") {
		error = "continue token empty";
		return;
	}

	std::string post_content =
	    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")" +
	    videos_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);

	access_and_parse_json(
	    [&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
	    [&](Document &, RJson yt_result) {
		    videos_continue_token = "";

		    for (auto i : yt_result["onResponseReceivedActions"].array_items()) {
			    auto continuation_items = i.has_key("appendContinuationItemsAction")
			                                  ? i["appendContinuationItemsAction"]["continuationItems"]
			                                  : i["reloadContinuationItemsCommand"]["continuationItems"];

			    for (auto j : continuation_items.array_items()) {
				    if (j["richItemRenderer"]["content"].has_key("videoWithContextRenderer")) {
					    videos.push_back(
					        parse_succinct_video(j["richItemRenderer"]["content"]["videoWithContextRenderer"]));
				    } else if (j.has_key("compactVideoRenderer")) {
					    videos.push_back(parse_succinct_video(j["compactVideoRenderer"]));
				    } else if (j.has_key("continuationItemRenderer")) {
					    videos_continue_token =
					        j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
					            .string_value();
				    }
			    }
		    }
		    if (videos_continue_token == "") {
			    debug_caution("failed to get next continue token");
		    }
	    },
	    [&](const std::string &error) { debug_error((this->error = "[ch+] " + error)); });
}

void YouTubeChannelDetail::load_more_streams() {
	if (streams_continue_token == "") {
		error = "continue token empty";
		return;
	}

	std::string post_content =
	    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")" +
	    streams_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);

	access_and_parse_json(
	    [&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
	    [&](Document &, RJson yt_result) {
		    streams_continue_token = "";

		    for (auto i : yt_result["onResponseReceivedActions"].array_items()) {
			    for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
				    if (j["richItemRenderer"]["content"].has_key("videoWithContextRenderer")) {
					    streams.push_back(
					        parse_succinct_video(j["richItemRenderer"]["content"]["videoWithContextRenderer"]));
				    } else if (j["richItemRenderer"]["content"].has_key("videoRenderer")) {
					    streams.push_back(parse_succinct_video(j["richItemRenderer"]["content"]["videoRenderer"]));
				    } else if (j.has_key("compactVideoRenderer")) {
					    streams.push_back(parse_succinct_video(j["compactVideoRenderer"]));
				    } else if (j.has_key("continuationItemRenderer")) {
					    streams_continue_token =
					        j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
					            .string_value();
				    }
			    }
		    }
		    if (streams_continue_token == "") {
			    debug_caution("failed to get next continue token");
		    }
	    },
	    [&](const std::string &error) { debug_error((this->error = "[ch+s] " + error)); });
}

void YouTubeChannelDetail::load_more_shorts() {
	if (shorts_continue_token == "") {
		error = "continue token empty";
		return;
	}

	std::string post_content =
	    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")" +
	    shorts_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);

	access_and_parse_json(
	    [&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
	    [&](Document &, RJson yt_result) {
		    shorts_continue_token = "";
		    for (auto i : yt_result["onResponseReceivedActions"].array_items()) {
			    RJson continuation_items;
			    if (i.has_key("appendContinuationItemsAction")) {
				    continuation_items = i["appendContinuationItemsAction"]["continuationItems"];
			    } else if (i.has_key("reloadContinuationItemsCommand")) {
				    continuation_items = i["reloadContinuationItemsCommand"]["continuationItems"];
			    }

			    for (auto j : continuation_items.array_items()) {
				    if (j["richItemRenderer"]["content"].has_key("shortsLockupViewModel")) {
					    auto shorts_data = j["richItemRenderer"]["content"]["shortsLockupViewModel"];
					    YouTubeVideoSuccinct short_video;

					    std::string video_id;
					    if (shorts_data["onTap"]["innertubeCommand"].has_key("reelWatchEndpoint")) {
						    video_id =
						        shorts_data["onTap"]["innertubeCommand"]["reelWatchEndpoint"]["videoId"].string_value();
					    }

					    if (video_id.empty() && shorts_data["onTap"]["innertubeCommand"].has_key("commandMetadata")) {
						    std::string url =
						        shorts_data["onTap"]["innertubeCommand"]["commandMetadata"]["webCommandMetadata"]["url"]
						            .string_value();
						    if (url.find("/shorts/") != std::string::npos) {
							    video_id = url.substr(url.find("/shorts/") + 8);
							    size_t param_pos = video_id.find('?');
							    if (param_pos != std::string::npos) {
								    video_id = video_id.substr(0, param_pos);
							    }
						    }
					    }

					    if (!video_id.empty()) {
						    short_video.url = "https://m.youtube.com/watch?v=" + video_id;
					    }

					    if (shorts_data.has_key("overlayMetadata")) {
						    auto overlay_metadata = shorts_data["overlayMetadata"];

						    if (overlay_metadata.has_key("primaryText") &&
						        overlay_metadata["primaryText"].has_key("content")) {
							    short_video.title = overlay_metadata["primaryText"]["content"].string_value();
						    }

						    if (overlay_metadata.has_key("secondaryText") &&
						        overlay_metadata["secondaryText"].has_key("content")) {
							    short_video.views_str = overlay_metadata["secondaryText"]["content"].string_value();
						    }
					    }

					    if (shorts_data["thumbnail"]["sources"].array_items().size() > 0) {
						    std::string original_thumbnail_url =
						        shorts_data["thumbnail"]["sources"].array_items()[0]["url"].string_value();

						    // Convert webp format to jpg if necessary
						    std::string converted_thumbnail_url = convert_webp_thumbnail_to_jpg(original_thumbnail_url);

						    std::string video_id = get_video_id_from_thumbnail_url(converted_thumbnail_url);
						    if (!video_id.empty()) {
							    short_video.thumbnail_url = youtube_get_video_thumbnail_url_by_id(video_id);
						    } else {
							    short_video.thumbnail_url = converted_thumbnail_url;
						    }
					    }

					    short_video.duration_text = "";
					    short_video.author = name;

					    shorts.push_back(short_video);
				    } else if (j.has_key("continuationItemRenderer")) {
					    shorts_continue_token =
					        j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
					            .string_value();
				    }
			    }
		    }
		    if (shorts_continue_token == "") {
			    debug_caution("failed to get next continue token");
		    }
	    },
	    [&](const std::string &error) { debug_error((this->error = "[ch-shorts+] " + error)); });
}

static void channel_load_playlists_(RJson yt_result, YouTubeChannelDetail &new_result) {
	auto convert_compact_playlist_renderer = [](RJson playlist_renderer) {
		YouTubePlaylistSuccinct cur_list;
		cur_list.title = get_text_from_object(playlist_renderer["title"]);
		cur_list.video_count_str = get_text_from_object(playlist_renderer["videoCountText"]);
		for (auto thumbnail : playlist_renderer["thumbnail"]["thumbnails"].array_items()) {
			if (std::string(thumbnail["url"].string_value()).find("/default.jpg") != std::string::npos) {
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
				return cur_list;
			}
		}
		return cur_list;
	};

	for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
		for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
			if (i.has_key("shelfRenderer")) {
				std::string category_name = get_text_from_object(i["shelfRenderer"]["title"]);
				std::vector<YouTubePlaylistSuccinct> playlists;
				for (auto j : i["shelfRenderer"]["content"]["verticalListRenderer"]["items"].array_items()) {
					if (j.has_key("compactPlaylistRenderer")) {
						playlists.push_back(convert_compact_playlist_renderer(j["compactPlaylistRenderer"]));
					}
				}
				if (playlists.size()) {
					new_result.playlists.push_back({category_name, playlists});
				}
			}
			if (i.has_key("itemSectionRenderer")) {
				std::string category_name;
				for (auto j : tab["tabRenderer"]["content"]["sectionListRenderer"]["subMenu"]["channelSubMenuRenderer"]
				                 ["contentTypeSubMenuItems"]
				                     .array_items()) {
					category_name += j["title"].string_value();
				}
				std::vector<YouTubePlaylistSuccinct> playlists;
				for (auto j : i["itemSectionRenderer"]["contents"].array_items()) {
					if (j.has_key("compactPlaylistRenderer")) {
						playlists.push_back(convert_compact_playlist_renderer(j["compactPlaylistRenderer"]));
					}
				}
				// If the channel has no playlists, there's an itemSectionRenderer with only a messageRenderer in
				// i["itemSectionRenderer"]["contents"]
				if (playlists.size()) {
					new_result.playlists.push_back({category_name, playlists});
				}
			}
		}
	}
	new_result.playlist_tab_browse_id = "";
	new_result.playlist_tab_params = "";
}
void YouTubeChannelDetail::load_playlists() {
	if (playlist_tab_browse_id == "") {
		error = "playlist browse id empty";
	}
	if (playlist_tab_params == "") {
		error = "playlist params empty";
	}

	if (error != "") {
		return;
	}

	std::string post_content =
	    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20241202.07.00", "utcOffsetMinutes": 0}}, "browseId": "%2", "params": "%3"})";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	post_content = std::regex_replace(post_content, std::regex("%2"), playlist_tab_browse_id);
	post_content = std::regex_replace(post_content, std::regex("%3"), playlist_tab_params);

	access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
	                      [&](Document &, RJson json) { channel_load_playlists_(json, *this); },
	                      [&](const std::string &error) { debug_error((this->error = "[ch/pl] " + error)); });
}

static void load_community_items(RJson contents, YouTubeChannelDetail &res) {
	res.community_continuation_token = "";
	for (auto post : contents.array_items()) {
		if (post.has_key("backstagePostThreadRenderer")) {
			auto post_renderer = post["backstagePostThreadRenderer"]["post"]["backstagePostRenderer"];
			YouTubeChannelDetail::CommunityPost cur_post;
			cur_post.message = get_text_from_object(post_renderer["contentText"]);
			cur_post.author_name = get_text_from_object(post_renderer["authorText"]);
			cur_post.author_icon_url = get_thumbnail_url_closest(post_renderer["authorThumbnail"]["thumbnails"], 70);
			cur_post.time = get_text_from_object(post_renderer["publishedTimeText"]);
			cur_post.upvotes_str = get_text_from_object(post_renderer["voteCount"]);
			if (post_renderer["backstageAttachment"]["backstageImageRenderer"].is_valid()) {
				auto tmp =
				    post_renderer["backstageAttachment"]["backstageImageRenderer"]["image"]["thumbnails"].array_items();
				if (tmp.size()) {
					cur_post.image_url = tmp[0]["url"].string_value();
				}
			}
			if (post_renderer["backstageAttachment"]["videoRenderer"].is_valid()) {
				cur_post.video = parse_succinct_video(post_renderer["backstageAttachment"]["videoRenderer"]);
			}
			if (post_renderer["backstageAttachment"]["pollRenderer"].is_valid()) {
				auto poll_renderer = post_renderer["backstageAttachment"]["pollRenderer"];
				cur_post.poll_total_votes = get_text_from_object(poll_renderer["totalVotes"]);
				for (auto choice : poll_renderer["choices"].array_items()) {
					cur_post.poll_choices.push_back(get_text_from_object(choice["text"]));
				}
			}
			res.community_posts.push_back(cur_post);
		} else if (post.has_key("continuationItemRenderer")) {
			res.community_continuation_token =
			    post["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
		}
	}
}

void YouTubeChannelDetail::load_more_community_posts() {
	if (!has_community_posts_to_load()) {
		error = "No community post to load";
		return;
	}

	if (!community_loaded) {
		community_loaded = true;
		// community post seems to be only available in the desktop version
		std::string url = convert_url_to_desktop(this->url + "/posts");
		auto result = http_get(
		    url, {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:94.0) Gecko/20100101 Firefox/94.0"}});
		if (!result.first) {
			debug_error((this->error = "[ch/c+] " + result.second));
		} else {
			auto html = result.second;
			if (!html.size()) {
				error = "failed to download community page";
				return;
			}
			Document json_root;
			RJson initial_data = get_initial_data(json_root, html);

			RJson contents;
			for (auto tab : initial_data["contents"]["twoColumnBrowseResultsRenderer"]["tabs"].array_items()) {
				for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
					contents = i["itemSectionRenderer"]["contents"];
				}
			}
			load_community_items(contents, *this);
		}
	} else {
		std::string post_content =
		    R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "WEB", "clientVersion": "2.20241126.01.00", "utcOffsetMinutes": 0}}, "continuation": "%2"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), community_continuation_token);

		access_and_parse_json([&]() { return http_post_json(get_innertube_api_url("browse"), post_content); },
		                      [&](Document &, RJson yt_result) {
			                      RJson contents;
			                      for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) {
				                      if (i.has_key("appendContinuationItemsAction")) {
					                      contents = i["appendContinuationItemsAction"]["continuationItems"];
				                      }
			                      }
			                      load_community_items(contents, *this);
		                      },
		                      [&](const std::string &error) { debug_error((this->error = "[ch/c+] " + error)); });
	}
}
