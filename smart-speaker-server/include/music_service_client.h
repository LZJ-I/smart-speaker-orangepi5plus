#ifndef SMART_SPEAKER_MUSIC_SERVICE_CLIENT_H
#define SMART_SPEAKER_MUSIC_SERVICE_CLIENT_H

#include <json/json.h>
#include <string>

bool music_service_post_json(const std::string &path, const Json::Value &request, Json::Value *response,
                             std::string *error_message);
bool music_service_ensure_ready(std::string *error_message);
bool music_service_restart_local(std::string *error_message);
void music_service_shutdown_spawned_process(void);

#endif
