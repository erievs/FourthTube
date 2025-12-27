#pragma once
#include <vector>
#include "view.hpp"
#include "../ui_common.hpp"

struct SelectorView : public FixedSizeView {
  private:
	UI::FlexibleString<SelectorView> title;
	UI::FlexibleString<SelectorView> info_text;
	bool has_info = false;
	int popup_height = 30;

	static constexpr double MARGIN_BUTTON_RATIO = 5.0;
	bool isBoolean_ = false;

  public:
	using CallBackFuncType = std::function<void(const SelectorView &value)>;
	int holding_button = -1;

	int selected_button = 0;
	int changed_num = 0;

	int button_num = 0;
	std::vector<UI::FlexibleString<SelectorView>> button_texts;

	inline double get_title_height() const { return (std::string)title != "" ? DEFAULT_FONT_INTERVAL : 0; }
	inline double unit_num() const { return button_num * MARGIN_BUTTON_RATIO + (button_num + 1); }
	inline double margin_x_size() const { return (x1 - x0) / unit_num(); }
	inline double button_x_size() const { return margin_x_size() * MARGIN_BUTTON_RATIO; }
	inline double button_x_left(int button_id) const {
		return x0 + margin_x_size() * (1 + button_id * (1 + MARGIN_BUTTON_RATIO));
	}
	inline double button_x_right(int button_id) const {
		return x0 + margin_x_size() * (button_id + 1) * (1 + MARGIN_BUTTON_RATIO);
	}
	inline double button_y_pos() const { return y0 + get_title_height() + (y1 - y0 - get_title_height()) * 0.1; }
	inline double button_y_size() const { return (y1 - y0 - get_title_height()) * 0.8; }
	inline double get_button_id_from_x(double x) const {
		int id = (x - x0) / margin_x_size() / (1 + MARGIN_BUTTON_RATIO);
		if (id < 0 || id >= button_num) {
			return -1;
		}
		double remainder = (x - x0) - id * margin_x_size() * (1 + MARGIN_BUTTON_RATIO);
		if (remainder >= margin_x_size() && remainder <= margin_x_size() * (1 + MARGIN_BUTTON_RATIO)) {
			return id;
		}
		return -1;
	}

	CallBackFuncType on_change_func;

	SelectorView(double x0, double y0, double width, double height, bool isBoolean)
	    : View(x0, y0), FixedSizeView(x0, y0, width, height), isBoolean_(isBoolean) {}
	virtual ~SelectorView() {}

	void reset_holding_status_() override { holding_button = -1; }
	void on_scroll() override { holding_button = -1; }

	SelectorView *set_texts(const std::vector<UI::FlexibleString<SelectorView>> &button_texts,
	                        int init_selected) { // mandatory
		this->button_num = button_texts.size();
		this->button_texts = button_texts;
		this->selected_button = init_selected;
		return this;
	}
	/*
	SelectorView *set_texts(const std::vector<std::function<std::string (const SelectorView &)> > &button_texts, int
	init_selected) { // mandatory this->button_num = button_texts.size(); this->button_texts.resize(button_num); for
	(int i = 0; i < button_num; i++) this->button_texts[i] = UI::FlexibleString<SelectorView>(button_texts[i], *this);
	    this->selected_button = init_selected;
	    return this;
	}*/
	SelectorView *set_title(const std::string &title) {
		this->title = UI::FlexibleString<SelectorView>(title);
		return this;
	}
	SelectorView *set_title(std::function<std::string(const SelectorView &)> title_func) {
		this->title = UI::FlexibleString<SelectorView>(title_func, *this);
		return this;
	}
	SelectorView *set_info(const std::string &info_text) {
		this->info_text = UI::FlexibleString<SelectorView>(info_text);
		this->has_info = true;
		return this;
	}
	SelectorView *set_info(std::function<std::string(const SelectorView &)> info_func) {
		this->info_text = UI::FlexibleString<SelectorView>(info_func, *this);
		this->has_info = true;
		return this;
	}
	SelectorView *set_popup_height(int height) {
		this->popup_height = height;
		return this;
	}
	SelectorView *set_on_change(CallBackFuncType on_change_func) {
		this->on_change_func = on_change_func;
		return this;
	}

	void draw_() const override {
		Draw(title, x0 + SMALL_MARGIN, y0, 0.5, 0.5, DEFAULT_TEXT_COLOR);

		if (has_info) {
			double title_width = Draw_get_width(title, 0.5);
			double info_x = x0 + SMALL_MARGIN + title_width + 5;
			Draw("ⓘ", info_x, y0, 0.45, 0.5, 0xFFFF9030);
		}

		if (isBoolean_ && selected_button == 0) {
			Draw_texture(var_square_image[0], DEF_DRAW_WEAK_RED, button_x_left(selected_button), button_y_pos(),
			             button_x_size(), button_y_size());
		} else {
			Draw_texture(var_square_image[0], DEF_DRAW_WEAK_GREEN, button_x_left(selected_button), button_y_pos(),
			             button_x_size(), button_y_size());
		}
		for (int i = 0; i < button_num; i++) {
			Draw_x_centered(button_texts[i], button_x_left(i), button_x_right(i), button_y_pos(), 0.5, 0.5,
			                DEFAULT_TEXT_COLOR);
		}
	}
	void update_(Hid_info key) override {
		if (has_info && key.p_touch && key.touch_y >= y0 && key.touch_y < y0 + get_title_height()) {
			double title_width = Draw_get_width(title, 0.5);
			double info_x = x0 + SMALL_MARGIN + title_width + 5;
			double info_width = Draw_get_width("ⓘ", 0.45);

			if (key.touch_x >= info_x - 3 && key.touch_x < info_x + info_width + 3) {
				extern void (*show_info_popup_callback)(const std::string &, int);
				if (show_info_popup_callback) {
					show_info_popup_callback(info_text, popup_height);
				}
				return;
			}
		}

		if (key.p_touch && key.touch_y >= button_y_pos() && key.touch_y < button_y_pos() + button_y_size()) {
			holding_button = get_button_id_from_x(key.touch_x);
		}

		int current_holding_button = get_button_id_from_x(key.touch_x);
		if (key.touch_y != -1 && (key.touch_y < button_y_pos() || key.touch_y >= button_y_pos() + button_y_size())) {
			holding_button = -1;
		}

		if (holding_button != -1 && current_holding_button != -1 && holding_button != current_holding_button) {
			holding_button = -1;
		}
		if (holding_button != -1 && key.touch_x == -1) {
			selected_button = holding_button;
			var_need_refresh = true;
			if (on_change_func) {
				on_change_func(*this);
			}
			changed_num++;
			holding_button = -1;
		}
	}
};

struct GridSelectorView : public FixedSizeView {
  private:
	UI::FlexibleString<GridSelectorView> title;
	UI::FlexibleString<GridSelectorView> info_text;
	bool has_info = false;
	int popup_height = 30;

	static constexpr double MARGIN_BUTTON_RATIO = 5.0;
	bool isBoolean_ = false;

	std::vector<int> row_counts;

  public:
	using CallBackFuncType = std::function<void(const GridSelectorView &value)>;
	int holding_button = -1;

	int selected_button = 0;
	int changed_num = 0;

	int button_num = 0;
	std::vector<UI::FlexibleString<GridSelectorView>> button_texts;

	GridSelectorView *set_row_counts(const std::vector<int> &counts) {
		this->row_counts = counts;
		return this;
	}

	inline double get_title_height() const { return (std::string)title != "" ? DEFAULT_FONT_INTERVAL : 0; }

	inline int get_items_in_row(int row_idx) const {
		if (row_idx < (int)row_counts.size()) {
			return row_counts[row_idx];
		}
		return button_num; // Fallback
	}

	inline int get_num_rows() const {
		if (row_counts.empty()) {
			return 1;
		}
		return row_counts.size();
	}

	// Layout helpers per row
	inline double unit_num(int row_idx) const {
		int count = get_items_in_row(row_idx);
		return count * MARGIN_BUTTON_RATIO + (count + 1);
	}
	inline double margin_x_size(int row_idx) const { return (x1 - x0) / unit_num(row_idx); }
	inline double button_x_size(int row_idx) const { return margin_x_size(row_idx) * MARGIN_BUTTON_RATIO; }

	inline double button_x_left(int row_idx, int local_idx) const {
		return x0 + margin_x_size(row_idx) * (1 + local_idx * (1 + MARGIN_BUTTON_RATIO));
	}
	inline double button_x_right(int row_idx, int local_idx) const {
		return x0 + margin_x_size(row_idx) * (local_idx + 1) * (1 + MARGIN_BUTTON_RATIO);
	}

	// Vertical layout
	inline double content_height() const { return y1 - y0 - get_title_height(); }
	inline double row_height() const { return content_height() / get_num_rows(); }
	inline double relative_button_y_size() const { return row_height() * 0.8; }

	inline double button_y_pos(int row_idx) const {
		return y0 + get_title_height() + row_height() * row_idx + row_height() * 0.1;
	}
	inline double button_y_size() const { return relative_button_y_size(); }

	// Get row from button index
	std::pair<int, int> get_row_col_from_index(int index) const {
		if (row_counts.empty()) {
			return {0, index};
		}

		int current_row = 0;
		int items_seen = 0;
		for (int count : row_counts) {
			if (index < items_seen + count) {
				return {current_row, index - items_seen};
			}
			items_seen += count;
			current_row++;
		}
		return {-1, -1}; // Should not happen if index is valid
	}

	// Get global index from coordinates
	int get_button_id_from_pos(double x, double y) const {
		if (y < y0 + get_title_height()) {
			return -1;
		}

		int rows = get_num_rows();
		double relative_y = y - (y0 + get_title_height());
		int row_idx = (int)(relative_y / row_height());

		if (row_idx < 0 || row_idx >= rows) {
			return -1;
		}

		// Check if Y is within the button area of the row (skip margins)
		double row_start_y = button_y_pos(row_idx);
		if (y < row_start_y || y > row_start_y + button_y_size()) {
			return -1;
		}

		int items_in_row = get_items_in_row(row_idx);
		double m_x = margin_x_size(row_idx);

		int local_id = (x - x0) / m_x / (1 + MARGIN_BUTTON_RATIO);

		if (local_id < 0 || local_id >= items_in_row) {
			return -1;
		}

		double remainder = (x - x0) - local_id * m_x * (1 + MARGIN_BUTTON_RATIO);
		if (remainder >= m_x && remainder <= m_x * (1 + MARGIN_BUTTON_RATIO)) {
			// Found valid button
			int global_idx = 0;
			for (int i = 0; i < row_idx && i < (int)row_counts.size(); ++i) {
				global_idx += row_counts[i];
			}
			return global_idx + local_id;
		}

		return -1;
	}

	CallBackFuncType on_change_func;

	GridSelectorView(double x0, double y0, double width, double height, bool isBoolean)
	    : View(x0, y0), FixedSizeView(x0, y0, width, height), isBoolean_(isBoolean) {}
	virtual ~GridSelectorView() {}

	void reset_holding_status_() override { holding_button = -1; }
	void on_scroll() override { holding_button = -1; }

	GridSelectorView *set_texts(const std::vector<UI::FlexibleString<GridSelectorView>> &button_texts,
	                            int init_selected) {
		this->button_num = button_texts.size();
		this->button_texts = button_texts;
		this->selected_button = init_selected;
		return this;
	}

	GridSelectorView *set_title(const std::string &title) {
		this->title = UI::FlexibleString<GridSelectorView>(title);
		return this;
	}
	GridSelectorView *set_title(std::function<std::string(const GridSelectorView &)> title_func) {
		this->title = UI::FlexibleString<GridSelectorView>(title_func, *this);
		return this;
	}
	GridSelectorView *set_info(const std::string &info_text) {
		this->info_text = UI::FlexibleString<GridSelectorView>(info_text);
		this->has_info = true;
		return this;
	}
	GridSelectorView *set_info(std::function<std::string(const GridSelectorView &)> info_func) {
		this->info_text = UI::FlexibleString<GridSelectorView>(info_func, *this);
		this->has_info = true;
		return this;
	}
	GridSelectorView *set_popup_height(int height) {
		this->popup_height = height;
		return this;
	}
	GridSelectorView *set_on_change(CallBackFuncType on_change_func) {
		this->on_change_func = on_change_func;
		return this;
	}

	void draw_() const override {
		Draw(title, x0 + SMALL_MARGIN, y0, 0.5, 0.5, DEFAULT_TEXT_COLOR);

		if (has_info) {
			double title_width = Draw_get_width(title, 0.5);
			double info_x = x0 + SMALL_MARGIN + title_width + 5;
			Draw("ⓘ", info_x, y0, 0.45, 0.5, 0xFFFF9030);
		}

		auto pos = get_row_col_from_index(selected_button);
		int r = pos.first;
		int c = pos.second;

		if (r != -1) {
			if (isBoolean_ && selected_button == 0) {
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_RED, button_x_left(r, c), button_y_pos(r),
				             button_x_size(r), button_y_size());
			} else {
				Draw_texture(var_square_image[0], DEF_DRAW_WEAK_GREEN, button_x_left(r, c), button_y_pos(r),
				             button_x_size(r), button_y_size());
			}
		}

		int global_idx = 0;
		int rows = get_num_rows();
		for (int i = 0; i < rows; i++) {
			int items = get_items_in_row(i);
			for (int j = 0; j < items; j++) {
				if (global_idx < button_num) {
					Draw_x_centered(button_texts[global_idx], button_x_left(i, j), button_x_right(i, j),
					                button_y_pos(i), 0.5, 0.5, DEFAULT_TEXT_COLOR);
					global_idx++;
				}
			}
		}
	}
	void update_(Hid_info key) override {
		if (has_info && key.p_touch && key.touch_y >= y0 && key.touch_y < y0 + get_title_height()) {
			double title_width = Draw_get_width(title, 0.5);
			double info_x = x0 + SMALL_MARGIN + title_width + 5;
			double info_width = Draw_get_width("ⓘ", 0.45);

			if (key.touch_x >= info_x - 3 && key.touch_x < info_x + info_width + 3) {
				extern void (*show_info_popup_callback)(const std::string &, int);
				if (show_info_popup_callback) {
					show_info_popup_callback(info_text, popup_height);
				}
				return;
			}
		}

		int touched_btn = get_button_id_from_pos(key.touch_x, key.touch_y);

		if (key.p_touch && touched_btn != -1) {
			holding_button = touched_btn;
		}

		if (key.touch_y != -1 && touched_btn == -1) {
			holding_button = -1;
		}

		if (holding_button != -1 && touched_btn != -1 && holding_button != touched_btn) {
			holding_button = -1;
		}
		if (holding_button != -1 && key.touch_x == -1) {
			selected_button = holding_button;
			var_need_refresh = true;
			if (on_change_func) {
				on_change_func(*this);
			}
			changed_num++;
			holding_button = -1;
		}
	}
};
