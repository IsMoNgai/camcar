#include <iostream>

//#####################################################################
#include "camera.hpp"


//#####################################################################

extern "C" {
    #include "esp_log.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "esp_psram.h"
}

static const char* TAG = "camcar:MAIN";

extern "C" void app_main(void)
{
    // check if psram is avaliable
    ESP_LOGI("psram", "Free heap: %d | Free PSRAM: %d",
        esp_get_free_heap_size(), esp_psram_get_size());

    Camera camera;
    if (!camera.ok()) {
        ESP_LOGI(TAG, "Failed to initialize camera");
        return;
    }

    FbPtr fb = camera.capture();
    
    // (Unreachable)
    std::cout << "Done!" << std::endl;
}
