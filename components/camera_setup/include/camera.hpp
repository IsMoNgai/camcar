/*
Oct 28:
This Camera interface provides a safe way to interact with the camera module safely with RAII
Everything is inline so can be put in hpp instead of cpp.
*/

#pragma once
extern "C" {
    #include "esp_camera.h"
}
#include <memory>
#include <vector>

// WROVER-KIT (OV2640) pin map
#define CAM_PIN_PWDN    GPIO_NUM_NC   // not used
#define CAM_PIN_RESET   GPIO_NUM_NC   // not used (software reset)
#define CAM_PIN_XCLK    GPIO_NUM_21
#define CAM_PIN_SIOD    GPIO_NUM_26
#define CAM_PIN_SIOC    GPIO_NUM_27
#define CAM_PIN_D7      GPIO_NUM_35
#define CAM_PIN_D6      GPIO_NUM_34
#define CAM_PIN_D5      GPIO_NUM_39
#define CAM_PIN_D4      GPIO_NUM_36
#define CAM_PIN_D3      GPIO_NUM_19
#define CAM_PIN_D2      GPIO_NUM_18
#define CAM_PIN_D1      GPIO_NUM_5
#define CAM_PIN_D0      GPIO_NUM_4
#define CAM_PIN_VSYNC   GPIO_NUM_25
#define CAM_PIN_HREF    GPIO_NUM_23
#define CAM_PIN_PCLK    GPIO_NUM_22

// custom deleter for smart pointer, CJ talks about this before in one of the mock interview
// unique_ptr will call operator() when it needs to destroy the raw pointer
// the camera_fb_t* ptr is special and need to be destroyed with the esp_camera_fb_return
struct FbDeleter{
    void operator()(camera_fb_t* p) const { if (p) esp_camera_fb_return(p); }
};
// type alias for the unique_ptr
using FbPtr = std::unique_ptr<camera_fb_t, FbDeleter>;

class Camera {
public:
    // NOTE: explicit keyword is used to avoid cfg being auto converted to camera_config_t causing error 
    Camera() : inited_(false) {
        camera_config_t cfg = defaultConfig();
        inited_ = (esp_camera_init(&cfg) == ESP_OK);
    }
    ~Camera() {
        if (inited_) esp_camera_deinit();
    }

    bool ok() const { return inited_; }

    FbPtr capture() const {
        /*
        Gets a new frame buffer from the camera driver, returns the raw pointer wrapped with the smart pointer.
        This is safe because FbPtr is automatically destroyed when out of frame (frame is released).
        */
        if (!inited_) return FbPtr(nullptr);
        return FbPtr(esp_camera_fb_get());
    }

    bool captureCopy(std::vector<uint8_t>& out) const {
        /*
            fb->buf is a pointer to the raw image bytes (start of the data)
            fb->len is the length of the image data in bytes
            assign() copies the bytes from the source range into the out vector
        */
        if (!inited_) return false;
        if (auto fb = FbPtr(esp_camera_fb_get())) {
            out.assign(fb->buf, fb->buf + fb->len);
            return true;
        }
        return false;
    }

    // Disallow copy, allow move
    // Disable copy constructor and copy assignment operator
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    // Enable move constructor and move assignment operator
    Camera(Camera&&) = default;
    Camera& operator=(Camera&&) = default;

private:
    static camera_config_t defaultConfig() {
        camera_config_t camera_config = {
            .pin_pwdn     = CAM_PIN_PWDN,
            .pin_reset    = CAM_PIN_RESET,
            .pin_xclk     = CAM_PIN_XCLK,
            .pin_sscb_sda = CAM_PIN_SIOD,
            .pin_sscb_scl = CAM_PIN_SIOC,
        
            .pin_d7 = CAM_PIN_D7,
            .pin_d6 = CAM_PIN_D6,
            .pin_d5 = CAM_PIN_D5,
            .pin_d4 = CAM_PIN_D4,
            .pin_d3 = CAM_PIN_D3,
            .pin_d2 = CAM_PIN_D2,
            .pin_d1 = CAM_PIN_D1,
            .pin_d0 = CAM_PIN_D0,
        
            .pin_vsync = CAM_PIN_VSYNC,        // marks frame start/end
            .pin_href  = CAM_PIN_HREF,         // marks valid line regions
            .pin_pclk  = CAM_PIN_PCLK,
        
            .xclk_freq_hz = 20000000,          // 20 MHz XCLK   
            .ledc_timer   = LEDC_TIMER_0,
            .ledc_channel = LEDC_CHANNEL_0,
        
            .pixel_format = PIXFORMAT_JPEG,    // (use JPEG for best perf on ESP32)
            .frame_size   = FRAMESIZE_UXGA,    // change later as needed
            .jpeg_quality = 12,                // lower is higher quality
            .fb_count     = 1,
            .fb_location  = CAMERA_FB_IN_PSRAM,
            .grab_mode    = CAMERA_GRAB_WHEN_EMPTY
        };
        return camera_config;
    }

    bool inited_;
};