#include "headers.hpp"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <numeric>

#include "scenes/home.hpp"
#include "scenes/search.hpp"
#include "scenes/video_player.hpp"
#include "data_io/subscription_util.hpp"
#include "youtube_parser/parser.hpp"
#include "ui/ui.hpp"
#include "ui/overlay.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "util/misc_tasks.hpp"
#include "util/async_task.hpp"
#include "oauth/oauth.hpp"
#include "rapidjson_wrapper.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 12

#define FEED_RELOAD_BUTTON_HEIGHT 18
#define TOP_HEIGHT 25
#define VIDEO_TITLE_MAX_WIDTH (320 - SMALL_MARGIN * 2 - VIDEO_LIST_THUMBNAIL_WIDTH)

namespace Home {
bool thread_suspend = false;
bool already_init = false;
bool exiting = false;

Mutex resource_lock;

YouTubeHomeResult home_info;
std::vector<SubscriptionChannel> subscribed_channels;
std::vector<SubscriptionChannel> oauth_subscribed_channels;
bool clicked_is_video;
std::string clicked_url;

bool last_oauth_state = false;

int feed_loading_progress = 0;
int feed_loading_total = 0;

int CONTENT_Y_HIGH = 240; // changes according to whether the video playing bar is drawn or not

VerticalListView *main_view = NULL;
TabView *main_tab_view = NULL;
ScrollView *home_tab_view = NULL;
VerticalListView *home_videos_list_view = NULL;
View *home_videos_bottom_view = new EmptyView(0, 0, 320, 0);
View *channels_tab_view = NULL;
ScrollView *local_channels_tab_view = NULL;
VerticalListView *local_channels_list_view = NULL;
ScrollView *oauth_channels_tab_view = NULL;
VerticalListView *oauth_channels_list_view = NULL;
View *feed_tab_view = NULL;
ScrollView *local_feed_videos_view = NULL;
VerticalListView *local_feed_videos_list_view = NULL;
ScrollView *oauth_feed_videos_view = NULL;
VerticalListView *oauth_feed_videos_list_view = NULL;
View *oauth_feed_videos_bottom_view = new EmptyView(0, 0, 320, 0);

std::string oauth_feed_continuation_token = "";
bool oauth_feed_has_more = true;
std::string oauth_feed_error = "";
}; // namespace Home
using namespace Home;

static void load_home_page(void *);
static void load_home_page_more(void *);
static void load_subscription_feed(void *);
static void load_oauth_subscription_feed(void *);
static void load_oauth_subscription_feed_more(void *);
static void load_oauth_subscribed_channels(void *);
static void update_subscribed_channels(const std::vector<SubscriptionChannel> &new_subscribed_channels);
static void update_oauth_subscribed_channels(const std::vector<SubscriptionChannel> &new_oauth_channels);

void Home_init(void) {
	logger.info("subsc/init", "Initializing...");

	home_videos_list_view = (new VerticalListView(0, 0, 320))
	                            ->set_margin(SMALL_MARGIN)
	                            ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	home_tab_view = (new ScrollView(0, 0, 320, 0))
	                    ->set_views({home_videos_list_view, home_videos_bottom_view})
	                    ->set_pull_to_refresh(!var_disable_pull_to_refresh, []() {
		                    if (!is_async_task_running(load_home_page)) {
			                    queue_async_task(load_home_page, NULL);
		                    }
	                    });
	local_channels_list_view = (new VerticalListView(0, 0, 320))
	                               ->set_margin(SMALL_MARGIN)
	                               ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	local_channels_tab_view = (new ScrollView(0, 0, 320, 0))->set_views({local_channels_list_view});
	oauth_channels_list_view = (new VerticalListView(0, 0, 320))
	                               ->set_margin(SMALL_MARGIN)
	                               ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	oauth_channels_tab_view = (new ScrollView(0, 0, 320, 0))
	                              ->set_views({oauth_channels_list_view})
	                              ->set_pull_to_refresh(!var_disable_pull_to_refresh, []() {
		                              if (!is_async_task_running(load_oauth_subscribed_channels)) {
			                              queue_async_task(load_oauth_subscribed_channels, NULL);
		                              }
	                              });

	if (OAuth::is_authenticated()) {
		channels_tab_view = (new TabView(0, 0, 320, 0))
		                        ->set_views({oauth_channels_tab_view, local_channels_tab_view})
		                        ->set_tab_texts<std::function<std::string()>>(
		                            {[]() { return LOCALIZED(ACCOUNT); }, []() { return LOCALIZED(LOCAL_CHANNELS); }})
		                        ->set_lr_tab_switch_enabled(false);
	} else {
		channels_tab_view = local_channels_tab_view;
	}
	local_feed_videos_list_view = (new VerticalListView(0, 0, 320))
	                                  ->set_margin(SMALL_MARGIN)
	                                  ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);

	if (var_disable_pull_to_refresh) {
		local_feed_videos_view = (new ScrollView(0, 0, 320, 0))->set_views({local_feed_videos_list_view});
		feed_tab_view =
		    (new VerticalListView(0, 0, 320))
		        ->set_views(
		            {(new TextView(0, 0, 320, FEED_RELOAD_BUTTON_HEIGHT))
		                 ->set_text((std::function<std::string()>)[]() {
			                 auto res = LOCALIZED(RELOAD);
			                 if (is_async_task_running(load_subscription_feed)) {
				                 res += " (" + std::to_string(feed_loading_progress) + "/" +
				                        std::to_string(feed_loading_total) + ")";
			                 }
			                 return res;
		                 })
		                 ->set_text_offset(SMALL_MARGIN, -1)
		                 ->set_on_view_released([](View &) {
			                 if (!is_async_task_running(load_subscription_feed)) {
				                 queue_async_task(load_subscription_feed, NULL);
			                 }
		                 })
		                 ->set_get_background_color([](const View &view) -> u32 {
			                 if (is_async_task_running(load_subscription_feed)) {
				                 return LIGHT0_BACK_COLOR;
			                 }
			                 return View::STANDARD_BACKGROUND(view);
		                 }),
		             (new RuleView(0, 0, 320, SMALL_MARGIN))->set_margin(0)->set_get_background_color([](const View &) {
			             return DEFAULT_BACK_COLOR;
		             }),
		             local_feed_videos_view})
		        ->set_draw_order({2, 1, 0});
	} else {
		local_feed_videos_view =
		    (new ScrollView(0, 0, 320, 0))->set_views({local_feed_videos_list_view})->set_pull_to_refresh(true, []() {
			    if (!is_async_task_running(load_subscription_feed)) {
				    queue_async_task(load_subscription_feed, NULL);
			    }
		    });
		feed_tab_view = local_feed_videos_view;
	}

	oauth_feed_videos_list_view = (new VerticalListView(0, 0, 320))
	                                  ->set_margin(SMALL_MARGIN)
	                                  ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);

	if (var_disable_pull_to_refresh) {
		oauth_feed_videos_view =
		    (new ScrollView(0, 0, 320, 0))->set_views({oauth_feed_videos_list_view, oauth_feed_videos_bottom_view});
	} else {
		oauth_feed_videos_view = (new ScrollView(0, 0, 320, 0))
		                             ->set_views({oauth_feed_videos_list_view, oauth_feed_videos_bottom_view})
		                             ->set_pull_to_refresh(true, []() {
			                             if (!is_async_task_running(load_oauth_subscription_feed)) {
				                             queue_async_task(load_oauth_subscription_feed, NULL);
			                             }
		                             });
	}

	if (OAuth::is_authenticated()) {
		if (var_disable_pull_to_refresh) {
			View *local_feed_tab = feed_tab_view;
			View *oauth_feed_tab =
			    (new VerticalListView(0, 0, 320))
			        ->set_views({(new TextView(0, 0, 320, FEED_RELOAD_BUTTON_HEIGHT))
			                         ->set_text((std::function<std::string()>)[]() {
				                         auto res = LOCALIZED(RELOAD);
				                         if (is_async_task_running(load_oauth_subscription_feed)) {
					                         res += " ...";
				                         }
				                         return res;
			                         })
			                         ->set_text_offset(SMALL_MARGIN, -1)
			                         ->set_on_view_released([](View &) {
				                         if (!is_async_task_running(load_oauth_subscription_feed)) {
					                         queue_async_task(load_oauth_subscription_feed, NULL);
				                         }
			                         })
			                         ->set_get_background_color([](const View &view) -> u32 {
				                         if (is_async_task_running(load_oauth_subscription_feed)) {
					                         return LIGHT0_BACK_COLOR;
				                         }
				                         return View::STANDARD_BACKGROUND(view);
			                         }),
			                     (new RuleView(0, 0, 320, SMALL_MARGIN))
			                         ->set_margin(0)
			                         ->set_get_background_color([](const View &) { return DEFAULT_BACK_COLOR; }),
			                     oauth_feed_videos_view})
			        ->set_draw_order({2, 1, 0});
			feed_tab_view = (new TabView(0, 0, 320, 0))
			                    ->set_views({oauth_feed_tab, local_feed_tab})
			                    ->set_tab_texts<std::function<std::string()>>(
			                        {[]() { return LOCALIZED(ACCOUNT); }, []() { return LOCALIZED(LOCAL_CHANNELS); }})
			                    ->set_lr_tab_switch_enabled(false);
		} else {
			feed_tab_view = (new TabView(0, 0, 320, 0))
			                    ->set_views({oauth_feed_videos_view, local_feed_videos_view})
			                    ->set_tab_texts<std::function<std::string()>>(
			                        {[]() { return LOCALIZED(ACCOUNT); }, []() { return LOCALIZED(LOCAL_CHANNELS); }})
			                    ->set_lr_tab_switch_enabled(false);
		}
	}
	main_tab_view = (new TabView(0, 0, 320, CONTENT_Y_HIGH - TOP_HEIGHT))
	                    ->set_views({home_tab_view, channels_tab_view, feed_tab_view})
	                    ->set_tab_texts<std::function<std::string()>>({[]() { return LOCALIZED(HOME); },
	                                                                   []() { return LOCALIZED(SUBSCRIBED_CHANNELS); },
	                                                                   []() { return LOCALIZED(NEW_VIDEOS); }});
	main_view = (new VerticalListView(0, 0, 320))
	                ->set_views({(new CustomView(0, 0, 320, 25))
	                                 ->set_draw([](const CustomView &) { return Search_get_search_bar_view()->draw(); })
	                                 ->set_update([](const CustomView &, Hid_info key) {
		                                 return Search_get_search_bar_view()->update(key);
	                                 }),
	                             main_tab_view})
	                ->set_draw_order({1, 0});

	queue_async_task(load_home_page, NULL);
	load_subscription();

	// Update subscribed channels after loading subscription data
	resource_lock.lock();
	update_subscribed_channels(get_valid_subscribed_channels());
	resource_lock.unlock();

	queue_async_task(load_subscription_feed, NULL);

	if (OAuth::is_authenticated()) {
		resource_lock.lock();
		update_oauth_subscribed_channels(get_oauth_subscribed_channels());
		resource_lock.unlock();
		queue_async_task(load_oauth_subscription_feed, NULL);
	}

	last_oauth_state = OAuth::is_authenticated();

	Home_resume("");
	already_init = true;
}
void Home_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;

	resource_lock.lock();

	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	feed_tab_view = NULL;
	local_feed_videos_view = NULL;
	local_feed_videos_list_view = NULL;
	oauth_feed_videos_view = NULL;
	oauth_feed_videos_list_view = NULL;
	channels_tab_view = NULL;
	local_channels_tab_view = NULL;
	local_channels_list_view = NULL;
	oauth_channels_tab_view = NULL;
	oauth_channels_list_view = NULL;

	resource_lock.unlock();

	logger.info("subsc/exit", "Exited.");
}
void Home_suspend(void) { thread_suspend = true; }

void Home_rebuild_channels_tab(void) {
	if (!already_init) {
		return;
	}

	resource_lock.lock();

	if (channels_tab_view) {
		std::vector<View *> main_views = main_tab_view->views;

		TabView *old_tab = dynamic_cast<TabView *>(channels_tab_view);
		if (old_tab) {
			old_tab->views.clear();
			delete old_tab;
		}
		channels_tab_view = NULL;

		oauth_channels_tab_view->set_pull_to_refresh(!var_disable_pull_to_refresh, []() {
			if (!is_async_task_running(load_oauth_subscribed_channels)) {
				queue_async_task(load_oauth_subscribed_channels, NULL);
			}
		});

		if (OAuth::is_authenticated()) {
			channels_tab_view =
			    (new TabView(0, 0, 320, 0))
			        ->set_views({oauth_channels_tab_view, local_channels_tab_view})
			        ->set_tab_texts<std::function<std::string()>>(
			            {[]() { return LOCALIZED(ACCOUNT); }, []() { return LOCALIZED(LOCAL_CHANNELS); }})
			        ->set_lr_tab_switch_enabled(false);
			update_oauth_subscribed_channels(get_oauth_subscribed_channels());
		} else {
			channels_tab_view = local_channels_tab_view;
		}

		main_views[1] = channels_tab_view;
		main_tab_view->views = main_views;
	}

	resource_lock.unlock();
	var_need_refresh = true;
}

void Home_rebuild_feed_tab(void) {
	if (!already_init) {
		return;
	}

	resource_lock.lock();

	if (feed_tab_view) {
		std::vector<View *> main_views = main_tab_view->views;

		TabView *old_tab = dynamic_cast<TabView *>(feed_tab_view);
		if (old_tab) {
			for (auto view : old_tab->views) {
				VerticalListView *vlist = dynamic_cast<VerticalListView *>(view);
				if (vlist) {
					delete vlist;
				}
			}
			old_tab->views.clear();
			delete old_tab;
		} else {
			VerticalListView *old_list = dynamic_cast<VerticalListView *>(feed_tab_view);
			if (old_list) {
				delete old_list;
			}
		}
		feed_tab_view = NULL;

		if (var_disable_pull_to_refresh) {
			local_feed_videos_view->set_pull_to_refresh(false, nullptr);
			feed_tab_view =
			    (new VerticalListView(0, 0, 320))
			        ->set_views({(new TextView(0, 0, 320, FEED_RELOAD_BUTTON_HEIGHT))
			                         ->set_text((std::function<std::string()>)[]() {
				                         auto res = LOCALIZED(RELOAD);
				                         if (is_async_task_running(load_subscription_feed)) {
					                         res += " (" + std::to_string(feed_loading_progress) + "/" +
					                                std::to_string(feed_loading_total) + ")";
				                         }
				                         return res;
			                         })
			                         ->set_text_offset(SMALL_MARGIN, -1)
			                         ->set_on_view_released([](View &) {
				                         if (!is_async_task_running(load_subscription_feed)) {
					                         queue_async_task(load_subscription_feed, NULL);
				                         }
			                         })
			                         ->set_get_background_color([](const View &view) -> u32 {
				                         if (is_async_task_running(load_subscription_feed)) {
					                         return LIGHT0_BACK_COLOR;
				                         }
				                         return View::STANDARD_BACKGROUND(view);
			                         }),
			                     (new RuleView(0, 0, 320, SMALL_MARGIN))
			                         ->set_margin(0)
			                         ->set_get_background_color([](const View &) { return DEFAULT_BACK_COLOR; }),
			                     local_feed_videos_view})
			        ->set_draw_order({2, 1, 0});
		} else {
			local_feed_videos_view->set_pull_to_refresh(true, []() {
				if (!is_async_task_running(load_subscription_feed)) {
					queue_async_task(load_subscription_feed, NULL);
				}
			});
			feed_tab_view = local_feed_videos_view;
		}

		if (OAuth::is_authenticated()) {
			if (var_disable_pull_to_refresh) {
				oauth_feed_videos_view->set_pull_to_refresh(false, nullptr);
				View *local_feed_tab = feed_tab_view;
				View *oauth_feed_tab =
				    (new VerticalListView(0, 0, 320))
				        ->set_views({(new TextView(0, 0, 320, FEED_RELOAD_BUTTON_HEIGHT))
				                         ->set_text((std::function<std::string()>)[]() {
					                         auto res = LOCALIZED(RELOAD);
					                         if (is_async_task_running(load_oauth_subscription_feed)) {
						                         res += " ...";
					                         }
					                         return res;
				                         })
				                         ->set_text_offset(SMALL_MARGIN, -1)
				                         ->set_on_view_released([](View &) {
					                         if (!is_async_task_running(load_oauth_subscription_feed)) {
						                         queue_async_task(load_oauth_subscription_feed, NULL);
					                         }
				                         })
				                         ->set_get_background_color([](const View &view) -> u32 {
					                         if (is_async_task_running(load_oauth_subscription_feed)) {
						                         return LIGHT0_BACK_COLOR;
					                         }
					                         return View::STANDARD_BACKGROUND(view);
				                         }),
				                     (new RuleView(0, 0, 320, SMALL_MARGIN))
				                         ->set_margin(0)
				                         ->set_get_background_color([](const View &) { return DEFAULT_BACK_COLOR; }),
				                     oauth_feed_videos_view})
				        ->set_draw_order({2, 1, 0});
				feed_tab_view =
				    (new TabView(0, 0, 320, 0))
				        ->set_views({oauth_feed_tab, local_feed_tab})
				        ->set_tab_texts<std::function<std::string()>>(
				            {[]() { return LOCALIZED(ACCOUNT); }, []() { return LOCALIZED(LOCAL_CHANNELS); }})
				        ->set_lr_tab_switch_enabled(false);
			} else {
				oauth_feed_videos_view->set_pull_to_refresh(true, []() {
					if (!is_async_task_running(load_oauth_subscription_feed)) {
						queue_async_task(load_oauth_subscription_feed, NULL);
					}
				});
				feed_tab_view =
				    (new TabView(0, 0, 320, 0))
				        ->set_views({oauth_feed_videos_view, local_feed_videos_view})
				        ->set_tab_texts<std::function<std::string()>>(
				            {[]() { return LOCALIZED(ACCOUNT); }, []() { return LOCALIZED(LOCAL_CHANNELS); }})
				        ->set_lr_tab_switch_enabled(false);
			}
		}

		main_views[2] = feed_tab_view;
		main_tab_view->views = main_views;
	}

	resource_lock.unlock();
	var_need_refresh = true;
}

void Home_update_pull_to_refresh(void) {
	if (!already_init) {
		return;
	}

	resource_lock.lock();

	home_tab_view->set_pull_to_refresh(!var_disable_pull_to_refresh, []() {
		if (!is_async_task_running(load_home_page)) {
			queue_async_task(load_home_page, NULL);
		}
	});

	oauth_channels_tab_view->set_pull_to_refresh(!var_disable_pull_to_refresh, []() {
		if (!is_async_task_running(load_oauth_subscribed_channels)) {
			queue_async_task(load_oauth_subscribed_channels, NULL);
		}
	});

	resource_lock.unlock();
	var_need_refresh = true;
}

void Home_resume(std::string arg) {
	(void)arg;

	// main_tab_view->on_resume();
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_refresh = true;

	// Rebuild tabs if login status changed
	bool current_oauth_state = OAuth::is_authenticated();
	if (current_oauth_state != last_oauth_state) {
		resource_lock.lock();
		update_subscribed_channels(get_valid_subscribed_channels());
		resource_lock.unlock();

		Home_rebuild_channels_tab();
		Home_rebuild_feed_tab();

		// Load OAuth subscription feed when logging in
		if (current_oauth_state && !is_async_task_running(load_oauth_subscription_feed)) {
			queue_async_task(load_oauth_subscription_feed, NULL);
		}

		last_oauth_state = current_oauth_state;
	}
}

// async functions
static SuccinctVideoView *convert_video_to_view(const YouTubeVideoSuccinct &video) {
	SuccinctVideoView *res = new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT);
	res->set_title_lines(truncate_str(video.title, 320 - (VIDEO_LIST_THUMBNAIL_WIDTH + 3), 2, 0.5, 0.5));
	res->set_auxiliary_lines({video.views_str, video.publish_date});
	res->set_bottom_right_overlay(video.duration_text);
	res->set_thumbnail_url(video.thumbnail_url);
	res->set_get_background_color(View::STANDARD_BACKGROUND);
	res->set_on_view_released([video](View &view) {
		clicked_url = video.url;
		clicked_is_video = true;
	});
	return res;
}
static void update_home_bottom_view(bool force_show_loading) {
	delete home_videos_bottom_view;
	if (home_info.has_more_results() || force_show_loading) {
		home_videos_bottom_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
		                              ->set_text([]() {
			                              if (home_info.error != "") {
				                              return home_info.error;
			                              } else {
				                              return LOCALIZED(LOADING);
			                              }
		                              })
		                              ->set_x_alignment(TextView::XAlign::CENTER)
		                              ->set_on_drawn([](View &) {
			                              if (!is_async_task_running(load_home_page) &&
			                                  !is_async_task_running(load_home_page_more) && home_info.error == "") {
				                              queue_async_task(load_home_page_more, NULL);
			                              }
		                              });
	} else {
		home_videos_bottom_view = new EmptyView(0, 0, 320, 0);
	}
	home_tab_view->set_views({home_videos_list_view, home_videos_bottom_view});
}

static void update_oauth_feed_bottom_view(bool force_show_loading) {
	delete oauth_feed_videos_bottom_view;
	if (oauth_feed_has_more || force_show_loading) {
		oauth_feed_videos_bottom_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
		                                    ->set_text([]() {
			                                    if (oauth_feed_error != "") {
				                                    return oauth_feed_error;
			                                    } else {
				                                    return LOCALIZED(LOADING);
			                                    }
		                                    })
		                                    ->set_x_alignment(TextView::XAlign::CENTER)
		                                    ->set_on_drawn([](View &) {
			                                    if (!is_async_task_running(load_oauth_subscription_feed) &&
			                                        !is_async_task_running(load_oauth_subscription_feed_more) &&
			                                        oauth_feed_error == "" && OAuth::is_authenticated()) {
				                                    queue_async_task(load_oauth_subscription_feed_more, NULL);
			                                    }
		                                    });
	} else {
		oauth_feed_videos_bottom_view = new EmptyView(0, 0, 320, 0);
	}
	oauth_feed_videos_view->set_views({oauth_feed_videos_list_view, oauth_feed_videos_bottom_view});
}

static void load_home_page(void *) {
	resource_lock.lock();
	update_home_bottom_view(true);
	var_need_refresh = true;
	resource_lock.unlock();

	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto results = youtube_load_home_page();
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	logger.info("home", "truncate/view creation start");
	std::vector<View *> new_videos_view;
	for (auto video : results.videos) {
		new_videos_view.push_back(convert_video_to_view(video));
	}
	logger.info("home", "truncate/view creation end");

	resource_lock.lock();
	if (results.error == "") {
		home_info = results;
		home_videos_list_view->recursive_delete_subviews();
		home_videos_list_view->set_views(new_videos_view);
		update_home_bottom_view(false);
	} else {
		home_info.error = results.error;
	}

	if (home_tab_view) {
		home_tab_view->finish_pull_refresh();
	}

	var_need_refresh = true;
	resource_lock.unlock();
}
static void load_home_page_more(void *) {
	auto new_result = home_info;
	new_result.load_more_results();

	logger.info("home-c", "truncate/view creation start");
	std::vector<View *> new_videos_view;
	for (size_t i = home_info.videos.size(); i < new_result.videos.size(); i++) {
		new_videos_view.push_back(convert_video_to_view(new_result.videos[i]));
	}
	logger.info("home-c", "truncate/view creation end");

	resource_lock.lock();
	home_info = new_result;
	if (new_result.error == "") {
		home_videos_list_view->views.insert(home_videos_list_view->views.end(), new_videos_view.begin(),
		                                    new_videos_view.end());
		update_home_bottom_view(false);
	}
	resource_lock.unlock();
}
static void load_subscription_feed(void *) {
	resource_lock.lock();
	auto channels = subscribed_channels;
	resource_lock.unlock();

	std::vector<std::string> ids;
	for (auto channel : channels) {
		ids.push_back(channel.id);
	}
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto results = youtube_load_channel_page_multi(ids, [](int cur, int total) {
		feed_loading_progress = cur;
		feed_loading_total = total;
		var_need_refresh = true;
	});
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	std::map<std::pair<int, int>, std::vector<YouTubeVideoSuccinct>> loaded_videos;
	for (auto result : results) {
		// update the subscription metadata at the same time
		if (result.name != "") {
			SubscriptionChannel new_info;
			new_info.id = result.id;
			new_info.url = result.url;
			new_info.name = result.name;
			new_info.icon_url = result.icon_url;
			new_info.subscriber_count_str = result.subscriber_count_str;
			subscription_unsubscribe(result.id);
			subscription_subscribe(new_info);
		}

		int loaded_cnt = 0;
		for (auto video : result.videos) {
			std::string date_number_str;
			for (auto c : video.publish_date) {
				if (isdigit(c)) {
					date_number_str.push_back(c);
				}
			}

			// 1 : seconds
			char *end;
			int number = strtoll(date_number_str.c_str(), &end, 10);
			if (*end) {
				logger.error("subsc", "failed to parse the integer in date : " + video.publish_date);
				continue;
			}
			int unit = -1;
			std::vector<std::vector<std::string>> unit_list = {{"second"}, {"minute"}, {"hour"}, {"day"},
			                                                   {"week"},   {"month"},  {"year"}};
			if (var_lang_content == "ja") {
				unit_list = {{"秒"}, {"分"}, {"時間"}, {"日"}, {"週間"}, {"月"}, {"年"}};
			} else if (var_lang_content == "de") {
				unit_list = {{"Sekunde"}, {"Minute"}, {"Stunde"}, {"Tag"}, {"Woche"}, {"Monat"}, {"Jahr"}};
			} else if (var_lang_content == "fr") {
				unit_list = {{"seconde"}, {"minute"}, {"heure"}, {"jour"}, {"semaine"}, {"mois"}, {"an"}};
			} else if (var_lang_content == "it") {
				unit_list = {{"second"}, {"minut"}, {"ora", "ore"}, {"giorn"}, {"settiman"}, {"mes"}, {"ann"}};
			} else if (var_lang_content != "en") {
				logger.error("i18n", "Units not found.");
			}
			for (size_t i = 0; i < unit_list.size(); i++) {
				bool matched = false;
				for (auto pattern : unit_list[i]) {
					if (video.publish_date.find(pattern) != std::string::npos) {
						matched = true;
						break;
					}
				}
				if (matched) {
					unit = i;
					break;
				}
			}
			if (unit == -1) {
				logger.error("subsc", "failed to parse the unit of date : " + video.publish_date);
				continue;
			}
			if (std::pair<int, int>{unit, number} > std::pair<int, int>{5, 2}) {
				continue; // more than 2 months old
			}
			loaded_cnt++;
			loaded_videos[{unit, number}].push_back(video);
		}
		logger.info("subsc", "loaded " + result.name + " : " + std::to_string(loaded_cnt) + " video(s)");
	}

	std::vector<View *> new_feed_video_views;
	for (auto &i : loaded_videos) {
		for (auto video : i.second) {
			SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT));

			cur_view->set_title_lines(truncate_str(video.title, VIDEO_TITLE_MAX_WIDTH, 2, 0.5, 0.5));
			cur_view->set_thumbnail_url(video.thumbnail_url);
			cur_view->set_auxiliary_lines({video.publish_date, video.views_str});
			cur_view->set_bottom_right_overlay(video.duration_text);
			cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
			cur_view->set_on_view_released([video](View &view) {
				clicked_url = video.url;
				clicked_is_video = true;
			});

			new_feed_video_views.push_back(cur_view);
		}
	}

	misc_tasks_request(TASK_SAVE_SUBSCRIPTION);

	resource_lock.lock();
	if (exiting) { // app shut down while loading
		resource_lock.unlock();
		if (local_feed_videos_view && !var_disable_pull_to_refresh) {
			local_feed_videos_view->finish_pull_refresh();
		}
		return;
	}
	update_subscribed_channels(get_valid_subscribed_channels());

	local_feed_videos_list_view->recursive_delete_subviews();
	local_feed_videos_list_view->views = new_feed_video_views;

	if (local_feed_videos_view && !var_disable_pull_to_refresh) {
		local_feed_videos_view->finish_pull_refresh();
	}

	resource_lock.unlock();
}

static void load_oauth_subscription_feed(void *) {
	if (!OAuth::is_authenticated()) {
		resource_lock.lock();
		if (oauth_feed_videos_view && !var_disable_pull_to_refresh) {
			oauth_feed_videos_view->finish_pull_refresh();
		}
		resource_lock.unlock();
		return;
	}

	resource_lock.lock();
	oauth_feed_continuation_token = "";
	oauth_feed_has_more = true;
	oauth_feed_error = "";
	update_oauth_feed_bottom_view(true);
	var_need_refresh = true;
	resource_lock.unlock();

	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	RJson data = OAuth::fetch_browse_data("FEsubscriptions");
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	logger.info("home/oauth_feed", "truncate/view creation start");
	std::vector<View *> new_oauth_feed_video_views;

	if (data.is_valid()) {
		auto contents = data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]
		                    ["content"]["sectionListRenderer"]["contents"];

		for (auto section : contents.array_items()) {
			if (!section.has_key("itemSectionRenderer")) {
				continue;
			}

			auto items = section["itemSectionRenderer"]["contents"];
			for (auto item : items.array_items()) {
				if (!item.has_key("compactVideoRenderer")) {
					continue;
				}

				auto renderer = item["compactVideoRenderer"];

				if (!renderer.has_key("videoId")) {
					continue;
				}

				std::string video_id = renderer["videoId"].string_value();
				std::string title = "";
				std::string publish_date = "";
				std::string views_str = "";
				std::string duration_text = "";
				std::string thumbnail_url = "";

				if (renderer.has_key("title") && renderer["title"].has_key("runs") &&
				    renderer["title"]["runs"].array_items().size() > 0) {
					title = renderer["title"]["runs"][(size_t)0]["text"].string_value();
				}

				if (renderer.has_key("publishedTimeText") && renderer["publishedTimeText"].has_key("runs") &&
				    renderer["publishedTimeText"]["runs"].array_items().size() > 0) {
					publish_date = renderer["publishedTimeText"]["runs"][(size_t)0]["text"].string_value();
				}

				if (renderer.has_key("shortViewCountText") && renderer["shortViewCountText"].has_key("accessibility") &&
				    renderer["shortViewCountText"]["accessibility"].has_key("accessibilityData") &&
				    renderer["shortViewCountText"]["accessibility"]["accessibilityData"].has_key("label")) {
					views_str =
					    renderer["shortViewCountText"]["accessibility"]["accessibilityData"]["label"].string_value();
				}

				if (renderer.has_key("lengthText") && renderer["lengthText"].has_key("runs") &&
				    renderer["lengthText"]["runs"].array_items().size() > 0) {
					duration_text = renderer["lengthText"]["runs"][(size_t)0]["text"].string_value();
				}

				if (renderer.has_key("thumbnail") && renderer["thumbnail"].has_key("thumbnails")) {
					auto thumbnails = renderer["thumbnail"]["thumbnails"];
					if (thumbnails.array_items().size() > 0) {
						if (thumbnails.array_items().size() >= 2) {
							thumbnail_url = thumbnails[(size_t)1]["url"].string_value();
						} else if (thumbnails.array_items().size() >= 1) {
							thumbnail_url = thumbnails[(size_t)0]["url"].string_value();
						}
					}
				}

				if (!video_id.empty() && !title.empty()) {
					SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT));

					cur_view->set_title_lines(truncate_str(title, VIDEO_TITLE_MAX_WIDTH, 2, 0.5, 0.5));
					cur_view->set_thumbnail_url(thumbnail_url);
					cur_view->set_auxiliary_lines({publish_date, views_str});
					cur_view->set_bottom_right_overlay(duration_text);
					cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
					cur_view->set_on_view_released([video_id](View &view) {
						clicked_url = "https://m.youtube.com/watch?v=" + video_id;
						clicked_is_video = true;
					});

					new_oauth_feed_video_views.push_back(cur_view);
				}
			}
		}

		if (data.has_key("contents") && data["contents"].has_key("singleColumnBrowseResultsRenderer") &&
		    data["contents"]["singleColumnBrowseResultsRenderer"].has_key("tabs")) {
			auto tabs = data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"];
			if (tabs.array_items().size() > 0 && tabs[(size_t)0].has_key("tabRenderer") &&
			    tabs[(size_t)0]["tabRenderer"].has_key("content") &&
			    tabs[(size_t)0]["tabRenderer"]["content"].has_key("sectionListRenderer") &&
			    tabs[(size_t)0]["tabRenderer"]["content"]["sectionListRenderer"].has_key("continuations")) {
				auto continuations = tabs[(size_t)0]["tabRenderer"]["content"]["sectionListRenderer"]["continuations"];
				if (continuations.array_items().size() > 0 &&
				    continuations[(size_t)0].has_key("nextContinuationData") &&
				    continuations[(size_t)0]["nextContinuationData"].has_key("continuation")) {
					oauth_feed_continuation_token =
					    continuations[(size_t)0]["nextContinuationData"]["continuation"].string_value();
				} else {
					oauth_feed_has_more = false;
				}
			} else {
				oauth_feed_has_more = false;
			}
		} else {
			oauth_feed_has_more = false;
		}
	} else {
		oauth_feed_error = "Failed to load subscriptions";
		oauth_feed_has_more = false;
	}

	logger.info("home/oauth_feed", "truncate/view creation end");

	resource_lock.lock();

	if (exiting) {
		resource_lock.unlock();
		if (oauth_feed_videos_view && !var_disable_pull_to_refresh) {
			oauth_feed_videos_view->finish_pull_refresh();
		}
		return;
	}

	oauth_feed_videos_list_view->recursive_delete_subviews();
	oauth_feed_videos_list_view->set_views(new_oauth_feed_video_views);
	update_oauth_feed_bottom_view(false);

	if (oauth_feed_videos_view && !var_disable_pull_to_refresh) {
		oauth_feed_videos_view->finish_pull_refresh();
	}

	var_need_refresh = true;
	resource_lock.unlock();
}

static void load_oauth_subscription_feed_more(void *) {
	if (!OAuth::is_authenticated() || oauth_feed_continuation_token.empty() || !oauth_feed_has_more) {
		return;
	}

	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	RJson data = OAuth::fetch_browse_data_with_continuation("FEsubscriptions", oauth_feed_continuation_token);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	std::vector<View *> new_oauth_feed_video_views;

	if (data.is_valid()) {
		auto continuation_contents = data["continuationContents"]["sectionListContinuation"]["contents"];

		for (auto section : continuation_contents.array_items()) {
			if (!section.has_key("itemSectionRenderer")) {
				continue;
			}

			auto items = section["itemSectionRenderer"]["contents"];
			for (auto item : items.array_items()) {
				if (!item.has_key("compactVideoRenderer")) {
					continue;
				}

				auto renderer = item["compactVideoRenderer"];

				if (!renderer.has_key("videoId")) {
					continue;
				}

				std::string video_id = renderer["videoId"].string_value();
				std::string title = "";
				std::string publish_date = "";
				std::string views_str = "";
				std::string duration_text = "";
				std::string thumbnail_url = "";

				if (renderer.has_key("title") && renderer["title"].has_key("runs") &&
				    renderer["title"]["runs"].array_items().size() > 0) {
					title = renderer["title"]["runs"][(size_t)0]["text"].string_value();
				}

				if (renderer.has_key("publishedTimeText") && renderer["publishedTimeText"].has_key("runs") &&
				    renderer["publishedTimeText"]["runs"].array_items().size() > 0) {
					publish_date = renderer["publishedTimeText"]["runs"][(size_t)0]["text"].string_value();
				}

				if (renderer.has_key("shortViewCountText") && renderer["shortViewCountText"].has_key("accessibility") &&
				    renderer["shortViewCountText"]["accessibility"].has_key("accessibilityData") &&
				    renderer["shortViewCountText"]["accessibility"]["accessibilityData"].has_key("label")) {
					views_str =
					    renderer["shortViewCountText"]["accessibility"]["accessibilityData"]["label"].string_value();
				}

				if (renderer.has_key("lengthText") && renderer["lengthText"].has_key("runs") &&
				    renderer["lengthText"]["runs"].array_items().size() > 0) {
					duration_text = renderer["lengthText"]["runs"][(size_t)0]["text"].string_value();
				}

				if (renderer.has_key("thumbnail") && renderer["thumbnail"].has_key("thumbnails")) {
					auto thumbnails = renderer["thumbnail"]["thumbnails"];
					if (thumbnails.array_items().size() > 0) {
						if (thumbnails.array_items().size() >= 2) {
							thumbnail_url = thumbnails[(size_t)1]["url"].string_value();
						} else if (thumbnails.array_items().size() >= 1) {
							thumbnail_url = thumbnails[(size_t)0]["url"].string_value();
						}
					}
				}

				if (!video_id.empty() && !title.empty()) {
					SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, VIDEO_LIST_THUMBNAIL_HEIGHT));

					cur_view->set_title_lines(truncate_str(title, VIDEO_TITLE_MAX_WIDTH, 2, 0.5, 0.5));
					cur_view->set_thumbnail_url(thumbnail_url);
					cur_view->set_auxiliary_lines({publish_date, views_str});
					cur_view->set_bottom_right_overlay(duration_text);
					cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
					cur_view->set_on_view_released([video_id](View &view) {
						clicked_url = "https://m.youtube.com/watch?v=" + video_id;
						clicked_is_video = true;
					});

					new_oauth_feed_video_views.push_back(cur_view);
				}
			}
		}

		oauth_feed_continuation_token = "";
		if (data.has_key("continuationContents") && data["continuationContents"].has_key("sectionListContinuation") &&
		    data["continuationContents"]["sectionListContinuation"].has_key("continuations")) {
			auto continuations = data["continuationContents"]["sectionListContinuation"]["continuations"];
			if (continuations.array_items().size() > 0 && continuations[(size_t)0].has_key("nextContinuationData") &&
			    continuations[(size_t)0]["nextContinuationData"].has_key("continuation")) {
				oauth_feed_continuation_token =
				    continuations[(size_t)0]["nextContinuationData"]["continuation"].string_value();
			} else {
				oauth_feed_has_more = false;
			}
		} else {
			oauth_feed_has_more = false;
		}

		if (oauth_feed_continuation_token.empty()) {
			oauth_feed_has_more = false;
		}
	} else {
		oauth_feed_error = "Failed to load more subscriptions";
		oauth_feed_has_more = false;
	}

	resource_lock.lock();

	if (exiting) {
		resource_lock.unlock();
		return;
	}

	oauth_feed_videos_list_view->views.insert(oauth_feed_videos_list_view->views.end(),
	                                          new_oauth_feed_video_views.begin(), new_oauth_feed_video_views.end());
	update_oauth_feed_bottom_view(false);

	var_need_refresh = true;
	resource_lock.unlock();
}

static void update_subscribed_channels(const std::vector<SubscriptionChannel> &new_subscribed_channels) {
	subscribed_channels = new_subscribed_channels;

	// prepare new views
	std::vector<View *> new_views;
	for (auto channel : new_subscribed_channels) {
		SuccinctChannelView *cur_view = (new SuccinctChannelView(0, 0, 320, CHANNEL_ICON_HEIGHT));
		cur_view->set_name(channel.name);
		cur_view->set_auxiliary_lines({channel.subscriber_count_str});
		cur_view->set_thumbnail_url(channel.icon_url);
		cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
		cur_view->set_on_view_released([channel](View &view) {
			clicked_url = channel.url;
			clicked_is_video = false;
		});
		new_views.push_back(cur_view);
	}

	local_channels_list_view->recursive_delete_subviews();
	local_channels_list_view->set_views(new_views);
}

static void update_oauth_subscribed_channels(const std::vector<SubscriptionChannel> &new_oauth_channels) {
	oauth_subscribed_channels = new_oauth_channels;

	std::vector<View *> new_views;
	for (auto channel : new_oauth_channels) {
		SuccinctChannelView *cur_view = (new SuccinctChannelView(0, 0, 320, CHANNEL_ICON_HEIGHT));
		cur_view->set_name(channel.name);
		cur_view->set_auxiliary_lines({channel.subscriber_count_str});
		cur_view->set_thumbnail_url(channel.icon_url);
		cur_view->set_get_background_color(View::STANDARD_BACKGROUND);
		cur_view->set_on_view_released([channel](View &view) {
			clicked_url = channel.url;
			clicked_is_video = false;
		});
		new_views.push_back(cur_view);
	}
	oauth_channels_list_view->swap_views(new_views);
}

static void load_oauth_subscribed_channels(void *) {
	if (!OAuth::is_authenticated()) {
		resource_lock.lock();
		if (oauth_channels_tab_view) {
			oauth_channels_tab_view->finish_pull_refresh();
		}
		resource_lock.unlock();
		return;
	}

	resource_lock.lock();
	auto new_oauth_channels = get_oauth_subscribed_channels();
	update_oauth_subscribed_channels(new_oauth_channels);

	if (oauth_channels_tab_view) {
		oauth_channels_tab_view->finish_pull_refresh();
	}

	var_need_refresh = true;
	resource_lock.unlock();
}

void Home_draw(void) {
	Hid_info key;
	Util_hid_query_key_state(&key);

	thumbnail_set_active_scene(SceneType::HOME);

	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT);

	if (var_need_refresh || !var_eco_mode) {
		var_need_refresh = false;
		Draw_frame_ready();
		video_draw_top_screen();

		Draw_screen_ready(1, DEFAULT_BACK_COLOR);

		resource_lock.lock();
		main_view->draw();
		resource_lock.unlock();

		if (video_playing_bar_show) {
			video_draw_playing_bar();
		}
		draw_overlay_menu(CONTENT_Y_HIGH - OVERLAY_MENU_ICON_SIZE - main_tab_view->tab_selector_height);

		if (Util_expl_query_show_flag()) {
			Util_expl_draw();
		}

		if (Util_err_query_error_show_flag()) {
			Util_err_draw();
		}

		Draw_touch_pos();

		Draw_apply_draw();
	} else {
		gspWaitForVBlank();
	}

	resource_lock.lock();

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if (Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key);

		home_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);

		TabView *channels_tab_as_tabview = dynamic_cast<TabView *>(channels_tab_view);
		if (channels_tab_as_tabview) {
			local_channels_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT -
			                                               main_tab_view->tab_selector_height -
			                                               channels_tab_as_tabview->tab_selector_height);
			oauth_channels_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT -
			                                               main_tab_view->tab_selector_height -
			                                               channels_tab_as_tabview->tab_selector_height);
		} else {
			local_channels_tab_view->update_y_range(0,
			                                        CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		}

		FixedHeightView *channels_fixed = dynamic_cast<FixedHeightView *>(channels_tab_view);
		if (channels_fixed) {
			channels_fixed->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		}

		TabView *feed_tab_as_tabview = dynamic_cast<TabView *>(feed_tab_view);
		if (feed_tab_as_tabview) {
			if (var_disable_pull_to_refresh) {
				local_feed_videos_view->update_y_range(
				    0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height -
				           feed_tab_as_tabview->tab_selector_height - FEED_RELOAD_BUTTON_HEIGHT - SMALL_MARGIN);
				oauth_feed_videos_view->update_y_range(
				    0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height -
				           feed_tab_as_tabview->tab_selector_height - FEED_RELOAD_BUTTON_HEIGHT - SMALL_MARGIN);
			} else {
				local_feed_videos_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT -
				                                              main_tab_view->tab_selector_height -
				                                              feed_tab_as_tabview->tab_selector_height);
				oauth_feed_videos_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT -
				                                              main_tab_view->tab_selector_height -
				                                              feed_tab_as_tabview->tab_selector_height);
			}
		} else {
			VerticalListView *feed_list = dynamic_cast<VerticalListView *>(feed_tab_view);
			if (feed_list && var_disable_pull_to_refresh) {
				local_feed_videos_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT -
				                                              main_tab_view->tab_selector_height -
				                                              FEED_RELOAD_BUTTON_HEIGHT - SMALL_MARGIN);
			} else {
				local_feed_videos_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT -
				                                              main_tab_view->tab_selector_height);
			}
		}

		FixedHeightView *feed_fixed = dynamic_cast<FixedHeightView *>(feed_tab_view);
		if (feed_fixed) {
			feed_fixed->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		}
		main_view->update(key);
		if (clicked_url != "") {
			global_intent.next_scene = clicked_is_video ? SceneType::VIDEO_PLAYER : SceneType::CHANNEL;
			global_intent.arg = clicked_url;
			clicked_url = "";
		}
		if (video_playing_bar_show) {
			video_update_playing_bar(key);
		}

		if (key.p_a) {
			Search_show_search_keyboard();
		}

		if (key.p_b) {
			global_intent.next_scene = SceneType::BACK;
		}
	}
	resource_lock.unlock();
}
