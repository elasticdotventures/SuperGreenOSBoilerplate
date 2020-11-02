/*
 * Copyright (C) 2019  SuperGreenLab <towelie@supergreenlab.com>
 * Author: Constantin Clauzel <constantin.clauzel@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "httpd.h"
#include "httpd_kv_handlers.h"

#include <stdlib.h>

#include <esp_http_server.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../kv/kv.h"
#include "../log/log.h"

#define IS_URI_SEP(c) (c == '?' || c == '&' || c == '=')

esp_err_t download_get_handler(httpd_req_t *req);
esp_err_t upload_post_handler(httpd_req_t *req);
esp_err_t init_spiffs(void);

/* static size_t get_char_count(const char *uri) {
  size_t i = 0;
  for (; uri[i] && !IS_URI_SEP(uri[i]); ++i) {}
  return i;
} */

static const char *move_to_next_elem(const char *uri) {
  for (; *uri && !IS_URI_SEP(*uri); ++uri) {}
  if (*uri) ++uri;
  return uri;
}

static const char *move_to_key_value(const char *uri, const char *name) {
  while (*(uri = move_to_next_elem(uri))) {
    if (strncmp(uri, name, strlen(name)) == 0) {
      break;
    }
  }
  if (!*uri) return uri;
  return move_to_next_elem(uri);
}

inline int ishex(int x)
{
  return	(x >= '0' && x <= '9')	||
    (x >= 'a' && x <= 'f')	||
    (x >= 'A' && x <= 'F');
}

int url_decode(const char *s, char *dec)
{
  char *o;
  const char *end = s + strlen(s);
  int c;

  for (o = dec; s <= end; o++) {
    c = *s++;
    if (c == '+') c = ' ';
    else if (c == '%' && (	!ishex(*s++)	||
          !ishex(*s++)	||
          !sscanf(s - 2, "%2x", &c)))
      return -1;

    if (dec) *o = c;
  }

  return o - dec;
}

/*static int find_int_param(const char *uri, const char *name) {
  int res = 0;
  const char *uri_offset = move_to_key_value(uri, name);
  if (!*uri_offset) {
    return 0;
  }
  return res;
}*/

static void find_str_param(const char *uri, const char *name, char *out, size_t *len) {
  uri = move_to_key_value(uri, name);
  ESP_LOGI(SGO_LOG_NOSEND, "found: %s", uri);
  if (!*uri) {
    return;
  }
  int i = 0;
  for (i = 0; uri[i] && i < *len && !IS_URI_SEP(uri[i]); ++i) {
    out[i] = uri[i];
  }
  *len = i;
}

static esp_err_t geti_handler(httpd_req_t *req) {
  size_t len = 50;
  char name[50] = {0};
  find_str_param(req->uri, "k", name, &len);
  const kvi8_handler *hi8 = get_kvi8_handler(name);
  const kvui8_handler *hui8 = get_kvui8_handler(name);
  const kvi16_handler *hi16 = get_kvi16_handler(name);
  const kvui16_handler *hui16 = get_kvui16_handler(name);
  const kvi32_handler *hi32 = get_kvi32_handler(name);
  const kvui32_handler *hui32 = get_kvui32_handler(name);

  if (!hi8 && !hui8 && !hi16 && !hui16 && !hi32 && !hui32) {
    return httpd_resp_send_404(req);
  }

  int v = 0;
  if (hi8) {
    v = hi8->getter();
  } else if (hui8) {
    v = hui8->getter();
  } else if (hi16) {
    v = hi16->getter();
  } else if (hui16) {
    v = hui16->getter();
  } else if (hi32) {
    v = hi32->getter();
  } else if (hui32) {
    v = hui32->getter();
  }
  char ret[12] = {0};
  snprintf(ret, sizeof(ret) - 1, "%d", v);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, ret, strlen(ret));
  return ESP_OK;
}

static esp_err_t seti_handler(httpd_req_t *req) {
  size_t len = 50;
  char name[50] = {0};
  find_str_param(req->uri, "k", name, &len);
  const kvi8_handler *hi8 = get_kvi8_handler(name);
  bool is_i8 = hi8 && hi8->handler;
  const kvui8_handler *hui8 = get_kvui8_handler(name);
  bool is_ui8 = hui8 && hui8->handler;
  const kvi16_handler *hi16 = get_kvi16_handler(name);
  bool is_i16 = hi16 && hi16->handler;
  const kvui16_handler *hui16 = get_kvui16_handler(name);
  bool is_ui16 = hui16 && hui16->handler;
  const kvi32_handler *hi32 = get_kvi32_handler(name);
  bool is_i32 = hi32 && hi32->handler;
  const kvui32_handler *hui32 = get_kvui32_handler(name);
  bool is_ui32 = hui32 && hui32->handler;

  if (!is_i8 && !is_ui8 && !is_i16 && !is_ui16 && !is_i32 && !is_ui32) {
    return httpd_resp_send_404(req);
  }

  len = 50;
  char value[50] = {0};
  find_str_param(req->uri, "v", value, &len);
  int res = atoi(value);

  if (is_i8) {
    hi8->handler((int8_t)res);
  } else if (is_ui8) {
    hui8->handler((uint8_t)res);
  } else if (is_i16) {
    hi16->handler((int16_t)res);
  } else if (is_ui16) {
    hui16->handler((uint16_t)res);
  } else if (is_i32) {
    hi32->handler((int32_t)res);
  } else if (is_ui32) {
    hui32->handler((uint32_t)res);
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t getstr_handler(httpd_req_t *req) {
  char name[50] = {0};
  size_t len = 50;
  find_str_param(req->uri, "k", name, &len);

  const kvs_handler *h = get_kvs_handler(name);
  if (!h) {
    return httpd_resp_send_404(req);
  }

  char v[MAX_KVALUE_SIZE] = {0};
  h->getter(v, MAX_KVALUE_SIZE - 1);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, v, strlen(v));
  return ESP_OK;
}

static esp_err_t setstr_handler(httpd_req_t *req) {
  char name[50] = {0};
  size_t len = 50;
  find_str_param(req->uri, "k", name, &len);

  const kvs_handler *h = get_kvs_handler(name);
  if (!h || !h->handler) {
    return httpd_resp_send_404(req);
  }

  len = MAX_KVALUE_SIZE;
  char raw[MAX_KVALUE_SIZE] = {0};
  char value[MAX_KVALUE_SIZE] = {0};
  find_str_param(req->uri, "v", raw, &len);
  url_decode(raw, value);

  h->handler(value);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t option_handler(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type,Access-Control-Allow-Origin");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t get_ip_handler(httpd_req_t *req) {
  int socket = httpd_req_to_sockfd(req);

  struct sockaddr_in6 destAddr;
  unsigned socklen=sizeof(destAddr);

  if(getpeername(socket, (struct sockaddr *)&destAddr, &socklen)<0) {
    return httpd_resp_send_500(req);
  }
  char ip[16] = {0};
  uint8_t *iphex = (uint8_t *)&(destAddr.sin6_addr.un.u32_addr[3]);
  snprintf(ip, sizeof(ip)-1, "%d.%d.%d.%d", iphex[0], iphex[1], iphex[2], iphex[3]);
  httpd_resp_send(req, ip, strlen(ip));
  return ESP_OK;
}

httpd_uri_t uri_geti = {
  .uri      = "/i",
  .method   = HTTP_GET,
  .handler  = geti_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_seti = {
  .uri      = "/i",
  .method   = HTTP_POST,
  .handler  = seti_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_getstr = {
  .uri      = "/s",
  .method   = HTTP_GET,
  .handler  = getstr_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_setstr = {
  .uri      = "/s",
  .method   = HTTP_POST,
  .handler  = setstr_handler,
  .user_ctx = NULL
};

httpd_uri_t uri_get_ip = {
  .uri      = "/myip",
  .method   = HTTP_GET,
  .handler  = get_ip_handler,
  .user_ctx = NULL
};

httpd_uri_t file_download = {
	.uri       = "/fs/?*",
	.method    = HTTP_GET,
	.handler   = download_get_handler,
	.user_ctx  = NULL
};

httpd_uri_t file_upload = {
	.uri       = "/fs/*",
	.method    = HTTP_POST,
	.handler   = upload_post_handler,
	.user_ctx  = NULL
};

httpd_uri_t uri_option = {
  .uri      = "/*",
  .method   = HTTP_OPTIONS,
  .handler  = option_handler,
  .user_ctx = NULL
};

static httpd_handle_t server = NULL;

static httpd_handle_t start_webserver(void) {

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &uri_geti);
    httpd_register_uri_handler(server, &uri_seti);
    httpd_register_uri_handler(server, &uri_getstr);
    httpd_register_uri_handler(server, &uri_setstr);
    httpd_register_uri_handler(server, &uri_get_ip);
    httpd_register_uri_handler(server, &file_download);
		httpd_register_uri_handler(server, &file_upload);
    httpd_register_uri_handler(server, &uri_option);
  }
  return server;
}

void init_httpd() {
  ESP_LOGI(SGO_LOG_EVENT, "@HTTPS Intializing HTTPD task");

  init_spiffs();
  start_webserver();
}
