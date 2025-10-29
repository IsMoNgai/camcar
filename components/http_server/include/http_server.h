#include <iostream>

#include "camera.hpp"

extern "C" {
    #include "esp_camera.h"
    #include "esp_http_server.h"
}

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req, Camera camera);