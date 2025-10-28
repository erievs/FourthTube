#include "ui/views/specialized/post.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "util/log.hpp"
#include "util/timestamp_parser.hpp"
#include "variables.hpp"

void PostView::cancel_all_thumbnail_requests() {
	thumbnail_cancel_request(author_icon_handle);
	thumbnail_cancel_request(additional_image_handle);
	author_icon_handle = additional_image_handle = -1;
	if (additional_video_view) {
		thumbnail_cancel_request(additional_video_view->thumbnail_handle);
		additional_video_view->thumbnail_handle = -1;
	}
}
void PostView::draw_() const {
	int cur_y = y0;
	
	// Skip author info for description mode
	if (!is_description_mode) {
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0) {
			float x = content_x_pos();
			Draw(author_name, x, cur_y - 3, 0.5, 0.5, LIGHT0_TEXT_COLOR);
			x += Draw_get_width(author_name + " ", 0.5);
			Draw(time_str, x, cur_y - 2, 0.45, 0.45, LIGHT1_TEXT_COLOR);
		}
		if (cur_y < 240 && cur_y + get_icon_size() > 0) {
			thumbnail_draw(author_icon_handle, x0 + SMALL_MARGIN, cur_y, get_icon_size(), get_icon_size());
		}
		cur_y += DEFAULT_FONT_INTERVAL;
	}

	for (size_t line = 0; line < lines_shown; line++) {
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0) {
			draw_content_line_with_timestamps(line, content_x_pos(), cur_y - 2);
		}
		cur_y += DEFAULT_FONT_INTERVAL;
	}
	if (lines_shown < content_lines.size()) {
		cur_y += SMALL_MARGIN;
		if (cur_y < 240 && cur_y + DEFAULT_FONT_INTERVAL > 0) {
			Draw(LOCALIZED(SHOW_MORE), content_x_pos(), cur_y - 2, 0.5, 0.5, DEF_DRAW_GRAY);
			if (show_more_holding) {
				Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, DEF_DRAW_GRAY,
				          content_x_pos() + Draw_get_width(LOCALIZED(SHOW_MORE), 0.5), cur_y + DEFAULT_FONT_INTERVAL,
				          DEF_DRAW_GRAY, 1);
			}
		}
		cur_y += DEFAULT_FONT_INTERVAL;
	}
	
	if (!is_description_mode) {
		cur_y = std::max<int>(cur_y, y0 + get_icon_size() + SMALL_MARGIN);
	}

	if (!is_description_mode) {
		if (additional_image_url != "") {
			cur_y += SMALL_MARGIN;
			int thumbnail_status_code = thumbnail_get_status_code(additional_image_handle);
			if (!thumbnail_is_available(additional_image_handle) &&
			    (thumbnail_status_code / 100 == 2 || thumbnail_status_code == 0)) {
				// webp(used for animated images) is currently unsupported
				Draw_texture(var_square_image[0], LIGHT0_BACK_COLOR, content_x_pos(), cur_y, COMMUNITY_IMAGE_SIZE,
				             COMMUNITY_IMAGE_SIZE);
				Draw_x_centered(LOCALIZED(UNSUPPORTED_IMAGE), content_x_pos(), content_x_pos() + COMMUNITY_IMAGE_SIZE,
				                cur_y + COMMUNITY_IMAGE_SIZE * 0.3, 0.5, 0.5, DEFAULT_TEXT_COLOR);
			} else {
				thumbnail_draw(additional_image_handle, content_x_pos(), cur_y, COMMUNITY_IMAGE_SIZE, COMMUNITY_IMAGE_SIZE);
			}
			cur_y += COMMUNITY_IMAGE_SIZE + SMALL_MARGIN;
		}
		if (additional_video_view) {
			cur_y += SMALL_MARGIN;
			additional_video_view->draw(content_x_pos(), cur_y);
			cur_y += SMALL_MARGIN + additional_video_view->get_height();
		}
		cur_y += SMALL_MARGIN;
		
		Draw_texture(var_texture_thumb_up[var_night_mode], content_x_pos(), cur_y, 16, 16);
		Draw(upvote_str, content_x_pos() + 16 + SMALL_MARGIN, cur_y + 1, 0.44, 0.44, LIGHT1_TEXT_COLOR);
		cur_y += 16 + SMALL_MARGIN;

		if (replies_shown) { // hide replies
			cur_y += SMALL_MARGIN;
			Draw(LOCALIZED(HIDE_REPLIES), content_x_pos(), cur_y - 2, 0.5, 0.5, COLOR_LINK);
			if (hide_replies_holding) {
				Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK,
				          content_x_pos() + Draw_get_width(LOCALIZED(HIDE_REPLIES), 0.5), cur_y + DEFAULT_FONT_INTERVAL,
				          COLOR_LINK, 1);
			}
			cur_y += DEFAULT_FONT_INTERVAL;
			cur_y += SMALL_MARGIN;
		}
		for (size_t i = 0; i < replies_shown; i++) {
			static_cast<View *>(replies[i])->draw(0, cur_y);
			cur_y += replies[i]->get_height();
		}
		if (is_loading_replies || get_has_more_replies() || replies_shown < replies.size()) { // load more replies
			cur_y += SMALL_MARGIN;
			std::string message = is_loading_replies ? LOCALIZED(LOADING)
			                      : replies_shown    ? LOCALIZED(SHOW_MORE_REPLIES)
			                                         : LOCALIZED(SHOW_REPLIES);
			Draw(message, content_x_pos(), cur_y - 2, 0.5, 0.5, COLOR_LINK);
			if (show_more_replies_holding) {
				Draw_line(content_x_pos(), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK,
				          content_x_pos() + Draw_get_width(message, 0.5), cur_y + DEFAULT_FONT_INTERVAL, COLOR_LINK, 1);
			}
			cur_y += DEFAULT_FONT_INTERVAL;
		}
	}
}
void PostView::update_(Hid_info key) {
	int cur_y = y0;
	
	// Skip author icon handling in description mode
	if (!is_description_mode) {
		bool inside_author_icon = in_range(key.touch_x, x0, std::min<float>(x1, x0 + get_icon_size() + SMALL_MARGIN)) &&
		                          in_range(key.touch_y, cur_y, cur_y + get_icon_size());

		if (key.p_touch && inside_author_icon) {
			icon_holding = true;
		}
		if (key.touch_x == -1 && icon_holding && on_author_icon_pressed_func) {
			on_author_icon_pressed_func(*this);
		}
		if (!inside_author_icon) {
			icon_holding = false;
		}
		cur_y += DEFAULT_FONT_INTERVAL;
	}

	// timestamp touch handling
	int content_line_y = cur_y;
	for (size_t line = 0; line < lines_shown; line++) {
		if (content_line_y < 240 && content_line_y + DEFAULT_FONT_INTERVAL > 0) {
			handle_timestamp_touch(key, line, content_x_pos(), content_line_y - 2);
		}
		content_line_y += DEFAULT_FONT_INTERVAL;
	}

	cur_y += lines_shown * DEFAULT_FONT_INTERVAL;

	if (lines_shown < content_lines.size()) {
		cur_y += SMALL_MARGIN;
		bool inside_show_more =
		    in_range(key.touch_x, content_x_pos(),
		             std::min<float>(x1, content_x_pos() + Draw_get_width(LOCALIZED(SHOW_MORE), 0.5))) &&
		    in_range(key.touch_y, cur_y, cur_y + DEFAULT_FONT_INTERVAL);

		if (key.p_touch && inside_show_more) {
			show_more_holding = true;
		}
		if (key.touch_x == -1 && show_more_holding) {
			lines_shown = std::min<size_t>(lines_shown + 50, content_lines.size());
			var_need_refresh = true;
		}
		if (!inside_show_more) {
			show_more_holding = false;
		}
		cur_y += DEFAULT_FONT_INTERVAL;
	}

	if (!is_description_mode) {
		cur_y = std::max<int>(cur_y, y0 + get_icon_size() + SMALL_MARGIN);

		if (additional_image_url != "") {
			cur_y += COMMUNITY_IMAGE_SIZE + SMALL_MARGIN * 2;
		}
		if (additional_video_view) {
			cur_y += SMALL_MARGIN;
			additional_video_view->update(key, content_x_pos(), cur_y);
			cur_y += additional_video_view->get_height() + SMALL_MARGIN;
		}
		
		cur_y += 16 + SMALL_MARGIN * 2;

		if (replies_shown) {
			cur_y += SMALL_MARGIN;
			bool inside_hide_replies =
			    in_range(key.touch_x, content_x_pos(),
			             std::min<float>(x1, content_x_pos() + Draw_get_width(LOCALIZED(HIDE_REPLIES), 0.5))) &&
			    in_range(key.touch_y, cur_y, cur_y + DEFAULT_FONT_INTERVAL + 1);

			if (key.p_touch && inside_hide_replies) {
				hide_replies_holding = true;
			}
			if (key.touch_x == -1 && hide_replies_holding) {
				replies_shown = 0;
				var_need_refresh = true;
			}
			if (!inside_hide_replies) {
				hide_replies_holding = false;
			}
			cur_y += DEFAULT_FONT_INTERVAL;
			cur_y += SMALL_MARGIN;
		}
		for (size_t i = 0; i < replies_shown; i++) {
			float cur_height = replies[i]->get_height();
			if (cur_y < 240 && cur_y + cur_height > 0) {
				static_cast<View *>(replies[i])->update(key, 0, cur_y);
			}
			cur_y += cur_height;
		}
		if (is_loading_replies || get_has_more_replies() || replies_shown < replies.size()) {
			cur_y += SMALL_MARGIN;
			std::string message = is_loading_replies ? LOCALIZED(LOADING)
			                      : replies_shown    ? LOCALIZED(SHOW_MORE_REPLIES)
			                                         : LOCALIZED(SHOW_REPLIES);
			bool inside_show_more_replies = in_range(key.touch_x, content_x_pos(),
			                                         std::min<float>(x1, content_x_pos() + Draw_get_width(message, 0.5))) &&
			                                in_range(key.touch_y, cur_y, cur_y + DEFAULT_FONT_INTERVAL + 1);

			if (key.p_touch && inside_show_more_replies) {
				show_more_replies_holding = true;
			}
			if (is_loading_replies) {
				show_more_replies_holding = false;
			}
			if (key.touch_x == -1 && show_more_replies_holding) {
				if (replies_shown < replies.size()) {
					replies_shown = replies.size();
					var_need_refresh = true;
				} else if (on_load_more_replies_pressed_func) {
					on_load_more_replies_pressed_func(*this);
				}
			}
			if (!inside_show_more_replies) {
				show_more_replies_holding = false;
			}
			cur_y += DEFAULT_FONT_INTERVAL;
		}
	}
}

void PostView::parse_timestamps_from_content() {
	timestamps.clear();
	
	for (size_t line_idx = 0; line_idx < content_lines.size(); line_idx++) {
		const std::string& line_text = content_lines[line_idx];
		if (line_text.length() < 4) continue;
		
		int search_pos = 0;

		while (search_pos < (int)line_text.length() - 3) {
			int timestamp_start, timestamp_end;
			double timestamp_seconds;
			
			int found_pos = Util_find_timestamp_in_text(
				line_text, search_pos, &timestamp_start, &timestamp_end, &timestamp_seconds);
				
			if (found_pos == -1) {
				break;
			}

			if (timestamp_start >= 0 && timestamp_end > timestamp_start && 
				timestamp_end <= (int)line_text.length() && timestamp_seconds >= 0.0) {
				timestamps.emplace_back(line_idx, timestamp_start, timestamp_end, timestamp_seconds);
			}
			
			search_pos = std::max(timestamp_end, search_pos + 1);
		}
	}
}

void PostView::reset_timestamp_holding_status() {
	for (auto& timestamp : timestamps) {
		timestamp.is_holding = false;
	}
}

void PostView::draw_content_line_with_timestamps(size_t line_index, float x, float y) const {
	if (line_index >= content_lines.size()) {
		return;
	}
	
	const std::string& line = content_lines[line_index];

	std::vector<const TimestampInfo*> line_timestamps;
	for (const auto& timestamp : timestamps) {
		if (timestamp.line_index == (int)line_index) {
			if (timestamp.start_pos >= 0 && timestamp.end_pos > timestamp.start_pos &&
				timestamp.start_pos < (int)line.length() && timestamp.end_pos <= (int)line.length()) {
				line_timestamps.push_back(&timestamp);
			}
		}
	}
	
	if (line_timestamps.empty()) {
		Draw(line, x, y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		return;
	}

	int last_end = 0;
	float current_x = x;
	
	for (const auto* timestamp : line_timestamps) {
		if (timestamp->start_pos < 0 || timestamp->end_pos <= timestamp->start_pos ||
			timestamp->end_pos > (int)line.length()) {
			continue;
		}
		
		if (timestamp->start_pos > last_end) {
			int before_len = std::min(timestamp->start_pos - last_end, (int)line.length() - last_end);
			if (before_len > 0) {
				std::string before_text = line.substr(last_end, before_len);
				Draw(before_text, current_x, y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
				current_x += Draw_get_width(before_text, 0.5);
			}
		}

		int text_len = std::min(timestamp->end_pos - timestamp->start_pos, 
		                       (int)line.length() - timestamp->start_pos);
		if (text_len > 0) {
			std::string timestamp_text = line.substr(timestamp->start_pos, text_len);
			Draw(timestamp_text, current_x, y, 0.5, 0.5, COLOR_LINK);

			if (timestamp->is_holding) {
				float timestamp_width = Draw_get_width(timestamp_text, 0.5);
				Draw_line(current_x, y + DEFAULT_FONT_INTERVAL, COLOR_LINK,
				          current_x + timestamp_width, y + DEFAULT_FONT_INTERVAL, COLOR_LINK, 1);
			}
			
			current_x += Draw_get_width(timestamp_text, 0.5);
		}
		last_end = std::max(last_end, timestamp->end_pos);
	}

	if (last_end < (int)line.length()) {
		int after_len = (int)line.length() - last_end;
		if (after_len > 0) {
			std::string after_text = line.substr(last_end, after_len);
			Draw(after_text, current_x, y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		}
	}
}

void PostView::handle_timestamp_touch(Hid_info key, size_t line_index, float line_x, float line_y) {
	if (line_index >= content_lines.size()) {
		return;
	}
	
	const std::string& line = content_lines[line_index];

	for (auto& timestamp : timestamps) {
		if (timestamp.line_index != (int)line_index) {
			continue;
		}

		if (timestamp.start_pos < 0 || timestamp.end_pos <= timestamp.start_pos ||
			timestamp.start_pos >= (int)line.length() || timestamp.end_pos > (int)line.length()) {
			continue;
		}

		float timestamp_x = line_x;

		if (timestamp.start_pos > 0 && timestamp.start_pos <= (int)line.length()) {
			std::string before_text = line.substr(0, std::min(timestamp.start_pos, (int)line.length()));
			timestamp_x += Draw_get_width(before_text, 0.5);
		}

		int text_start = std::max(0, timestamp.start_pos);
		int text_len = std::min(timestamp.end_pos - text_start, (int)line.length() - text_start);
		if (text_len <= 0) continue;
		
		std::string timestamp_text = line.substr(text_start, text_len);
		float timestamp_width = Draw_get_width(timestamp_text, 0.5);

		bool inside_timestamp = in_range(key.touch_x, timestamp_x, std::min<float>(x1, timestamp_x + timestamp_width)) &&
		                       in_range(key.touch_y, line_y, line_y + DEFAULT_FONT_INTERVAL);
		
		if (key.p_touch && inside_timestamp) {
			timestamp.is_holding = true;
		}
		
		if (key.touch_x == -1 && timestamp.is_holding && on_timestamp_pressed_func) {
			on_timestamp_pressed_func(timestamp.seconds);
		}
		
		if (!inside_timestamp) {
			timestamp.is_holding = false;
		}
	}
}
