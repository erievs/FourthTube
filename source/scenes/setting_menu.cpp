// clang-format off
#include "headers.hpp"
#include <functional>
#include <regex>

#include "data_io/settings.hpp"
#include "data_io/history.hpp"
#include "util/misc_tasks.hpp"
#include "scenes/setting_menu.hpp"
#include "scenes/video_player.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "ui/views/specialized/succinct_channel.hpp"
#include "youtube_parser/parser.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "network_decoder/network_io.hpp"
#include "rapidjson_wrapper.hpp"
#include "oauth/oauth.hpp"

struct SectionTitleWithInfoView : public FixedSizeView {
	UI::FlexibleString<SectionTitleWithInfoView> title_text;
	UI::FlexibleString<SectionTitleWithInfoView> info_text;
	int popup_height = 30;
	
	SectionTitleWithInfoView(double x0, double y0, double width, double height) 
		: View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	
	SectionTitleWithInfoView *set_title(std::function<std::string(const SectionTitleWithInfoView &)> title_func) {
		title_text = UI::FlexibleString<SectionTitleWithInfoView>(title_func, *this);
		return this;
	}
	
	SectionTitleWithInfoView *set_info(std::function<std::string(const SectionTitleWithInfoView &)> info_func) {
		info_text = UI::FlexibleString<SectionTitleWithInfoView>(info_func, *this);
		return this;
	}
	
	SectionTitleWithInfoView *set_popup_height(int height) {
		popup_height = height;
		return this;
	}
	
	void draw_() const override {
		double y_pos = (y0 + y1 - DEFAULT_FONT_INTERVAL) / 2 - 2;
		Draw(title_text, x0 + SMALL_MARGIN, y_pos, 0.6, 0.6, DEFAULT_TEXT_COLOR);
		
		double info_x = x0 + SMALL_MARGIN + Draw_get_width(title_text, 0.6) + 5;
		Draw("ⓘ", info_x, y_pos + (0.6 - 0.45) * DEFAULT_FONT_INTERVAL / 2, 0.45, 0.45, 0xFFFF9030);
	}
	
	void update_(Hid_info key) override {
		if (key.p_touch && key.touch_y >= y0 && key.touch_y < y1) {
			double info_x = x0 + SMALL_MARGIN + Draw_get_width(title_text, 0.6) + 5;
			if (key.touch_x >= info_x - 3 && key.touch_x < info_x + Draw_get_width("ⓘ", 0.45) + 3) {
				extern void (*show_info_popup_callback)(const std::string &, int);
				if (show_info_popup_callback) show_info_popup_callback(info_text, popup_height);
			}
		}
	}
};

namespace Settings {
	bool thread_suspend = false;
	bool already_init = false;
	bool exiting = false;
	
	bool is_3dsx = false;
	std::string path_3dsx;
	
	TextView *toast_view;
	int toast_frames_left = 0;
	
	OverlayDialogView *popup_view;
	VerticalListView *main_view;
	TabView *main_tab_view;
	
	ProgressBarView *update_progress_bar_view;
	VerticalListView *release_notes_view;
	
	// OAuth state
	bool oauth_check_timer_active = false;
	int oauth_check_frames = 0;
	int oauth_timeout_counter = 0;
	const int OAUTH_CHECK_INTERVAL_FRAMES = 300;
	const int OAUTH_TIMEOUT_SECONDS = 900;
	
	SuccinctChannelView *oauth_user_view = nullptr;
	
	Thread oauth_worker_thread;
	
	int CONTENT_Y_HIGH = 240;
	constexpr int TOP_HEIGHT = MIDDLE_FONT_INTERVAL + SMALL_MARGIN * 2;
	
	constexpr int DIALOG_WIDTH = 240;
	
	// used for updating of the app
	enum class UpdateState {
		CHECKING_UPDATES,
		FAILED_CHECKING,
		UP_TO_DATE,
		UPDATES_AVAILABLE,
		INSTALLING,
		FAILED_INSTALLING,
		SUCCEEDED_INSTALLING
	};
	UpdateState update_state = UpdateState::CHECKING_UPDATES;
	
	static std::string update_error_message;
	
	static std::string update_url_cia;
	static std::string update_url_3dsx;
	static std::string next_version_str;
	static Mutex resource_lock;
	static std::string install_progress_str;
	
	static Thread update_worker_thread;
	
	Thread settings_misc_thread;
};
using namespace Settings;

void (*show_info_popup_callback)(const std::string &, int) = nullptr;

void show_info_popup(const std::string &message, int height = 30) {
	if (!popup_view) return;
	
	popup_view->get_message_view()
		->set_text_lines(split_string(message, '\n'))
		->update_y_range(0, height);
	popup_view->set_buttons<std::function<std::string()>>({
		[]() { return LOCALIZED(OK); }
	}, [](OverlayDialogView &, int) {
		return true;
	});
	popup_view->set_is_visible(true);
	var_need_refresh = true;
}


std::string install_update(NetworkSessionList &session_list) {
	std::string new_error_message = "";
	auto url = is_3dsx ? update_url_3dsx : update_url_cia;
	// auto url = update_url_cia;
	
	resource_lock.lock();
	install_progress_str = LOCALIZED(ACCESSING);
	resource_lock.unlock();
	update_progress_bar_view->set_progress(0);
	update_progress_bar_view->set_is_visible(true);
	
	bool first = true;
	auto result = session_list.perform(HttpRequest::GET(url, {}).with_progress_func([&] (u64 now, u64 total) {
		if (total < 10000) return; // ignore header(?)
		if (first) {
			first = false;
			resource_lock.lock();
			install_progress_str = LOCALIZED(DOWNLOADING);
			resource_lock.unlock();
		}
		update_progress_bar_view->set_progress((double) now / total);
	}));
	
	if (result.fail) return "curl deep fail : " + result.error;
	if (result.status_code != 200) return "http returned " + std::to_string(result.status_code);
	
	resource_lock.lock();
	install_progress_str = LOCALIZED(INSTALLING);
	resource_lock.unlock();
	update_progress_bar_view->set_progress(1.0);
	
	Result libctru_result;
	if (is_3dsx) {
		char *slash_ptr = strrchr(path_3dsx.c_str(), '/');
		if (!slash_ptr) return "no slash in 3dsx path : " + path_3dsx;
		size_t slash_index = slash_ptr - path_3dsx.c_str();
		auto tmp_res = Path(path_3dsx).write_file(result.data.data(), result.data.size());
		if (tmp_res.code != 0) return "Failed to write to .3dsx file : " + tmp_res.string + " " + std::to_string(tmp_res.code);
	} else {
		Handle am_handle = 0;
		if ((libctru_result = AM_StartCiaInstall(MEDIATYPE_SD, &am_handle))) return "AM_StartCiaInstall() returned " + std::to_string(libctru_result);
		
		update_progress_bar_view->set_progress(0.0);
		
		const size_t BLOCK_SIZE = 200000;
		for (size_t i = 0; i < result.data.size(); ) {
			size_t size = std::min(BLOCK_SIZE, result.data.size() - i);
			u32 size_written;
			libctru_result = FSFILE_Write(am_handle, &size_written, i, result.data.data() + i, size, FS_WRITE_FLUSH);
			i += size_written;
			if (libctru_result) return "FSFILE_Write() returned " + std::to_string(libctru_result);
			update_progress_bar_view->set_progress((double) i / result.data.size());
		}
		update_progress_bar_view->set_progress(1.0);
		
		libctru_result = AM_FinishCiaInstall(am_handle);
		if (libctru_result) return "AM_FinishCiaInstall() returned " + std::to_string(libctru_result);
	}
	return "";
}

static void update_worker_thread_func(void *) {
	logger.info("updater", "Thread started.");
	
	static NetworkSessionList session_list;
	session_list.init();
	
	while (!exiting) {
		if (update_state == UpdateState::CHECKING_UPDATES) {
			std::string new_error_message;
			
			bool check_success = false;
			auto result = session_list.perform(HttpRequest::GET("https://api.github.com/repos/erievs/FourthTube/releases/latest", {}));
			if (result.fail) new_error_message = "Failed accessing(deep fail) : " + result.fail;
			else if (result.status_code == 200) {
				result.data.push_back('\0');
				rapidjson::Document json_root;
				std::string error;
				RJson result_json = RJson::parse(json_root, (char *) result.data.data(), error);
				if (error != "") {
					logger.error("updater", "failed to parse json : " + error);
					new_error_message = "failed to parse json : " + error;
				} else {
					update_url_3dsx = update_url_cia;
					for (auto item : result_json["assets"].array_items()) {
						auto url = item["browser_download_url"].string_value();
						if (url.size() >= 5 && url.substr(url.size() - 5, 5) == ".3dsx") update_url_3dsx = url;
						if (url.size() >= 4 && url.substr(url.size() - 4, 4) == ".cia") update_url_cia = url;
					}
					if (update_url_cia == "") new_error_message = "could not find .cia url";
					if (update_url_3dsx == "") new_error_message = "could not find .3dsx url";
					if (update_url_cia != "" && update_url_3dsx != "") {
						// lexicographically compare the sequence of version numbers ([0, 4, 1] if v0.4.1)
						auto version_str_to_vector = [] (const std::string &str) {
							size_t head = 0;
							std::vector<int> res;
							while (head < str.size()) {
								while (head < str.size() && !isdigit(str[head])) head++;
								if (head >= str.size()) break;
								int cur = 0;
								while (head < str.size() && isdigit(str[head])) cur = cur * 10 + str[head++] - '0';
								res.push_back(cur);
							}
							return res;
						};
						auto new_version_seq = version_str_to_vector(result_json["tag_name"].string_value());
						auto cur_version_seq = version_str_to_vector(DEF_CURRENT_APP_VER);
						bool update_available = cur_version_seq < new_version_seq;
						std::string release_notes = result_json["body"].string_value();
						std::istringstream lines(release_notes);
						std::ostringstream wrapped;
						std::string line;
						size_t line_len = 0;
						const size_t line_length = 47;
						std::vector<std::string> release_notes_lines;

						while (std::getline(lines, line)) {
  						    std::istringstream words(line);
 						    std::string word;
 						    size_t line_len = 0;

						    while (words >> word) {
  						        if (line_len + word.length() > line_length) {
  						            wrapped << "\n";
  						            release_notes_lines.push_back(wrapped.str());
  						            wrapped.str("");
 						            line_len = 0;
  						        }
  						        wrapped << word << " ";
  						        line_len += word.length() + 1;
 						    }
   						wrapped << "\n";
  						release_notes_lines.push_back(wrapped.str());
  						wrapped.str("");
						}

						release_notes = wrapped.str();

   						resource_lock.lock();
   						release_notes_view->set_is_visible(true);
   						while (!release_notes_view->views.empty()) {
 						    delete release_notes_view->views.back();
 						    release_notes_view->views.pop_back();
   						}
   						for (const auto& note_line : release_notes_lines) {
   						    release_notes_view->views.push_back((new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))->set_text(note_line));
   						}
   						resource_lock.unlock();

						if (update_available) update_state = UpdateState::UPDATES_AVAILABLE, next_version_str = result_json["tag_name"].string_value();
						else update_state = UpdateState::UP_TO_DATE;
						check_success = true;
					}
				}
			} else new_error_message = "http returned " + std::to_string(result.status_code);
			if (!check_success) update_state = UpdateState::FAILED_CHECKING;
			if (new_error_message != "") {
				resource_lock.lock();
				update_error_message = new_error_message;
				resource_lock.unlock();
			}
		}
		if (update_state == UpdateState::INSTALLING) {
			std::string error = install_update(session_list);
			if (error == "") update_state = UpdateState::SUCCEEDED_INSTALLING;
			else {
				resource_lock.lock();
				update_error_message = error;
				resource_lock.unlock();
				update_state = UpdateState::FAILED_INSTALLING;
			}
		}
		usleep(100000);
	}
	
	logger.info("updater", "Thread exit.");
	threadExit(0);
}

static void oauth_worker_thread_func(void *) {
	logger.info("oauth_worker", "Thread started.");
	oauth_timeout_counter = 0;
	
	while (!exiting) {
		if (OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATING && oauth_check_timer_active) {
			int poll_interval = std::max(OAuth::interval, 8);
			oauth_timeout_counter += poll_interval;
			
			logger.info("oauth_worker", "Checking device flow authentication...");
			OAuth::check_device_flow();
			
			if (OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATED) {
				oauth_check_timer_active = false;
				oauth_timeout_counter = 0;
				var_oauth_enabled = true;
				misc_tasks_request(TASK_SAVE_SETTINGS);

				resource_lock.lock();
				if (oauth_user_view) {
					std::string user_name = OAuth::get_user_account_name();
					std::string channel_id = OAuth::get_user_channel_id();
					std::string photo_url = OAuth::get_user_photo_url();
					
				oauth_user_view->set_name(user_name);
				oauth_user_view->set_auxiliary_lines({channel_id});
				oauth_user_view->set_thumbnail_url(photo_url);
				oauth_user_view->set_height(CHANNEL_ICON_HEIGHT);
				if (!photo_url.empty()) {
					oauth_user_view->thumbnail_handle = thumbnail_request(photo_url, SceneType::SETTINGS, PRIORITY_FOREGROUND, ThumbnailType::ICON);
				}
			}
			resource_lock.unlock();
				resource_lock.lock();
				if (popup_view->is_visible) {
					std::string success_msg = std::string(LOCALIZED(OAUTH_AUTHENTICATED)) + "\n\n" + LOCALIZED(OAUTH_TOKEN_WARNING);
					popup_view->get_message_view()->set_text_lines(split_string(success_msg, '\n'))->update_y_range(0, 80);
					popup_view->set_buttons<std::function<std::string ()> >({
						[] () { return LOCALIZED(OK); }
					},
					[] (OverlayDialogView &, int button_pressed) {
						return true;
					});
				}
				resource_lock.unlock();
				var_need_refresh = true;
			} else if (OAuth::oauth_state == OAuth::OAuthState::ERROR || oauth_timeout_counter >= OAUTH_TIMEOUT_SECONDS) {
				oauth_check_timer_active = false;
				oauth_timeout_counter = 0;
				
				char log_msg[512];
				snprintf(log_msg, sizeof(log_msg), "OAuth error detected: %s", OAuth::oauth_error_message.c_str());
				logger.info("oauth_worker", log_msg);
				
				if (oauth_timeout_counter >= OAUTH_TIMEOUT_SECONDS) {
					OAuth::device_code = "";
					OAuth::user_code = "";
					OAuth::verification_url = "";
				}
				
				resource_lock.lock();
				if (popup_view->is_visible) {
					std::string error_msg;
					if (oauth_timeout_counter >= OAUTH_TIMEOUT_SECONDS) {
						error_msg = LOCALIZED(OAUTH_TIMEOUT);
					} else {
						error_msg = LOCALIZED(OAUTH_ERROR);
						if (!OAuth::oauth_error_message.empty()) {
							error_msg += "\n\n";
							error_msg += OAuth::oauth_error_message;
						}
					}
					popup_view->get_message_view()->set_text_lines(split_string(error_msg, '\n'))->update_y_range(0, 80);
					popup_view->set_buttons<std::function<std::string ()> >({
						[] () { return LOCALIZED(OK); }
					},
					[] (OverlayDialogView &, int button_pressed) {
						return true;
					});
				}
				resource_lock.unlock();
				var_need_refresh = true;
				
				usleep(5000000);
				resource_lock.lock();
				popup_view->set_is_visible(false);
				resource_lock.unlock();
				var_need_refresh = true;
			}
		}
		
		int sleep_seconds = (OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATING && oauth_check_timer_active) ?
			std::max(OAuth::interval, 8) : 2;
		usleep(sleep_seconds * 1000000);
	}
	
	logger.info("oauth_worker", "Thread exit.");
	threadExit(0);
}



void Sem_init(void) {
	logger.info("settings/init", "Initializing...");
	Result_with_string result;
	
	load_settings();
	load_string_resources(var_lang);
	
	popup_view = new OverlayDialogView(0, 0, 320, 240);
	popup_view->set_is_visible(false);
	toast_view = new TextView((320 - 150) / 2, 190, 150, DEFAULT_FONT_INTERVAL + SMALL_MARGIN);
	toast_view->set_is_visible(false);
	release_notes_view = new VerticalListView(0, 10, 320);
	release_notes_view->set_is_visible(false);
	
	main_tab_view = (new TabView(0, 0, 320, CONTENT_Y_HIGH - TOP_HEIGHT))
		->set_stretch_subview(true)
		->set_tab_font_size(0.4)
		->set_views({
			// Tab #1 : UI/Display
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// UI language
					(new SelectorView(0, 0, 320, 35, false))
						->set_texts({
							"English",
							"日本語",
							"Deutsch",
							"Français",
							"Italiano"
						}, var_lang == "ja" ? 1 : var_lang == "de" ? 2 : var_lang == "fr" ? 3 : var_lang == "it" ? 4 : 0)
						->set_title([](const SelectorView &) { return LOCALIZED(UI_LANGUAGE); })
						->set_on_change([](const SelectorView &view) {
							auto next_lang = std::vector<std::string>{"en", "ja", "de", "fr", "it"}[view.selected_button];
							if (var_lang != next_lang) {
								var_lang = next_lang;
								misc_tasks_request(TASK_RELOAD_STRING_RESOURCE);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Content language
					(new SelectorView(0, 0, 320, 35, false))
						->set_texts({
							"English",
							"日本語",
							"Deutsch",
							"Français",
							"Italiano"
						}, var_lang_content == "ja" ? 1 : var_lang_content == "de" ? 2 : var_lang_content == "fr" ? 3 : var_lang_content == "it" ? 4 : 0)
						->set_title([](const SelectorView &) { return LOCALIZED(CONTENT_LANGUAGE); })
						->set_on_change([](const SelectorView &view) {
							auto next_lang = std::vector<std::string>{"en", "ja", "de", "fr", "it"}[view.selected_button];
							if (var_lang_content != next_lang) {
								var_lang_content = next_lang;
								misc_tasks_request(TASK_SAVE_SETTINGS);
								youtube_change_content_language(var_lang_content);
							}
						}),
					// LCD Brightness
					(new BarView(0, 0, 320, 40))
						->set_values_sync(15, 163, &var_lcd_brightness)
						->set_title([] (const BarView &view) { return LOCALIZED(LCD_BRIGHTNESS); })
						->set_while_holding([] (const BarView &view) { misc_tasks_request(TASK_CHANGE_BRIGHTNESS); })
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Time to turn off LCD
					(new BarView(0, 0, 320, 40))
						->set_values(10, 310, var_time_to_turn_off_lcd <= 309 ? var_time_to_turn_off_lcd : 310)
						->set_title([] (const BarView &view) { return LOCALIZED(TIME_TO_TURN_OFF_LCD) + " : " +
							(view.get_value() <= 309 ? std::to_string((int) view.get_value()) + " " + LOCALIZED(SECONDS) : LOCALIZED(NEVER_TURN_OFF)); })
						->set_on_release([] (const BarView &view) {
							var_time_to_turn_off_lcd = view.get_value() <= 309 ? view.get_value() : 1000000000;
							misc_tasks_request(TASK_SAVE_SETTINGS);
						}),
					// full screen mode
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_full_screen_mode)
						->set_title([](const SelectorView &) { return LOCALIZED(FULL_SCREEN_MODE); })
						->set_on_change([](const SelectorView &view) {
							if (var_full_screen_mode != view.selected_button) {
								var_full_screen_mode = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// show full dislike/like
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_full_dislike_like_count)
						->set_title([](const SelectorView &) { return LOCALIZED(SHOW_FULL_DISLIKE); })
						->set_on_change([](const SelectorView &view) {
							if (var_full_dislike_like_count != view.selected_button) {
								var_full_dislike_like_count = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Hide pointer option
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_hide_pointer)
						->set_title([](const SelectorView &) { return LOCALIZED(HIDE_POINTER); })
						->set_on_change([](const SelectorView &view) {
							if (var_hide_pointer != view.selected_button) {
								var_hide_pointer = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Dark theme
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_night_mode)
						->set_title([](const SelectorView &) { return LOCALIZED(DARK_THEME); })
						->set_on_change([](const SelectorView &view) {
							if (var_night_mode != view.selected_button) {
								var_night_mode = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Scroll speed 0
					(new BarView(0, 0, 320, 35))
						->set_values_sync(DPAD_SCROLL_SPEED_MIN, DPAD_SCROLL_SPEED_MAX, &var_dpad_scroll_speed0)
						->set_title([] (const BarView &view) { return LOCALIZED(SCROLL_SPEED0) + " : " + double2str(var_dpad_scroll_speed0, 1); })
						->set_while_holding([] (const BarView &view) {
							var_dpad_scroll_speed1 = std::max(var_dpad_scroll_speed1, var_dpad_scroll_speed0);
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Scroll speed 1
					(new BarView(0, 0, 320, 35))
						->set_values_sync(DPAD_SCROLL_SPEED_MIN, DPAD_SCROLL_SPEED_MAX, &var_dpad_scroll_speed1)
						->set_title([] (const BarView &view) { return LOCALIZED(SCROLL_SPEED1) + " : " + double2str(var_dpad_scroll_speed1, 1); })
						->set_while_holding([] (const BarView &view) {
							var_dpad_scroll_speed0 = std::min(var_dpad_scroll_speed0, var_dpad_scroll_speed1);
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Scroll speed change threshold
					(new BarView(0, 0, 320, 40))
						->set_values_sync(DPAD_SCROLL_THRESHOLD_MIN, DPAD_SCROLL_THRESHOLD_MAX, &var_dpad_scroll_speed1_threshold)
						->set_title([] (const BarView &view) { return LOCALIZED(SCROLL_SPEED_THRESHOLD) + " : " + double2str(var_dpad_scroll_speed1_threshold, 1); })
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					// Size of images in community posts
					(new BarView(0, 0, 320, 40))
						->set_values_sync(COMMUNITY_IMAGE_SIZE_MIN, COMMUNITY_IMAGE_SIZE_MAX, &var_community_image_size)
						->set_title([] (const BarView &view) { return LOCALIZED(COMMUNITY_POST_IMAGE_SIZE) + " : " + std::to_string(var_community_image_size) + " px"; })
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); }),
					(new EmptyView(0, 0, 320, 10))
				}),
			// Tab #2 : Playback
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// Autoplay
					(new SelectorView(0, 0, 320, 35, false))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ONLY_IN_PLAYLIST); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_autoplay_level)
						->set_title([](const SelectorView &) { return LOCALIZED(AUTOPLAY); })
						->set_on_change([](const SelectorView &view) {
							if (var_autoplay_level != view.selected_button) {
								var_autoplay_level = view.selected_button;
								misc_tasks_request(TASK_CHANGE_BRIGHTNESS);
							}
						}),

					// Forward buffer ratio
					(new BarView(0, 0, 320, 40))
						->set_values_sync(0.1, 1.0, &var_forward_buffer_ratio)
						->set_title([] (const BarView &view) {
							char ratio_str[16];
							snprintf(ratio_str, 16, "%.2f", var_forward_buffer_ratio);
							return LOCALIZED(FORWARD_BUFFER_RATIO) + " : " + ratio_str;
						})
						->set_on_release([] (const BarView &view) { misc_tasks_request(TASK_SAVE_SETTINGS); })
				}),
			// Tab #3 : Data
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// History recording
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(DISABLED); },
							(std::function<std::string ()>) []() { return LOCALIZED(ENABLED); }
						}, var_history_enabled)
						->set_title([](const SelectorView &) { return LOCALIZED(WATCH_HISTORY); })
						->set_on_change([](const SelectorView &view) {
							if (var_history_enabled != view.selected_button) {
								var_history_enabled = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10)),
					// Erase history
					(new TextView(10, 0, 120, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text((std::function<std::string ()>) [] () { return LOCALIZED(REMOVE_ALL_HISTORY); })
						->set_x_alignment(TextView::XAlign::CENTER)
						->set_text_offset(0, -2)
						->set_get_background_color([] (const View &view) {
							int red = std::min<int>(0xFF, 0xD0 + 0x30 * view.touch_darkness);
							int other = 0x30 * (1 - view.touch_darkness);
							return 0xFF000000 | other << 8 | other << 16 | red;
						})
						->set_on_view_released([] (View &view) {
							popup_view->get_message_view()->set_text_lines(split_string(LOCALIZED(REMOVE_ALL_HISTORY_CONFIRM), '\n'))->update_y_range(0, 30);
							popup_view->set_buttons<std::function<std::string ()> >({
								[] () { return LOCALIZED(CANCEL); },
								[] () { return LOCALIZED(OK); }
							},
							[] (OverlayDialogView &, int button_pressed) {
								if (button_pressed == 1) {
									history_erase_all();
									misc_tasks_request(TASK_SAVE_HISTORY);
									
									float tabbed_content_y_high = CONTENT_Y_HIGH - main_tab_view->tab_selector_height;
									toast_view
										->set_text((std::function<std::string ()>) [] () { return LOCALIZED(ALL_HISTORY_REMOVED); })
										->set_x_alignment(TextView::XAlign::CENTER)
										->set_text_offset(0, -1)
										->set_get_text_color([] () { return (u32) -1; })
										->update_y_range(tabbed_content_y_high - 20, tabbed_content_y_high - 5)
										->set_get_background_color([] (const View &) { return 0x50000000; })
										->set_is_visible(true);
									toast_frames_left = 120;
								}
								return true; // close the dialog
							});
							popup_view->set_is_visible(true);
							var_need_refresh = true;
						}),
					(new EmptyView(0, 0, 320, 10))
				}),
			// Tab #4 : Update
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text((std::function<std::string ()>) [] () { return LOCALIZED(UPDATE); })
						->set_font_size(0.6, DEFAULT_FONT_INTERVAL)
						->set_text_offset(0, -2),
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN))
						->set_text((std::function<std::string ()>) [] () -> std::string {
							if (update_state == UpdateState::CHECKING_UPDATES) return LOCALIZED(CHECKING_FOR_UPDATES);
							if (update_state == UpdateState::FAILED_CHECKING) return LOCALIZED(FAILED_CHECKING_UPDATES) + " : " + update_error_message;
							if (update_state == UpdateState::UP_TO_DATE) return LOCALIZED(APP_UP_TO_DATE);
							if (update_state == UpdateState::UPDATES_AVAILABLE) return LOCALIZED(UPDATES_AVAILABLE) + " : " + next_version_str;
							if (update_state == UpdateState::INSTALLING) return install_progress_str;
							if (update_state == UpdateState::FAILED_INSTALLING) return LOCALIZED(UPDATE_FAIL) + " : " + update_error_message;
							if (update_state == UpdateState::SUCCEEDED_INSTALLING) return LOCALIZED(UPDATE_SUCCESS);
							return "";
						}),
					(new EmptyView(0, 0, 320, SMALL_MARGIN)),
					(update_progress_bar_view = (new ProgressBarView(10, 0, 300, 5)))
						->set_get_color([] () { return DEF_DRAW_BLUE; })
						->set_is_visible(false),
					(new EmptyView(0, 0, 320, SMALL_MARGIN)),
					(new TextView(10, 0, 100, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text((std::function<std::string ()>) [] () -> std::string {
							if (update_state == UpdateState::FAILED_CHECKING || update_state == UpdateState::UP_TO_DATE) return LOCALIZED(RETRY);
							if (update_state == UpdateState::UPDATES_AVAILABLE) return LOCALIZED(UPDATE);
							if (update_state == UpdateState::FAILED_INSTALLING) return LOCALIZED(RETRY);
							return "";
						})
						->set_x_alignment(TextView::XAlign::CENTER)
						->set_text_offset(0, -1)
						->set_on_view_released([] (const View &) {
							if (update_state == UpdateState::FAILED_CHECKING || update_state == UpdateState::UP_TO_DATE) {
								update_state = UpdateState::CHECKING_UPDATES;
								var_need_refresh = true;
							}
							if (update_state == UpdateState::UPDATES_AVAILABLE || update_state == UpdateState::FAILED_INSTALLING) {
								std::vector<std::string> confirm_lines;
								if (is_3dsx) confirm_lines = truncate_str(std::regex_replace(LOCALIZED(OVERWRITE_3DSX_CONFIRM), std::regex("%0"), path_3dsx), DIALOG_WIDTH, 3, 0.5, 0.5);
								else confirm_lines = {LOCALIZED(INSTALL_CIA_CONFIRM)};
								
								popup_view->get_message_view()->set_text_lines(confirm_lines)->update_y_range(0, 45);
								popup_view->set_buttons<std::function<std::string ()> >({
									[] () { return LOCALIZED(CANCEL); },
									[] () { return LOCALIZED(OK); }
								},
								[] (OverlayDialogView &, int button_pressed) {
									if (button_pressed == 1) update_state = UpdateState::INSTALLING;
									return true; // close the dialog
								});
								popup_view->set_is_visible(true);
								var_need_refresh = true;
							}
						})
						->set_get_background_color([] (const View &view) -> u32 {
							if (update_state == UpdateState::FAILED_CHECKING || update_state == UpdateState::UPDATES_AVAILABLE || update_state == UpdateState::UP_TO_DATE) {
								int blue = std::min<int>(0xFF, 0xB0 + 0x30 * view.touch_darkness);
								int other = 0x50 + 0x20 * (1 - view.touch_darkness);
								return 0xFF000000 | blue << 16 | other << 8 | other;
							}
							return DEFAULT_BACK_COLOR;
						}),
					(new RuleView(0, 6, 320, 1))
					    ->set_get_background_color([] (const View &) {return DEFAULT_BACK_COLOR; }),
					    release_notes_view,
					(new EmptyView(0, 0, 320, DEFAULT_FONT_INTERVAL))
				}),
			// Tab #5 : Advanced
			(new ScrollView(0, 0, 320, 0))
				->set_views({
					// Eco mode
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_eco_mode)
						->set_title([](const SelectorView &) { return LOCALIZED(ECO_MODE); })
						->set_info([](const SelectorView &) { return LOCALIZED(INFO_ECO_MODE); })
						->set_popup_height(60)
						->set_on_change([](const SelectorView &view) {
							if (var_eco_mode != view.selected_button) {
								var_eco_mode = view.selected_button;
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					// Use linear filter
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_video_linear_filter)
						->set_title([](const SelectorView &) { return LOCALIZED(LINEAR_FILTER); })
						->set_on_change([](const SelectorView &view) {
							if (var_video_linear_filter != view.selected_button) {
								var_video_linear_filter = view.selected_button;
								video_set_linear_filter_enabled(var_video_linear_filter);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10)),
					// Debug info in the control tab
					(new SelectorView(0, 0, 320, 35, true))
						->set_texts({
							(std::function<std::string ()>) []() { return LOCALIZED(OFF); },
							(std::function<std::string ()>) []() { return LOCALIZED(ON); }
						}, var_video_show_debug_info)
						->set_title([](const SelectorView &view) { return LOCALIZED(VIDEO_SHOW_DEBUG_INFO); })
						->set_on_change([](const SelectorView &view) {
							if (var_video_show_debug_info != view.selected_button) {
								var_video_show_debug_info = view.selected_button;
								video_set_show_debug_info(var_video_show_debug_info);
								misc_tasks_request(TASK_SAVE_SETTINGS);
							}
						}),
					(new EmptyView(0, 0, 320, 10)),
                    // Select app data to use
                    (new SelectorView(0, 0, 320, 35, false))
                        ->set_texts({
                            (std::function<std::string ()>) []() { return "Android"; },
                            (std::function<std::string ()>) []() { return "Android VR"; },
                            (std::function<std::string ()>) []() { return "visionOS"; }
                        }, var_player_response)
                        ->set_title([](const SelectorView &) { return LOCALIZED(PLAYER_RESPONSE); })
                        ->set_info([](const SelectorView &) { return LOCALIZED(INFO_PLAYER_RESPONSE); })
                        ->set_popup_height(50)
                        ->set_on_change([](const SelectorView &view) {
                            if (var_player_response != view.selected_button) {
                                var_player_response = view.selected_button;
                                if (view.selected_button == 0) {
                                    var_player_response = 0; // Android
                                } else if (view.selected_button == 1) {
                                    var_player_response = 1; // Android VR
                                } else if (view.selected_button == 2) {
                                    var_player_response = 2; // visionOS
                                }
                                misc_tasks_request(TASK_SAVE_SETTINGS);
                            }
                        }),
                    (new EmptyView(0, 0, 320, 10)),
					// OAuth Settings Section
					(new SectionTitleWithInfoView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_title([] (const SectionTitleWithInfoView &) { return LOCALIZED(OAUTH); })
						->set_info([] (const SectionTitleWithInfoView &) { return LOCALIZED(INFO_OAUTH); })
						->set_popup_height(85),
					(new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN))
						->set_text([] () -> std::string {
							const std::string prefix = LOCALIZED(OAUTH_STATUS) + ": ";
							switch (OAuth::oauth_state) {
								case OAuth::OAuthState::NOT_AUTHENTICATED: return prefix + LOCALIZED(OAUTH_NOT_AUTHENTICATED);
								case OAuth::OAuthState::AUTHENTICATED: return prefix + LOCALIZED(OAUTH_AUTHENTICATED);
								case OAuth::OAuthState::AUTHENTICATING: return prefix + LOCALIZED(OAUTH_AUTHENTICATING);
								case OAuth::OAuthState::ERROR: return prefix + LOCALIZED(OAUTH_ERROR);
								default: return "";
							}
						}),
					(new EmptyView(0, 0, 320, SMALL_MARGIN)),
					(oauth_user_view = (new SuccinctChannelView(0, 0, 320, 0))
						->set_name("")
						->set_auxiliary_lines({})
						->set_thumbnail_url("")),
					(new EmptyView(0, 0, 320, SMALL_MARGIN)),
					(new TextView(10, 0, 120, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
						->set_text([] () {
							return OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATED ? 
								LOCALIZED(OAUTH_LOGOUT) : LOCALIZED(OAUTH_LOGIN);
						})
						->set_x_alignment(TextView::XAlign::CENTER)
						->set_text_offset(0, -2)
						->set_get_background_color([] (const View &view) {
							if (OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATED) {
								int red = std::min(0xFF, (int)(0xD0 + 0x30 * view.touch_darkness));
								int other = (int)(0x30 * (1 - view.touch_darkness));
								return 0xFF000000 | other << 8 | other << 16 | red;
							}
							int blue = std::min(0xFF, (int)(0xB0 + 0x30 * view.touch_darkness));
							int other = (int)(0x50 + 0x20 * (1 - view.touch_darkness));
							return 0xFF000000 | blue << 16 | other << 8 | other;
						})
						->set_on_view_released([] (View &) {
							if (OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATED) {
								OAuth::revoke_tokens();
								var_oauth_enabled = false;
								misc_tasks_request(TASK_SAVE_SETTINGS);

								if (oauth_user_view) {
									oauth_user_view->set_name("");
									oauth_user_view->set_auxiliary_lines({});
									oauth_user_view->set_thumbnail_url("");
									oauth_user_view->thumbnail_handle = -1;
									oauth_user_view->set_height(0);
								}
							} else if (OAuth::oauth_state == OAuth::OAuthState::NOT_AUTHENTICATED) {
								OAuth::start_device_flow();
								if (OAuth::oauth_state == OAuth::OAuthState::AUTHENTICATING) {
									auto cancel_auth = [] () {
										oauth_check_timer_active = false;
										oauth_timeout_counter = 0;
										OAuth::revoke_tokens();
									};
									
									std::string message = LOCALIZED(OAUTH_LOGIN_INSTRUCTION) + "\n\n" +
										std::regex_replace(LOCALIZED(OAUTH_USER_CODE), std::regex("%0"), OAuth::user_code) + "\n" +
										LOCALIZED(OAUTH_VERIFICATION_URL) + "\n\n" + LOCALIZED(OAUTH_WAITING);
									
									popup_view->get_message_view()->set_text_lines(split_string(message, '\n'))->update_y_range(0, 120);
									popup_view->set_buttons<std::function<std::string ()> >({
										[] () { return LOCALIZED(CANCEL); }
									}, [cancel_auth] (OverlayDialogView &, int) {
										cancel_auth();
										return true;
									});
									popup_view->set_on_cancel([cancel_auth](OverlayView &view) {
										cancel_auth();
										view.set_is_visible(false);
										var_need_refresh = true;
									});
									popup_view->set_is_visible(true);
									oauth_check_timer_active = true;
									oauth_timeout_counter = oauth_check_frames = 0;
								}
							}
							var_need_refresh = true;
						}),
					(new EmptyView(0, 0, 320, 10)),
				})
		}, 0)
		->set_tab_texts<std::function<std::string ()> >({
			[] () { return LOCALIZED(SETTINGS_DISPLAY_UI); },
			[] () { return LOCALIZED(PLAYBACK); },
			[] () { return LOCALIZED(SETTINGS_DATA); },
			[] () { return LOCALIZED(UPDATE); },
			[] () { return LOCALIZED(SETTINGS_ADVANCED); }
		});
	main_view = (new VerticalListView(0, 0, 320))
		->set_views({
			// 'Settings'
			(new TextView(0, 0, 320, MIDDLE_FONT_INTERVAL))
				->set_text((std::function<std::string ()>) [] () { return LOCALIZED(SETTINGS); })
				->set_font_size(MIDDLE_FONT_SIZE, MIDDLE_FONT_INTERVAL)
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			// ---------------------
			(new RuleView(0, 0, 320, SMALL_MARGIN * 2))
				->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; }),
			main_tab_view
		})
		->set_draw_order({2, 1, 0});
		
	
	is_3dsx = envIsHomebrew();
	if (is_3dsx) {
		const char *arglist = envGetSystemArgList();
		int argc = *(u32 *) arglist;
		if (argc) {
			arglist += 4;
			path_3dsx = arglist;
			if (path_3dsx.substr(0, 6) == "sdmc:/") path_3dsx = path_3dsx.substr(5, path_3dsx.size() - 5);
		}
	}
	
	update_worker_thread = threadCreate(update_worker_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_LOW, 1, false);
	oauth_worker_thread = threadCreate(oauth_worker_thread_func, (void*)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_LOW, 1, false);
	
	// Initialize OAuth
	OAuth::init();
	if (OAuth::is_authenticated()) {
		var_oauth_enabled = true;

		std::string user_name = OAuth::get_user_account_name();
		std::string channel_id = OAuth::get_user_channel_id();
		std::string photo_url = OAuth::get_user_photo_url();
		
		if (oauth_user_view && !user_name.empty()) {
			oauth_user_view->set_name(user_name);
			oauth_user_view->set_auxiliary_lines({channel_id});
			oauth_user_view->set_thumbnail_url(photo_url);
			oauth_user_view->set_height(CHANNEL_ICON_HEIGHT);
			if (!photo_url.empty()) {
				oauth_user_view->thumbnail_handle = thumbnail_request(photo_url, SceneType::SETTINGS, PRIORITY_FOREGROUND, ThumbnailType::ICON);
			}
		}
	}
	
	show_info_popup_callback = show_info_popup;
	
	Sem_resume("");
	already_init = true;
}
void Sem_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;
	
	save_settings();
	OAuth::exit();
	
	u64 time_out = 10000000000;
	logger.info(DEF_MENU_EXIT_STR, "threadJoin()...", threadJoin(update_worker_thread, time_out));
	threadFree(update_worker_thread);
	logger.info(DEF_MENU_EXIT_STR, "oauth threadJoin()...", threadJoin(oauth_worker_thread, time_out));
	threadFree(oauth_worker_thread);
	
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	
	logger.info("settings/exit", "Exited.");
}
void Sem_suspend(void) {
	thread_suspend = true;
}
void Sem_resume(std::string arg) {
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_refresh = true;
}


void Sem_draw(void)
{
	Hid_info key;
	Util_hid_query_key_state(&key);
	
	thumbnail_set_active_scene(SceneType::SETTINGS);
	
	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = 240;
	if (video_playing_bar_show) CONTENT_Y_HIGH -= VIDEO_PLAYING_BAR_HEIGHT;
	main_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT);
	
	if(var_need_refresh || !var_eco_mode)
	{
		var_need_refresh = false;
		Draw_frame_ready();
		video_draw_top_screen();
		
		Draw_screen_ready(1, DEFAULT_BACK_COLOR);
		
		resource_lock.lock();
		main_view->draw();
		popup_view->draw();
		toast_view->draw();
		resource_lock.unlock();
		
		if (video_playing_bar_show) video_draw_playing_bar();
		draw_overlay_menu(CONTENT_Y_HIGH - main_tab_view->tab_selector_height - OVERLAY_MENU_ICON_SIZE);
		
		if(Util_expl_query_show_flag())
			Util_expl_draw();

		if(Util_err_query_error_show_flag())
			Util_err_draw();

		Draw_touch_pos();

		Draw_apply_draw();
	}
	else
		gspWaitForVBlank();
	
	if (--toast_frames_left <= 0) toast_view->set_is_visible(false);

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if(Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key);
		
		// toast_view is never 'updated'
		resource_lock.lock();
		if (popup_view->is_visible) popup_view->update(key);
		else main_view->update(key);
		resource_lock.unlock();
		
		if (video_playing_bar_show) video_update_playing_bar(key);
		
		if (key.p_b) global_intent.next_scene = SceneType::BACK;
	}
}
