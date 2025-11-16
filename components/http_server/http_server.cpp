#include "camera.hpp"   // Provides Camera, FbPtr, JpegBufPtr, PIXFORMAT_JPEG

extern "C" {
    #include <string.h>
    #include <unistd.h>

    #include "esp_check.h"
    #include "esp_event.h"
    #include "esp_log.h"
    #include "esp_netif.h"
    #include "esp_timer.h"
    #include "nvs_flash.h"

    #include "esp_http_server.h"

    #if !CONFIG_IDF_TARGET_LINUX
    #include "esp_wifi.h"
    #include "esp_system.h"
    #include "esp_eth.h"
    #endif
}

static const char* TAG = "http_server";

// ---------- Multipart MJPEG constants ----------
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

// Bundle server + camera so event handlers can access both.
struct WebServerCtx {
    httpd_handle_t server = nullptr;
    Camera* camera        = nullptr;
};

// ---------- /camera handler ----------
static esp_err_t jpg_stream_httpd_handler(httpd_req_t* req) {
    auto* camera = static_cast<Camera*>(req->user_ctx);
    if (!camera) {
        ESP_LOGE(TAG, "Camera context missing");
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    char part_buf[64];
    static int64_t last_frame = 0;
    if (!last_frame) last_frame = esp_timer_get_time();

    while (true) {
        FbPtr fb = camera->capture();
        if (!fb) {
            ESP_LOGE(TAG, "capture() returned null");
            res = ESP_FAIL;
            break;
        }

        const uint8_t* data_ptr = nullptr;
        size_t data_len = 0;

        JpegBufPtr converted_jpg = nullptr;
        if (fb->format != PIXFORMAT_JPEG) {
            converted_jpg = camera->convert_to_jpeg(fb, 80, &data_len);
            if (!converted_jpg) {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
                break;
            }
            data_ptr = converted_jpg.get();
        } else {
            data_len = fb->len;
            data_ptr = fb->buf;
        }

        if (!data_ptr || data_len == 0) {
            ESP_LOGE(TAG, "No JPEG data to send");
            res = ESP_FAIL;
            break;
        }

        // Boundary
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res != ESP_OK) break;

        // Part header with length
        int hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, data_len);
        if (hlen <= 0 || hlen >= (int)sizeof(part_buf)) {
            ESP_LOGE(TAG, "Header truncated or error (hlen=%d)", hlen);
            res = ESP_FAIL;
            break;
        }
        res = httpd_resp_send_chunk(req, part_buf, (size_t)hlen);
        if (res != ESP_OK) break;

        // JPEG payload
        res = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(data_ptr), data_len);
        if (res != ESP_OK) break;

        // FPS log
        int64_t now = esp_timer_get_time();
        int64_t ft_ms = (now - last_frame) / 1000;
        last_frame = now;
        float fps = ft_ms > 0 ? 1000.0f / (float)ft_ms : 0.0f;
        ESP_LOGI(TAG, "MJPG: %uKB %llums (%.1ffps)",
                 (uint32_t)(data_len / 1024), (unsigned long long)ft_ms, fps);
    }

    last_frame = 0;
    return res;
}

// Base URI template; we'll fill user_ctx at runtime.
static const httpd_uri_t CAMERA_URI_TEMPLATE = {
    .uri      = "/camera",
    .method   = HTTP_GET,
    .handler  = jpg_stream_httpd_handler,
    .user_ctx = nullptr
};

// ---------- Server lifecycle ----------
static esp_err_t stop_webserver(WebServerCtx* ctx) {
    if (ctx && ctx->server) {
        ESP_LOGI(TAG, "Stopping server");
        esp_err_t r = httpd_stop(ctx->server);
        ctx->server = nullptr;
        return r;
    }
    return ESP_OK;
}

static httpd_handle_t start_webserver(WebServerCtx* ctx) {
    if (!ctx || !ctx->camera) {
        ESP_LOGE(TAG, "start_webserver: invalid ctx or camera");
        return nullptr;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_LINUX
    // Non-privileged port for Linux target.
    config.server_port = 8001;
#endif
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);

    if (httpd_start(&ctx->server, &config) == ESP_OK) {
        httpd_uri_t cam = CAMERA_URI_TEMPLATE;
        cam.user_ctx = ctx->camera; // IMPORTANT: Camera* (not &camera)
        esp_err_t reg = httpd_register_uri_handler(ctx->server, &cam);
        if (reg != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register /camera handler: %s", esp_err_to_name(reg));
            stop_webserver(ctx);
            return nullptr;
        }
        ESP_LOGI(TAG, "Registered /camera handler");
        return ctx->server;
    }

    ESP_LOGE(TAG, "Error starting server!");
    return nullptr;
}

// ---------- Netif event handlers ----------
static void disconnect_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data) {
    auto* ctx = static_cast<WebServerCtx*>(arg);
    (void)event_base; (void)event_id; (void)event_data;

    if (ctx && ctx->server) {
        ESP_LOGI(TAG, "Network down -> stop server");
        if (stop_webserver(ctx) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop server on disconnect");
        }
    }
}

static void connect_handler(void* arg,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            void* event_data) {
    auto* ctx = static_cast<WebServerCtx*>(arg);
    (void)event_base; (void)event_id; (void)event_data;

    if (ctx && ctx->server == nullptr) {
        ESP_LOGI(TAG, "Network up -> start server");
        start_webserver(ctx);
    }
}

// ---------- Public entry point ----------
void setup_webserver(Camera* camera) {
    static WebServerCtx ctx;   // Persistent for handler callbacks
    ctx.camera = camera;

    // Init NVS, netif, and event loop
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Bring up Wi-Fi/Ethernet via helper (replace with your own if needed)
    // ESP_ERROR_CHECK(example_connect());

#if !CONFIG_IDF_TARGET_LINUX
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &connect_handler, &ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               WIFI_EVENT_STA_DISCONNECTED,
                                               &disconnect_handler, &ctx));
#endif
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_ETH_GOT_IP,
                                               &connect_handler, &ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT,
                                               ETHERNET_EVENT_DISCONNECTED,
                                               &disconnect_handler, &ctx));
#endif
#endif

    // Start once initially (connect handler will also start on future reconnects)
    start_webserver(&ctx);

    // Keep the task alive if you want this function to block like the examples.
    // If you'd rather return immediately, just remove the loop below.
    while (true) {
        sleep(5); // or vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
