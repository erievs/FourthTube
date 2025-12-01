#include <deque>
#include <numeric>
#include <cmath>
#include "ui/views/scroll.hpp"
#include "ui/ui_common.hpp"
#include "variables.hpp"

void ScrollView::update_scroller(Hid_info key) {
	content_height = 0;
	for (auto view : views) {
		content_height += view->get_height();
	}
	content_height += std::max((int)views.size() - 1, 0) * margin;

	double scroll_max = std::max<double>(0, content_height - (y1 - y0));
	if (key.p_touch) {
		first_touch_x = key.touch_x;
		first_touch_y = key.touch_y;
		if (x0 <= key.touch_x && key.touch_x < x1 && y0 <= key.touch_y &&
		    key.touch_y < std::min(y0 + content_height, y1) && var_afk_time <= var_time_to_turn_off_lcd) {
			grabbed = true;
		}
	} else if (grabbed && !scrolling && key.touch_y != -1 && std::abs(key.touch_y - first_touch_y) >= 5) {
		scrolling = true;
		offset += first_touch_y - key.touch_y;
	} else if (scrolling && key.touch_y != -1) {
		offset += last_touch_y - key.touch_y;
	}
	offset += inertia;
	if (inertia) {
		var_need_refresh = true;
	}

	if (pull_to_refresh_enabled && pull_refresh_loading) {
		pull_refresh_animation_frame++;
		pull_refresh_rotation += 10.0f;
		if (pull_refresh_rotation >= 360.0f) {
			pull_refresh_rotation -= 360.0f;
		}
		var_need_refresh = true;
	}

	if (pull_to_refresh_enabled && offset < 0) {
		float consistent_pull_distance = pull_refresh_threshold * 0.8f;
		float trigger_threshold = consistent_pull_distance * 0.85f;

		if (offset < -consistent_pull_distance) {
			offset = -consistent_pull_distance;
			inertia = 0;
		}

		if (offset <= -trigger_threshold && !pull_refresh_triggered && !pull_refresh_loading) {
			if (key.touch_x == -1 && last_touch_x != -1) {
				pull_refresh_triggered = true;
				pull_refresh_loading = true;
				pull_refresh_animation_frame = 0;
				if (on_pull_refresh) {
					on_pull_refresh();
				}
			}
		}

		if (!pull_refresh_loading && offset < 0) {
			pull_refresh_rotation = (-offset / trigger_threshold) * 180.0f;
		}

		if (key.touch_x == -1) {
			if (pull_refresh_loading) {
				offset = -consistent_pull_distance;
				var_need_refresh = true;
			} else if (offset < -2) {
				offset = std::min(0.0, offset + 5.0);
				var_need_refresh = true;
			} else {
				offset = 0;
				if (pull_refresh_triggered) {
					pull_refresh_triggered = false;
					pull_refresh_rotation = 0.0f;
				}
			}
		}
	} else if (offset < 0) {
		offset = 0;
		inertia = 0;
		pull_refresh_triggered = false;
	}
	if (offset > scroll_max) {
		offset = scroll_max;
		inertia = 0;
	}

	if (grabbed && !scrolling) {
		selected_darkness = std::min(1.0, selected_darkness + 0.15);
	} else {
		selected_darkness = std::max(0.0, selected_darkness - 0.15);
	}
	if (key.touch_x == -1) {
		selected_darkness = 0;
	}

	if (key.touch_x != -1) {
		inertia = 0;
	} else if (inertia > 0) {
		inertia = std::max(0.0, inertia - 0.1);
	} else {
		inertia = std::min(0.0, inertia + 0.1);
	}
	if (scrolling && key.touch_x == -1 && touch_frames >= 4) {
		int sample = std::min<int>(3, touch_moves.size());
		float amount = std::accumulate(touch_moves.end() - sample, touch_moves.end(), 0.0f) / sample;
		// Util_log_save("scroller", "inertia start : " + std::to_string(amount));
		if (std::fabs(amount) >= 8) {
			inertia = -amount;
		}
	}

	if (key.touch_y != -1 && last_touch_y != -1) {
		touch_moves.push_back(key.touch_y - last_touch_y);
		if (touch_moves.size() > 10) {
			touch_moves.pop_front();
		}
	} else {
		touch_moves.clear();
	}
	if (key.touch_y == -1) {
		scrolling = grabbed = false;
		touch_frames = 0;
	} else {
		touch_frames++;
	}

	// c-pad scroll
	if (key.h_c_up || key.h_c_down) {
		if (key.h_c_up) {
			consecutive_cpad_scroll = std::max(0, consecutive_cpad_scroll) + 1;
		} else {
			consecutive_cpad_scroll = std::min(0, consecutive_cpad_scroll) - 1;
		}

		float scroll_amount = var_dpad_scroll_speed0;
		if (std::abs(consecutive_cpad_scroll) > var_dpad_scroll_speed1_threshold * 60) {
			scroll_amount = var_dpad_scroll_speed1;
		}
		if (key.h_c_up) {
			scroll_amount *= -1;
		}

		scroll(scroll_amount);
		var_need_refresh = true;
	} else {
		consecutive_cpad_scroll = 0;
	}

	last_touch_x = key.touch_x;
	last_touch_y = key.touch_y;
}
void ScrollView::draw_slider_bar() const {
	float displayed_height = y1 - y0;
	if (content_height > displayed_height) {
		float bar_len = displayed_height * displayed_height / content_height;
		float bar_pos = (float)offset / content_height * displayed_height;
		Draw_texture(var_square_image[0], DEF_DRAW_GRAY, x1 - 3, y0 + bar_pos, 3, bar_len);
	}

	if (pull_to_refresh_enabled && pull_refresh_loading) {
		u32 indicator_alpha = 200;
		u32 final_color = (indicator_alpha << 24) | DEF_DRAW_WEAK_GREEN;

		float center_x = x0 + (x1 - x0) / 2;
		float center_y = y0 + 20;

		float size = 14;

		int segments = 8;
		float segment_angle = 360.0f / segments;

		for (int i = 0; i < segments; i++) {
			float angle = (i * segment_angle + pull_refresh_rotation) * 3.14159f / 180.0f;
			float radius = size * 0.6f;
			float seg_x = center_x + cos(angle) * radius;
			float seg_y = center_y + sin(angle) * radius;

			u32 seg_alpha = (u32)((indicator_alpha * (segments - i % 4)) / segments);
			u32 seg_color = (seg_alpha << 24) | DEF_DRAW_WEAK_GREEN;

			Draw_texture(var_square_image[0], seg_color, seg_x - 1.5f, seg_y - 1.5f, 3, 3);
		}
	}
}
void ScrollView::on_resume() {
	last_touch_x = last_touch_y = -1;
	first_touch_x = first_touch_y = -1;
	touch_frames = 0;
	touch_moves.clear();
	selected_darkness = 0;
	scrolling = false;
	grabbed = false;
	pull_refresh_triggered = false;
	pull_refresh_loading = false;
	pull_refresh_rotation = 0.0f;
	pull_refresh_animation_frame = 0;
}
void ScrollView::reset() {
	on_resume();
	offset = 0;
}
