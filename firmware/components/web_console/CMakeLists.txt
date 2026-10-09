idf_component_register(SRCS "web_console.c" INCLUDE_DIRS "include"
    REQUIRES control test_session esp_hw_support esp_wifi esp_event esp_netif esp_http_server nvs_flash esp_timer
    EMBED_FILES "index.html")
