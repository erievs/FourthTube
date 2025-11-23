#pragma once
#include <string>

namespace OAuth {
extern const char *DEVICE_CODE_URL;
extern const char *TOKEN_URL;
extern const char *REVOKE_URL;

enum class OAuthState { NOT_AUTHENTICATED, AUTHENTICATING, AUTHENTICATED, ERROR };

extern OAuthState oauth_state;
extern std::string oauth_error_message;
extern std::string device_code;
extern std::string user_code;
extern std::string verification_url;
extern int expires_in;
extern int interval;
extern std::string access_token;
extern std::string refresh_token;

extern std::string user_account_name;
extern std::string user_channel_id;
extern std::string user_photo_url;

void init();
void exit();
void start_device_flow();
void check_device_flow();
void refresh_access_token();
void revoke_tokens();
bool is_authenticated();
std::string get_access_token();
std::string get_user_account_name();
std::string get_user_channel_id();
std::string get_user_photo_url();

void save_tokens();
void load_tokens();

void fetch_library_data();
} // namespace OAuth
