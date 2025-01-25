#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"


static RJson get_initial_data(Document &json_root, const std::string &html) {
	RJson res;
	if (fast_extract_initial(json_root, html, "ytInitialData", res)) return res;
	res = get_succeeding_json_regexes(json_root, html, {
		"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialData\\s*=\\s*['\\{]"
	});
	if (!res.is_valid()) return get_error_json("did not match any of the ytInitialData regexes");
	return res;
}

static void parse_channel_data(RJson data, YouTubeChannelDetail &res) {
	std::string channel_name = "stub channel name";
	
	auto metadata_renderer = data["metadata"]["channelMetadataRenderer"];
	res.name = metadata_renderer["title"].string_value();
	if (data["header"].has_key("c4TabbedHeaderRenderer")) {
		res.subscriber_count_str = get_text_from_object(data["header"]["c4TabbedHeaderRenderer"]["subscriberCountText"]);
		res.banner_url = get_thumbnail_url_exact(data["header"]["c4TabbedHeaderRenderer"]["banner"]["thumbnails"], 320);
		res.icon_url = get_thumbnail_url_closest(data["header"]["c4TabbedHeaderRenderer"]["avatar"]["thumbnails"], 88);
	} else {
		auto model = data["header"]["pageHeaderRenderer"]["content"]["pageHeaderViewModel"];
		res.banner_url = get_thumbnail_url_exact(model["banner"]["imageBannerViewModel"]["image"]["sources"], 320);
		res.icon_url = get_thumbnail_url_closest(model["image"]["decoratedAvatarViewModel"]["avatar"]["avatarViewModel"]["image"]["sources"], 88);
		auto tmp = model["metadata"]["contentMetadataViewModel"]["metadataRows"].array_items();
		if (tmp.size()) {
			tmp = tmp[1]["metadataParts"].array_items();
			if (tmp.size() >= 1) res.subscriber_count_str = tmp[0]["text"]["content"].string_value();
		}
	}
	res.id = metadata_renderer["externalId"].string_value();
	res.url = "https://m.youtube.com/channel/" + metadata_renderer["externalId"].string_value();
	res.description = metadata_renderer["description"].string_value();
	
	for (auto tab : data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
		for (auto i : tab["tabRenderer"]["content"]["richGridRenderer"]["contents"].array_items()) {
			if (i.has_key("continuationItemRenderer")) 
				res.continue_token = i["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
			else if (i["richItemRenderer"]["content"].has_key("compactVideoRenderer")) 
				res.videos.push_back(parse_succinct_video(i["richItemRenderer"]["content"]["compactVideoRenderer"]));
			else if (i["richItemRenderer"]["content"].has_key("videoWithContextRenderer")) 
				res.videos.push_back(parse_succinct_video(i["richItemRenderer"]["content"]["videoWithContextRenderer"]));
			else debug_warning("unknown item found in channel videos");
		}
		std::string tab_url = tab["tabRenderer"]["endpoint"]["commandMetadata"]["webCommandMetadata"]["url"].string_value();
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
			for (auto pattern : std::vector<std::string>{"https://m.youtube.com/channel/", "https://m.youtube.com/c/", "https://m.youtube.com/user/", "https://m.youtube.com/@"}) {
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
		if (!result.first) debug_error((res.error = "[ch-id] " + result.second));
		else {
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
		
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20240304.08.00"}}, "browseId": "%2", "params":"EgZ2aWRlb3PyBgQKAjoA"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), id);
		
		access_and_parse_json(
			[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
			[&] (Document &, RJson json) { parse_channel_data(json, res); },
			[&] (const std::string &error) {
				res.error = "[ch-id] " + error;
				debug_error(res.error);
			}
		);
	}
	return res;
}
std::vector<YouTubeChannelDetail> youtube_load_channel_page_multi(std::vector<std::string> ids, std::function<void (int, int)> progress) {
	std::vector<YouTubeChannelDetail> res;
	if (progress) progress(0, ids.size());
#ifdef _WIN32
	int finished = 0;
	for (auto id : ids) {
		res.push_back(youtube_load_channel_page(id));
		if (progress) progress(++finished, ids.size());
	}
#else
	
	std::vector<HttpRequest> requests;
	int n = ids.size();
	int finished = 0;
	for (int i = 0; i < n; i++) {
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00"}}, "browseId": "%2", "params":"EgZ2aWRlb3PyBgQKAjoA"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), ids[i]);
		requests.push_back(http_post_json_request(get_innertube_api_url("browse"), post_content).with_on_finish_callback([&] (NetworkResult &, int cur) {
			if (progress) progress(++finished, n);
		}));
	}
	debug_info("access(multi)...");
	auto results = thread_network_session_list.perform(requests);
	debug_info("ok");
	for (auto result : results) {
		result.data.push_back('\0');
		YouTubeChannelDetail cur_res;
		parse_json_destructive((char *) &result.data[0],
			[&] (Document &, RJson data) { parse_channel_data(data, cur_res); },
			[&] (const std::string &error) {
				cur_res.error = "[ch-mul] " + error;
				debug_error(cur_res.error);
			}
		);
		res.push_back(cur_res);
	}
#endif
	return res;
}


void YouTubeChannelDetail::load_more_videos() {
	if (continue_token == "") {
		error = "continue token empty";
		return;
	}
	
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
		+ continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
		[&] (Document &, RJson yt_result) {
			continue_token = "";
			
			for (auto i : yt_result["onResponseReceivedActions"].array_items()) {
				for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
					if (j["richItemRenderer"]["content"].has_key("videoWithContextRenderer")) 
						videos.push_back(parse_succinct_video(j["richItemRenderer"]["content"]["videoWithContextRenderer"]));
					else if (j.has_key("compactVideoRenderer"))
						videos.push_back(parse_succinct_video(j["compactVideoRenderer"]));
					else if (j.has_key("continuationItemRenderer"))
						continue_token = j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				}
			}
			if (continue_token == "") debug_caution("failed to get next continue token");
		},
		[&] (const std::string &error) { debug_error((this->error = "[ch+] " + error)); }
	);
}



static void channel_load_playlists_(RJson yt_result, YouTubeChannelDetail &new_result) {
	auto convert_compact_playlist_renderer = [] (RJson playlist_renderer) {
		YouTubePlaylistSuccinct cur_list;
		cur_list.title = get_text_from_object(playlist_renderer["title"]);
		cur_list.video_count_str = get_text_from_object(playlist_renderer["videoCountText"]);
		for (auto thumbnail : playlist_renderer["thumbnail"]["thumbnails"].array_items())
			if (std::string(thumbnail["url"].string_value()).find("/default.jpg") != std::string::npos) cur_list.thumbnail_url = thumbnail["url"].string_value();
		
		cur_list.url = convert_url_to_mobile(playlist_renderer["shareUrl"].string_value());
		if (!starts_with(cur_list.url, "https://m.youtube.com/watch", 0)) {
			if (starts_with(cur_list.url, "https://m.youtube.com/playlist?", 0)) {
				auto params = parse_parameters(cur_list.url.substr(std::string("https://m.youtube.com/playlist?").size(), cur_list.url.size()));
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
				for (auto j : i["shelfRenderer"]["content"]["verticalListRenderer"]["items"].array_items())
					if (j.has_key("compactPlaylistRenderer")) playlists.push_back(convert_compact_playlist_renderer(j["compactPlaylistRenderer"]));
				if (playlists.size()) new_result.playlists.push_back({category_name, playlists});
			}
			if (i.has_key("itemSectionRenderer")) {
				std::string category_name;
				for (auto j : tab["tabRenderer"]["content"]["sectionListRenderer"]["subMenu"]["channelSubMenuRenderer"]["contentTypeSubMenuItems"].array_items())
					category_name += j["title"].string_value();
				std::vector<YouTubePlaylistSuccinct> playlists;
				for (auto j : i["itemSectionRenderer"]["contents"].array_items())
					if (j.has_key("compactPlaylistRenderer")) playlists.push_back(convert_compact_playlist_renderer(j["compactPlaylistRenderer"]));
				// If the channel has no playlists, there's an itemSectionRenderer with only a messageRenderer in i["itemSectionRenderer"]["contents"]
				if (playlists.size()) new_result.playlists.push_back({category_name, playlists});
			}
		}
	}
	new_result.playlist_tab_browse_id = "";
	new_result.playlist_tab_params = "";
}
void YouTubeChannelDetail::load_playlists() {
	if (playlist_tab_browse_id == "") error = "playlist browse id empty";
	if (playlist_tab_params == "") error = "playlist params empty";
	
	if (error != "") return;
	
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}}, "browseId": "%2", "params": "%3"})";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	post_content = std::regex_replace(post_content, std::regex("%2"), playlist_tab_browse_id);
	post_content = std::regex_replace(post_content, std::regex("%3"), playlist_tab_params);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
		[&] (Document &, RJson json) { channel_load_playlists_(json, *this); },
		[&] (const std::string &error) { debug_error((this->error = "[ch/pl] " + error)); }
	);
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
				auto tmp = post_renderer["backstageAttachment"]["backstageImageRenderer"]["image"]["thumbnails"].array_items();
				if (tmp.size()) cur_post.image_url = tmp[0]["url"].string_value();
			}
			if (post_renderer["backstageAttachment"]["videoRenderer"].is_valid()) 
				cur_post.video = parse_succinct_video(post_renderer["backstageAttachment"]["videoRenderer"]);
			if (post_renderer["backstageAttachment"]["pollRenderer"].is_valid()) {
				auto poll_renderer = post_renderer["backstageAttachment"]["pollRenderer"];
				cur_post.poll_total_votes = get_text_from_object(poll_renderer["totalVotes"]);
				for (auto choice : poll_renderer["choices"].array_items())
					cur_post.poll_choices.push_back(get_text_from_object(choice["text"]));
			}
			res.community_posts.push_back(cur_post);
		} else if (post.has_key("continuationItemRenderer")) {
			res.community_continuation_token = post["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
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
		std::string url = convert_url_to_desktop(this->url + "/community");
		auto result = http_get(url, {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:94.0) Gecko/20100101 Firefox/94.0"}});
		if (!result.first) debug_error((this->error = "[ch/c+] " + result.second));
		else {
			auto html = result.second;
			if (!html.size()) {
				error = "failed to download community page";
				return;
			}
			Document json_root;
			RJson initial_data = get_initial_data(json_root, html);
			
			RJson contents;
			for (auto tab : initial_data["contents"]["twoColumnBrowseResultsRenderer"]["tabs"].array_items())
				for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items())
					contents = i["itemSectionRenderer"]["contents"];
			load_community_items(contents, *this);
		}
	} else {
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "WEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}}, "continuation": "%2"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), community_continuation_token);
		
		access_and_parse_json(
			[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
			[&] (Document &, RJson yt_result) {
				RJson contents;
				for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) if (i.has_key("appendContinuationItemsAction"))
					contents = i["appendContinuationItemsAction"]["continuationItems"];
				load_community_items(contents, *this);
			},
			[&] (const std::string &error) { debug_error((this->error = "[ch/c+] " + error)); }
		);
	}
}
