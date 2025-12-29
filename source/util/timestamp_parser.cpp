#include "headers.hpp"

double Util_convert_time_to_seconds(const std::string &timestamp_str) {
	if (timestamp_str.empty()) {
		return -1.0;
	}

	int colon_count = 0;
	for (char c : timestamp_str) {
		if (c == ':') {
			colon_count++;
		}
	}

	int hours = 0, minutes = 0, seconds = 0;

	if (colon_count == 2) {
		if (sscanf(timestamp_str.c_str(), "%d:%d:%d", &hours, &minutes, &seconds) != 3) {
			return -1.0;
		}

		size_t first_colon = timestamp_str.find(':');
		size_t second_colon = timestamp_str.find(':', first_colon + 1);
		std::string min_str = timestamp_str.substr(first_colon + 1, second_colon - first_colon - 1);
		std::string sec_str = timestamp_str.substr(second_colon + 1);

		if (min_str.length() != 2 || sec_str.length() != 2 || hours < 0 || minutes < 0 || minutes >= 60 ||
		    seconds < 0 || seconds >= 60) {
			return -1.0;
		}

		return hours * 3600.0 + minutes * 60.0 + seconds;
	} else if (colon_count == 1) {
		if (sscanf(timestamp_str.c_str(), "%d:%d", &minutes, &seconds) != 2) {
			return -1.0;
		}

		size_t colon_pos = timestamp_str.find(':');
		std::string min_str = timestamp_str.substr(0, colon_pos);
		std::string sec_str = timestamp_str.substr(colon_pos + 1);

		if (sec_str.length() != 2 || minutes < 0 || seconds < 0 || seconds >= 60) {
			return -1.0;
		}

		return minutes * 60.0 + seconds;
	}

	return -1.0;
}

int Util_find_timestamp_in_text(const std::string &text, int start_pos, int *timestamp_start, int *timestamp_end,
                                double *timestamp_seconds) {
	if (!timestamp_start || !timestamp_end || !timestamp_seconds) {
		return -1;
	}

	int text_len = text.length();
	if (start_pos >= text_len || text_len < 4) {
		return -1;
	}

	for (int i = start_pos; i < text_len - 3; i++) {
		if (!isdigit(text[i])) {
			continue;
		}

		int digit_start = i;
		int j = i;

		while (j < text_len && (isdigit(text[j]) || text[j] == ':')) {
			j++;
		}

		int clean_end = j;
		while (clean_end > digit_start && text[clean_end - 1] == ':') {
			clean_end--;
		}

		int clean_len = clean_end - digit_start;
		if (clean_len < 4) {
			i = j - 1;
			continue;
		}

		int colon_count = 0;
		for (int k = digit_start; k < clean_end; k++) {
			if (text[k] == ':') {
				colon_count++;
			}
		}

		if (colon_count < 1 || colon_count > 2) {
			i = j - 1;
			continue;
		}

		std::string timestamp_candidate = text.substr(digit_start, clean_len);
		double parsed_seconds = Util_convert_time_to_seconds(timestamp_candidate);
		if (parsed_seconds >= 0.0) {
			*timestamp_start = digit_start;
			*timestamp_end = clean_end;
			*timestamp_seconds = parsed_seconds;
			return digit_start;
		}

		if (colon_count == 2) {
			int first_colon = -1, second_colon = -1;
			for (int k = digit_start; k < clean_end; k++) {
				if (text[k] == ':') {
					if (first_colon == -1) {
						first_colon = k;
					} else {
						second_colon = k;
						break;
					}
				}
			}

			if (first_colon != -1 && second_colon != -1) {
				int h_len = first_colon - digit_start;
				int m_len = second_colon - first_colon - 1;
				int s_len = clean_end - second_colon - 1;

				if (m_len == 2 && s_len == 1) {
					std::string timestamp_part = text.substr(digit_start, second_colon - digit_start);
					double h_mm_seconds = Util_convert_time_to_seconds(timestamp_part);
					if (h_mm_seconds >= 0.0) {
						*timestamp_start = digit_start;
						*timestamp_end = second_colon;
						*timestamp_seconds = h_mm_seconds;
						return digit_start;
					}
				} else if (m_len == 1 && s_len == 2) {
					int h, m, s;
					if (sscanf(timestamp_candidate.c_str(), "%d:%d:%d", &h, &m, &s) == 3 && h >= 0 && m >= 0 &&
					    m < 60 && s >= 0 && s < 60) {
						*timestamp_start = digit_start;
						*timestamp_end = clean_end;
						*timestamp_seconds = h * 3600.0 + m * 60.0 + s;
						return digit_start;
					}
				}
			}
		}

		i = j - 1;
	}

	return -1;
}
