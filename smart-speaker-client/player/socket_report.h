#ifndef __SOCKET_REPORT_H__
#define __SOCKET_REPORT_H__

#include <pthread.h>
#include <json-c/json.h>

int socket_send_data(json_object *data);
int socket_report_start_thread(pthread_t *out_tid);

#endif
