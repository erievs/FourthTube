#include "headers.hpp"
#include "oauth.hpp"
#include "network_decoder/network_io.hpp"
#include "data_io/settings.hpp"
#include "util/log.hpp"
#include "youtube_parser/internal_common.hpp"
#include <3ds/services/ps.h>
#include <cstring>
#include <regex>

namespace OAuth {
const char *CLIENT_ID = "652469312169-4lvs9bnhr9lpns9v451j5oivd81vjvu1.apps.googleusercontent.com";
const char *CLIENT_SECRET = "3fTWrBJI5Uojm1TK7_iJCW5Z";
const char *DEVICE_CODE_URL = "https://www.youtube.com/o/oauth2/device/code";
const char *TOKEN_URL = "https://www.youtube.com/o/oauth2/token";
const char *REVOKE_URL = "https://www.youtube.com/o/oauth2/revoke";
const char *SCOPE = "https://www.googleapis.com/auth/youtube";
const char *GRANT_TYPE_DEVICE = "http://oauth.net/grant_type/device/1.0";
const char *GRANT_TYPE_REFRESH = "refresh_token";

OAuthState oauth_state = OAuthState::NOT_AUTHENTICATED;
std::string oauth_error_message = "";
std::string device_code = "";
std::string user_code = "";
std::string verification_url = "";
int expires_in = 0;
int interval = 5;
std::string access_token = "";
std::string refresh_token = "";

std::string user_account_name = "";
std::string user_channel_id = "";
std::string user_photo_url = "";

static NetworkSessionList *session_list = nullptr;

static NetworkSessionList &get_session() {
	if (!session_list) {
		session_list = new NetworkSessionList();
		session_list->init();
	}
	return *session_list;
}

void init() {
	psInit();

	load_tokens();
	if (!access_token.empty()) {
		oauth_state = OAuthState::AUTHENTICATED;
	}
}

void exit() {
	save_tokens();
	if (session_list) {
		delete session_list;
		session_list = nullptr;
	}

	psExit();
}

void start_device_flow() {
	oauth_state = OAuthState::AUTHENTICATING;
	oauth_error_message = "";
	device_code = "";
	user_code = "";
	verification_url = "";
	expires_in = 0;
	interval = 5;

	std::string device_id = "3ds-" + std::to_string(time(nullptr));
	std::string post_data = "client_id=" + std::string(CLIENT_ID) + "&scope=" + std::string(SCOPE) +
	                        "&device_id=" + device_id + "&device_model=NINTENDO3DS";

	auto result = get_session().perform(
	    HttpRequest::POST(DEVICE_CODE_URL,
	                      {{"Host", "www.youtube.com"},
	                       {"User-Agent", "Mozilla/5.0 (Linux; Android 12; Quest 2) AppleWebKit/537.36"},
	                       {"Content-Type", "application/x-www-form-urlencoded"}},
	                      post_data));

	if (result.fail) {
		oauth_error_message = "Network error: " + result.error;
		oauth_state = OAuthState::ERROR;
		return;
	}

	if (result.status_code != 200) {
		oauth_error_message = "HTTP error: " + std::to_string(result.status_code);
		oauth_state = OAuthState::ERROR;
		return;
	}

	result.data.push_back('\0');
	rapidjson::Document json_root;
	std::string error;
	RJson response_json = RJson::parse(json_root, (char *)result.data.data(), error);

	if (!error.empty()) {
		oauth_error_message = "JSON error: " + error;
		oauth_state = OAuthState::ERROR;
		return;
	}

	if (response_json.has_key("error")) {
		oauth_error_message = "OAuth error: " + response_json["error"].string_value();
		oauth_state = OAuthState::ERROR;
		return;
	}

	device_code = response_json["device_code"].string_value();
	user_code = response_json["user_code"].string_value();
	verification_url = response_json["verification_url"].string_value();
	expires_in = response_json["expires_in"].int_value();
	interval = response_json["interval"].int_value();
}

void check_device_flow() {
	if (device_code.empty()) {
		return;
	}

	std::string post_data = "client_id=" + std::string(CLIENT_ID) + "&client_secret=" + std::string(CLIENT_SECRET) +
	                        "&code=" + device_code + "&grant_type=" + std::string(GRANT_TYPE_DEVICE);

	auto result = get_session().perform(
	    HttpRequest::POST(TOKEN_URL,
	                      {{"Host", "www.youtube.com"},
	                       {"User-Agent", "Mozilla/5.0 (Linux; Android 12; Quest 2) AppleWebKit/537.36"},
	                       {"Content-Type", "application/x-www-form-urlencoded"}},
	                      post_data));

	if (result.fail) {
		oauth_error_message = "Network error: " + result.error;
		oauth_state = OAuthState::ERROR;
		return;
	}

	if (result.status_code == 428) {
		return;
	} else if (result.status_code != 200 && result.status_code != 400) {
		oauth_error_message = "HTTP error: " + std::to_string(result.status_code);
		oauth_state = OAuthState::ERROR;
		return;
	}

	result.data.push_back('\0');
	rapidjson::Document json_root;
	std::string error;
	RJson response_json = RJson::parse(json_root, (char *)result.data.data(), error);

	if (!error.empty()) {
		oauth_error_message = "JSON error: " + error;
		oauth_state = OAuthState::ERROR;
		return;
	}

	if (response_json.has_key("error")) {
		std::string err = response_json["error"].string_value();
		if (err == "authorization_pending") {
			return;
		} else if (err == "slow_down") {
			interval += 5;
			return;
		} else if (err == "expired_token") {
			oauth_error_message = "Code expired";
			oauth_state = OAuthState::ERROR;
			device_code = "";
			user_code = "";
			verification_url = "";
			return;
		} else if (err == "access_denied") {
			oauth_error_message = "User denied access";
			oauth_state = OAuthState::ERROR;
			device_code = "";
			user_code = "";
			verification_url = "";
			return;
		} else {
			oauth_error_message = "OAuth error: " + err;
			oauth_state = OAuthState::ERROR;
			device_code = "";
			user_code = "";
			verification_url = "";
			return;
		}
	}

	access_token = response_json["access_token"].string_value();
	refresh_token = response_json["refresh_token"].string_value();

	device_code = "";
	user_code = "";
	verification_url = "";

	oauth_state = OAuthState::AUTHENTICATED;
	save_tokens();
	fetch_library_data();
}

void refresh_access_token() {
	if (refresh_token.empty()) {
		oauth_error_message = "No refresh token";
		oauth_state = OAuthState::ERROR;
		return;
	}

	std::string post_data = "client_id=" + std::string(CLIENT_ID) + "&client_secret=" + std::string(CLIENT_SECRET) +
	                        "&refresh_token=" + refresh_token + "&grant_type=refresh_token";

	auto result = get_session().perform(
	    HttpRequest::POST(TOKEN_URL,
	                      {{"Host", "www.youtube.com"},
	                       {"User-Agent", "Mozilla/5.0 (Linux; Android 12; Quest 2) AppleWebKit/537.36"},
	                       {"Content-Type", "application/x-www-form-urlencoded"}},
	                      post_data));

	if (result.fail) {
		oauth_error_message = "Network error: " + result.error;
		oauth_state = OAuthState::ERROR;
		return;
	}

	if (result.status_code != 200) {
		oauth_error_message = "HTTP error: " + std::to_string(result.status_code);
		oauth_state = OAuthState::ERROR;
		return;
	}

	result.data.push_back('\0');
	rapidjson::Document json_root;
	std::string error;
	RJson response_json = RJson::parse(json_root, (char *)result.data.data(), error);

	if (!error.empty()) {
		oauth_error_message = "JSON error: " + error;
		oauth_state = OAuthState::ERROR;
		return;
	}

	if (response_json.has_key("error")) {
		oauth_error_message = "OAuth error: " + response_json["error"].string_value();
		oauth_state = OAuthState::ERROR;
		return;
	}

	access_token = response_json["access_token"].string_value();
	if (response_json.has_key("refresh_token")) {
		refresh_token = response_json["refresh_token"].string_value();
	}

	oauth_state = OAuthState::AUTHENTICATED;
	save_tokens();
	fetch_library_data();
}

void revoke_tokens() {
	if (!access_token.empty()) {
		std::string post_data = "token=" + access_token;

		get_session().perform(
		    HttpRequest::POST(REVOKE_URL,
		                      {{"Host", "www.youtube.com"},
		                       {"User-Agent", "Mozilla/5.0 (Linux; Android 12; Quest 2) AppleWebKit/537.36"},
		                       {"Content-Type", "application/x-www-form-urlencoded"}},
		                      post_data));
	}

	access_token = "";
	refresh_token = "";
	device_code = "";
	user_code = "";
	verification_url = "";
	oauth_state = OAuthState::NOT_AUTHENTICATED;
	oauth_error_message = "";
	user_account_name = "";
	user_channel_id = "";
	user_photo_url = "";

	save_tokens();
}

bool is_authenticated() { return oauth_state == OAuthState::AUTHENTICATED && !access_token.empty(); }

std::string get_access_token() { return access_token; }

std::string get_user_account_name() { return user_account_name; }

std::string get_user_channel_id() { return user_channel_id; }

std::string get_user_photo_url() { return user_photo_url; }

static void encrypt_decrypt_data(std::vector<u8> &data) {
	size_t original_size = data.size();
	size_t padded_size = (original_size + 15) & ~15;
	data.resize(padded_size, 0);

	u8 iv[16];
	memset(iv, 0, sizeof(iv));

	PS_EncryptDecryptAes(padded_size, data.data(), data.data(), PS_ALGORITHM_CTR_ENC, PS_KEYSLOT_0D, iv);
}

void save_tokens() {
	std::string data = "<access_token>" + access_token + "</access_token>\n" + "<refresh_token>" + refresh_token +
	                   "</refresh_token>\n" + "<user_name>" + user_account_name + "</user_name>\n" + "<channel_id>" +
	                   user_channel_id + "</channel_id>\n" + "<photo_url>" + user_photo_url + "</photo_url>\n";

	std::vector<u8> encrypted_data(data.begin(), data.end());
	encrypt_decrypt_data(encrypted_data);

	std::string file_path = DEF_MAIN_DIR + "oauth_tokens";
	Path(file_path).write_file(encrypted_data.data(), encrypted_data.size());
}

void load_tokens() {
	std::string file_path = DEF_MAIN_DIR + "oauth_tokens";

	std::vector<u8> buffer(4096);
	u32 read_size;
	Result_with_string result = Path(file_path).read_file(buffer.data(), buffer.size(), read_size);

	if (result.code != 0 || read_size == 0) {
		return;
	}

	buffer.resize(read_size);

	encrypt_decrypt_data(buffer);

	buffer.push_back('\0');

	std::string data_str((char *)buffer.data());
	size_t access_start = data_str.find("<access_token>");
	size_t access_end = data_str.find("</access_token>");
	size_t refresh_start = data_str.find("<refresh_token>");
	size_t refresh_end = data_str.find("</refresh_token>");
	size_t name_start = data_str.find("<user_name>");
	size_t name_end = data_str.find("</user_name>");
	size_t channel_start = data_str.find("<channel_id>");
	size_t channel_end = data_str.find("</channel_id>");
	size_t photo_start = data_str.find("<photo_url>");
	size_t photo_end = data_str.find("</photo_url>");

	if (access_start != std::string::npos && access_end != std::string::npos) {
		access_start += 14;
		access_token = data_str.substr(access_start, access_end - access_start);
	}

	if (refresh_start != std::string::npos && refresh_end != std::string::npos) {
		refresh_start += 15;
		refresh_token = data_str.substr(refresh_start, refresh_end - refresh_start);
	}

	if (name_start != std::string::npos && name_end != std::string::npos) {
		name_start += 11;
		user_account_name = data_str.substr(name_start, name_end - name_start);
	}

	if (channel_start != std::string::npos && channel_end != std::string::npos) {
		channel_start += 12;
		user_channel_id = data_str.substr(channel_start, channel_end - channel_start);
	}

	if (photo_start != std::string::npos && photo_end != std::string::npos) {
		photo_start += 11;
		user_photo_url = data_str.substr(photo_start, photo_end - photo_start);
	}
}

void fetch_library_data() {
	if (!is_authenticated()) {
		return;
	}

	std::string post_data = R"({"context":{"client":{"hl":")" + youtube_parser::language_code + R"(","gl":")" +
	                        youtube_parser::country_code +
	                        R"(","clientName":"ANDROID_VR","clientVersion":"1.62.27"}},"browseId":"FElibrary"})";

	auto result = get_session().perform(HttpRequest::POST(
	    "https://www.youtube.com/youtubei/v1/browse",
	    {{"Content-Type", "application/json"}, {"Authorization", "Bearer " + access_token}}, post_data));

	if (result.fail || result.status_code != 200) {
		return;
	}

	result.data.push_back('\0');
	rapidjson::Document json_root;
	std::string error;
	RJson response_json = RJson::parse(json_root, (char *)result.data.data(), error);
	if (!error.empty()) {
		return;
	}

	auto header =
	    response_json["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]["tabRenderer"]["header"];
	if (!header.has_key("activeAccountHeaderRenderer")) {
		return;
	}

	auto account = header["activeAccountHeaderRenderer"];
	if (account.has_key("accountName")) {
		user_account_name = youtube_parser::get_text_from_object(account["accountName"]);
	}
	if (account.has_key("channelEndpoint")) {
		user_channel_id = account["channelEndpoint"]["browseEndpoint"]["browseId"].string_value();
	}
	if (account.has_key("accountPhoto")) {
		auto thumbnails = account["accountPhoto"]["thumbnails"].array_items();
		if (!thumbnails.empty()) {
			user_photo_url = thumbnails[0]["url"].string_value();
			size_t s_pos = user_photo_url.find("=s");
			if (s_pos != std::string::npos) {
				size_t size_end = s_pos + 2;
				while (size_end < user_photo_url.length() && isdigit(user_photo_url[size_end])) {
					size_end++;
				}
				user_photo_url = user_photo_url.substr(0, size_end) + "-c-k-c0x00ffffff-no-rj";
			}
		}
	}
}
} // namespace OAuth
