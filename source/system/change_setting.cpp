#include "headers.hpp"

Result_with_string Util_cset_set_screen_brightness(bool top_screen, bool bottom_screen, int brightness) {
	gspLcdInit();
	Result_with_string result;

	if (top_screen) {
		result.code = GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_TOP, brightness);
		if (result.code != 0) {
			result.string = "GSPLCD_SetBrightnessRaw() failed.";
		}
	}
	if (bottom_screen) {
		result.code = GSPLCD_SetBrightnessRaw(GSPLCD_SCREEN_BOTTOM, brightness);
		if (result.code != 0) {
			result.string = "GSPLCD_SetBrightnessRaw() failed.";
		}
	}

	gspLcdExit();
	return result;
}

Result_with_string Util_cset_set_wifi_state(bool wifi_state) {
	nwmExtInit();
	Result_with_string result;

	result.code = NWMEXT_ControlWirelessEnabled(wifi_state);
	if (result.code != 0) {
		result.string = "NWMEXT_ControlWirelessEnabled() failed.";
	}

	nwmExtExit();
	return result;
}

Result_with_string Util_cset_set_screen_state(bool top_screen, bool bottom_screen, bool state) {
	gspLcdInit();
	Result_with_string result;

	if (top_screen) {
		if (state) {
			result.code = GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_TOP);
			if (result.code != 0) {
				result.string = "GSPLCD_PowerOnBacklight() failed.";
			}
		} else {
			result.code = GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_TOP);
			if (result.code != 0) {
				result.string = "GSPLCD_PowerOffBacklight() failed.";
			}
		}
	}
	if (bottom_screen) {
		if (state) {
			result.code = GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
			if (result.code != 0) {
				result.string = "GSPLCD_PowerOnBacklight() failed.";
			}
		} else {
			result.code = GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
			if (result.code != 0) {
				result.string = "GSPLCD_PowerOffBacklight() failed.";
			}
		}
	}

	gspLcdExit();
	return result;
}
