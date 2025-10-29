#include <iostream>

#include "camera.hpp"

extern "C" {
    #include "esp_camera.h"
    #include "esp_http_server.h"
    #include "esp_timer.h"
}

#define TAG "http_server"
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

esp_err_t jpg_stream_httpd_handler(httpd_req_t *req, Camera camera){
    FbPtr fb = NULL;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t * jpg_buf = NULL;

    char part_buf[64];
    static int64_t last_frame = 0;

    const uint8_t* data_ptr = nullptr;
    size_t data_len = 0;
    
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        FbPtr fb = camera.capture();

        JpegBufPtr converted_jpg = nullptr;

        if(fb->format != PIXFORMAT_JPEG){
            converted_jpg = camera.convert_to_jpg(fb, 80, &data_len);
            if(!converted_jpg){
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
                break;
            }
            data_ptr = converted_jpg.get();
        } else {
            data_len = fb->len;
            data_ptr = fb->buf;
        }

        if (data_ptr == NULL) { 
            res = ESP_FAIL; 
            break; 
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            int hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, data_len);
            if(hlen < 0 || hlen >= sizeof(part_buf)){
                ESP_LOGE(TAG, "Header truncated (%d bytes needed >= %zu buffer)",
                         hlen, sizeof(part_buf));
                res = ESP_FAIL;
            } else {
                res = httpd_resp_send_chunk(req, part_buf, (size_t)hlen);
            }
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)data_ptr, data_len);
        }
 
        if(res != ESP_OK){
            break;
        }

        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        float fps = frame_time > 0 ? 1000.0f / (float)frame_time : 0.0f;
        ESP_LOGI(TAG, "MJPG: %uKB %ums (%.1ffps)",
            (uint32_t)(jpg_buf_len/1024),
            (uint32_t)frame_time, fps);
    }

    last_frame = 0;
    return res;
}