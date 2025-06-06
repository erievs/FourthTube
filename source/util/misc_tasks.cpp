#include "misc_tasks.hpp"
#include "data_io/settings.hpp"
#include "data_io/history.hpp"
#include "data_io/subscription_util.hpp"
#include "data_io/string_resource.hpp"
#include "system/change_setting.hpp"
#include "headers.hpp"

static bool should_be_running = true;
static bool request[100];

void misc_tasks_request(int type) { request[type] = true; }

void misc_tasks_thread_func(void *arg) {
	(void)arg;

	while (should_be_running) {
		if (request[TASK_SAVE_SETTINGS]) {
			request[TASK_SAVE_SETTINGS] = false;
			save_settings();
		} else if (request[TASK_CHANGE_BRIGHTNESS]) {
			request[TASK_CHANGE_BRIGHTNESS] = false;
			Util_cset_set_screen_brightness(true, true, var_lcd_brightness);
		} else if (request[TASK_RELOAD_STRING_RESOURCE]) {
			request[TASK_RELOAD_STRING_RESOURCE] = false;
			load_string_resources(var_lang);
		} else if (request[TASK_SAVE_HISTORY]) {
			request[TASK_SAVE_HISTORY] = false;
			save_watch_history();
		} else if (request[TASK_SAVE_SUBSCRIPTION]) {
			request[TASK_SAVE_SUBSCRIPTION] = false;
			save_subscription();
		} else {
			usleep(50000);
		}
	}

	logger.info("misc-task", "Thread exit.");
	threadExit(0);
}
void misc_tasks_thread_exit_request() { should_be_running = false; }
