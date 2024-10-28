#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "math.h"
#include "mbedtls/aes.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "st7789.h"
#include "fontx.h"

// Визначаємо пристрій сервер чи клієнт
#define IS_SERVER
//#define IS_CLIENT

#define I2S_NUM_TX 0
#define I2S_NUM_RX 1

#define I2S_WS_TX  12
#define I2S_SCK_TX 13
#define I2S_DATA_OUT_TX  15
#define I2S_WS_RX 25
#define I2S_SCL_RX 26
#define I2S_SD_RX 27

#define BUTTON_GPIO GPIO_NUM_35
#define ENCRYPTION_BUTTON_GPIO GPIO_NUM_0

// Налаштування Wi-Fi
#define EXAMPLE_ESP_WIFI_SSID "esp32_ap"
#define EXAMPLE_ESP_WIFI_PASS "password"
#define PORT 1234
#define CLIENT_IP_ADDR "192.168.4.2"
#define SERVER_IP_ADDR "192.168.4.1"

#define UDP_BUFFER_SIZE 1024
#define SAMPLE_RATE 44100 // Аудіо стандарт, частота дискретизації

#define AES_KEY_SIZE 16

uint8_t aes_key[AES_KEY_SIZE] = {
    0x3d, 0xf2, 0x67, 0xf0, 0x34, 0xa9, 0xbc, 0x0b, 
    0x8e, 0xac, 0xe5, 0x8f, 0x12, 0x3c, 0x56, 0x78
};

static i2s_chan_handle_t    rx_chan;
static i2s_chan_handle_t    tx_chan;

static const char *TAG = "Walkie_Talkie"; 
#ifdef IS_CLIENT
static bool got_ip = false;
#endif
volatile bool transmit_data = false;
volatile bool receiving_data = false;
volatile bool encryption_enabled = true;

// Обробник подій Wi-Fi
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{   
#ifdef IS_SERVER
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        ESP_LOGI(TAG, "Station connected");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        ESP_LOGI(TAG, "Station disconnected");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) 
    {
        ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        ESP_LOGI(TAG, "Assigned IP to station: " IPSTR, IP2STR(&event->ip));

        esp_netif_ip_info_t ip_info;
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif == NULL) 
        {
            ESP_LOGE(TAG, "Failed to get AP interface handle");
        } 
        else 
        {
            if (esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) 
            {
                ESP_LOGE(TAG, "Failed to get IP info for AP interface");
            } 
            else 
            {
                ESP_LOGI(TAG, "Current AP IP address: " IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }
#else
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
        got_ip = false;
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        got_ip = true;
    }
#endif
}

void wifi_init(void)
{
    // Ініціалізація стеку WiFi
    ESP_ERROR_CHECK(esp_netif_init());

    // Ініціалізація бібліотеки WiFi з налаштуваннями за замовчуванням
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Створення обробника подій WiFi
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
#ifdef IS_SERVER
    // Створення мережевого інтерфейсу для точки доступу (AP)
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    // Реєстрація обробника подій IP для AP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = 
    {
        .ap = 
        {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK      // Режим автентифікації
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) 
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Встановлення режиму WiFi як AP (точка доступу)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // Встановлення конфігурації WiFi
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // Запуск інтерфейсу WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Налаштування IP-адреси для інтерфейсу AP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    // Зупинка DHCP сервера для AP  
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));

    // Встановлення IP-інформації для інтерфейсу AP
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));

    // Запуск DHCP сервера для AP
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
#else
    // Створення нового мережевого інтерфейсу STA (клієнт)
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    // Реєстрація обробника подій IP для STA
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = 
    {
        .sta = 
        {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };

    // Встановлення режиму WiFi як STA (клієнт)    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Встановлення конфігурації WiFi
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    // Запуск інтерфейсу WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Запуск DHCP клієнта
    ESP_ERROR_CHECK(esp_netif_dhcpc_start(sta_netif));

    ESP_LOGI(TAG, "wifi_init_sta finished.");
#endif
}

void encryption_button_task(void* arg)
{
    esp_rom_gpio_pad_select_gpio(ENCRYPTION_BUTTON_GPIO);

    // Встановлення напрямку GPIO як вхід
    gpio_set_direction(ENCRYPTION_BUTTON_GPIO, GPIO_MODE_INPUT);

    // Встановлення режиму підтягування до живлення
    gpio_set_pull_mode(ENCRYPTION_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    int last_state = 1;
    while(1) 
    {
        int state = gpio_get_level(ENCRYPTION_BUTTON_GPIO);
        if(state != last_state)     // Перевірка на зміну стану кнопки
        {
            last_state = state;
            if(state == 0) 
            {
                encryption_enabled = !encryption_enabled;
                ESP_LOGI(TAG, "Encryption %s", encryption_enabled ? "enabled" : "disabled");
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);     // Затримка для запобігання багаторазового натискання кнопки
    }
}

void button_task(void* arg)
{
    esp_rom_gpio_pad_select_gpio(BUTTON_GPIO);

    // Встановлення напрямку GPIO як вхід
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);

    // Встановлення режиму підтягування до живлення
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    int last_state = 1;
    while(1) 
    {
        int state = gpio_get_level(BUTTON_GPIO);
        if(state != last_state)     // Перевірка на зміну стану кнопки
        {
            last_state = state;
            if(state == 0) 
            {
                ESP_LOGI(TAG, "Button Pressed");
                 transmit_data = true; 
            } 
            else 
            {
                ESP_LOGI(TAG, "Button Released");
               transmit_data = false; 
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);    // Затримка для запобігання багаторазового натискання кнопки
    }
}

// Функція підсилення сигналу
void amplify_signal(int16_t *buffer, size_t length, float gain) {
    for (size_t i = 0; i < length; i++) {
        buffer[i] = (int16_t) fminf(fmaxf(buffer[i] * gain, -32768.0f), 32767.0f);
    }
}

// Функція фільтрації сигналу
void high_pass_filter(int16_t *buffer, size_t length, float alpha) {
    int16_t prev = buffer[0];
    for (size_t i = 1; i < length; i++) {
        int16_t current = buffer[i];
        buffer[i] = (int16_t) (alpha * (buffer[i] - prev) + prev);
        prev = current;
    }
}

void my_aes_encrypt(uint8_t *input, uint8_t *output, size_t length, uint8_t *key) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);

    // Шифрування даних блочним методом ECB
    for (size_t i = 0; i < length; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
    }

    mbedtls_aes_free(&aes);
}

void my_aes_decrypt(uint8_t *input, uint8_t *output, size_t length, uint8_t *key) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 128);

    // Дешифрування даних блочним методом ECB
    for (size_t i = 0; i < length; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
    }

    mbedtls_aes_free(&aes);
}

void udp_send_task(void *pvParameters)
{
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    // Виділення пам'яті для буфера даних та шифрованого буфера
    uint8_t *read_buf = (uint8_t *)calloc(1, UDP_BUFFER_SIZE);
    uint8_t *encrypted_buf = (uint8_t *)calloc(1, UDP_BUFFER_SIZE);
    assert(read_buf); // Перевірка на успішне виділення пам'яті
    assert(encrypted_buf);
    size_t read_bytes = 0;

#ifdef IS_SERVER
    dest_addr.sin_addr.s_addr = inet_addr(CLIENT_IP_ADDR);
#else
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP_ADDR);
#endif

    while (1) {
        if (transmit_data) {
            if (i2s_channel_read(rx_chan, read_buf, UDP_BUFFER_SIZE, &read_bytes, 1000) == ESP_OK) {
                // Підсилюємо сигнал
                amplify_signal((int16_t *)read_buf, read_bytes / 2, 10.0f); // Підсилюємо в 10 разів

                // Фільтруємо сигнал
                //high_pass_filter((int16_t *)read_buf, read_bytes / 2, 0.9f);

                // Шифрування даних, якщо увімкнено шифрування
               if (encryption_enabled) {
                    my_aes_encrypt(read_buf, encrypted_buf, read_bytes, aes_key);
                } else {
                    memcpy(encrypted_buf, read_buf, read_bytes);
                }

                // Відправка даних по UDP
                int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
                int err = sendto(sock, encrypted_buf, read_bytes, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                close(sock);
            }
        } 
        vTaskDelay(10 / portTICK_PERIOD_MS);        // Для запобігання watch dog
    }

    // Звільнення виділеної пам'яті
    free(read_buf);
    free(encrypted_buf);
}

void udp_receive_task(void *pvParameters)
{
    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;
    struct sockaddr_in source_addr;
    source_addr.sin_family = AF_INET;
    source_addr.sin_port = htons(PORT);
    source_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Створення UDP сокету
    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    bind(sock, (struct sockaddr *)&source_addr, sizeof(source_addr));

    // Налаштування таймауту для recvfrom
    struct timeval timeout;
    timeout.tv_sec = 0; // 0 секунд
    timeout.tv_usec = 100000; // 100 мілісекунд
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Виділення пам'яті для буфера даних та дешифрованого буфера
    uint8_t *write_buf = (uint8_t *)calloc(1, UDP_BUFFER_SIZE);
    uint8_t *decrypted_buf = (uint8_t *)calloc(1, UDP_BUFFER_SIZE);
    assert(write_buf); // Перевірка на успішне виділення пам'яті
    assert(decrypted_buf);
    size_t write_bytes = 0;     

    TickType_t last_receive_time = xTaskGetTickCount(); // Час останнього отримання даних
    const TickType_t timeout_ticks = pdMS_TO_TICKS(100); // 100 мілісекунд таймаут

    while (1) {
        ESP_LOGI(TAG, "Receiving");

        struct sockaddr_in dest_addr;
        socklen_t socklen = sizeof(dest_addr);

        // Отримання даних по UDP   
        int len = recvfrom(sock, write_buf, UDP_BUFFER_SIZE, 0, (struct sockaddr *)&dest_addr, &socklen);

        if (len < 0) {
            receiving_data = false;
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Таймаут recvfrom, продовжуємо цикл
                if (xTaskGetTickCount() - last_receive_time > timeout_ticks) {
                    memset(write_buf, 0, UDP_BUFFER_SIZE); // Очищення буфера звуку при таймауті
                    for (int i = 0; i < 5; i++) {
                        if (i2s_channel_write(tx_chan, write_buf, UDP_BUFFER_SIZE, &write_bytes, 1000) != ESP_OK) {
                            ESP_LOGE(TAG, "i2s write failed");
                            break; // Виходимо з циклу, якщо запис не вдається
                        }
                    }
                    last_receive_time = xTaskGetTickCount(); // Оновлюємо час останнього очищення буфера
                }
            } else {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            }
        } else {
            last_receive_time = xTaskGetTickCount(); // Оновлюємо час останнього отримання даних

            // Дешифрування даних, якщо увімкнено шифрування
            if (encryption_enabled) {
                my_aes_decrypt(write_buf, decrypted_buf, len, aes_key);
            } else {
                memcpy(decrypted_buf, write_buf, len);
            }

            receiving_data = true;

            // Запис даних у I2S канал
            if (i2s_channel_write(tx_chan, decrypted_buf, len, &write_bytes, 1000) != ESP_OK) {
                ESP_LOGE(TAG, "i2s write failed");
            }
        }
    }

    // Звільнення виділеної пам'яті
    free(write_buf);
    free(decrypted_buf);
}

// Конфігурація I2S каналу для приймання (RX) даних
void microphone_init(void)  
{
    i2s_chan_config_t rx_chan_cfg  = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_RX, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg , NULL, &rx_chan));
    
    i2s_std_config_t rx_std_cfg  = 
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
        {
            .mclk = I2S_GPIO_UNUSED, 
            .bclk = I2S_SCL_RX,
            .ws = I2S_WS_RX,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD_RX,
            .invert_flags =
            {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg ));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

// Конфігурація I2S каналу для передавання (TX) даних
void speaker_init(void)
{
    i2s_chan_config_t tx_chan_cfg  = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_TX, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));
    
    i2s_std_config_t tx_std_cfg  = 
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg =
        {
            .mclk = I2S_GPIO_UNUSED, 
            .bclk = I2S_SCK_TX,
            .ws = I2S_WS_TX,
            .dout = I2S_DATA_OUT_TX,
            .din = -1
        }
    };


    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg ));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

// Функція для відображення тексту на дисплеї
void DrawText(TFT_t * dev, FontxFile *fx, int width, int height, const char* text1, const char* text2, const char* text3, const char* text4, uint16_t bgColor, uint16_t textColor) {
    uint16_t xpos1, ypos1, xpos2, ypos2, xpos3, ypos3, xpos4, ypos4;
    uint8_t ascii1[24], ascii2[24], ascii3[24], ascii4[24];

    // Встановлюємо колір фону 
    lcdFillScreen(dev, bgColor);

    // Встановлюємо тексти для відображення
    strcpy((char *)ascii1, text1);
    strcpy((char *)ascii2, text2);
    strcpy((char *)ascii3, text3);
    strcpy((char *)ascii4, text4);

    // Розраховуємо позицію для центрування першого тексту
    uint8_t buffer[FontxGlyphBufSize];
    uint8_t fontWidth;
    uint8_t fontHeight;
    GetFontx(fx, 0, buffer, &fontWidth, &fontHeight);
    xpos1 = (width - (strlen((char *)ascii1) * fontWidth)) / 2;
    ypos1 = (height / 2) - fontHeight * 3;

    // Розраховуємо позицію для центрування другого тексту
    xpos2 = (width - (strlen((char *)ascii2) * fontWidth)) / 2;
    ypos2 = (height / 2) - fontHeight;

    // Розраховуємо позицію для центрування третього тексту
    xpos3 = (width - (strlen((char *)ascii3) * fontWidth)) / 2;
    ypos3 = (height / 2) + fontHeight;

    // Розраховуємо позицію для центрування четвертого тексту
    xpos4 = (width - (strlen((char *)ascii4) * fontWidth)) / 2;
    ypos4 = (height / 2) + fontHeight * 3;

    // Встановлюємо напрямок шрифту та колір
    lcdSetFontDirection(dev, DIRECTION0);
    lcdDrawString(dev, fx, xpos1, ypos1, ascii1, textColor);
    lcdDrawString(dev, fx, xpos2, ypos2, ascii2, textColor);
    if (strlen((char *)ascii3) > 0) {
        lcdDrawString(dev, fx, xpos3, ypos3, ascii3, textColor);
    }
    if (strlen((char *)ascii4) > 0) {
        lcdDrawString(dev, fx, xpos4, ypos4, ascii4, textColor);
    }
    lcdDrawFinish(dev);
}

// Функція для управління дисплеєм ST7789
void ST7789(void *pvParameters)
{
    // Ініціалізація файлу шрифту
    FontxFile fx16G[2];
    InitFontx(fx16G,"/spiffs/ILGH16XB.FNT",""); // 8x16Dot Gothic

    TFT_t dev;

    // Ініціалізація дисплея
    spi_master_init(&dev, CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO, CONFIG_CS_GPIO, CONFIG_DC_GPIO, CONFIG_RESET_GPIO, CONFIG_BL_GPIO);
    lcdInit(&dev, CONFIG_WIDTH, CONFIG_HEIGHT, CONFIG_OFFSETX, CONFIG_OFFSETY);

    esp_rom_gpio_pad_select_gpio(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    char encryption_status[24];
    
    bool last_transmit_state = false;
    bool last_receive_state = false;
    bool last_encryption_state = encryption_enabled;

    // Стартове оновлення дисплея
    snprintf(encryption_status, sizeof(encryption_status), "Encryption: %s", encryption_enabled ? "ON" : "OFF");
#ifdef IS_SERVER
    DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "SERVER", "", encryption_status, BLUE, WHITE);
#else
    DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "CLIENT", "", encryption_status, BLUE, WHITE);
#endif

    while (1) {
        bool update_display = false;

        // Перевірка на зміну станів передавання/прийому/шифрування
        if (transmit_data != last_transmit_state) {
            last_transmit_state = transmit_data;
            update_display = true;
        }

        if (receiving_data != last_receive_state) {
            last_receive_state = receiving_data;
            update_display = true;
        }

        if (encryption_enabled != last_encryption_state) {
            last_encryption_state = encryption_enabled;
            update_display = true;
        }

        snprintf(encryption_status, sizeof(encryption_status), "Encryption: %s", encryption_enabled ? "ON" : "OFF");

        // Оновлення дисплея, якщо змінився якийсь стан
        if (update_display) {
#ifdef IS_SERVER
            if (transmit_data && receiving_data) { // Повний дуплекс
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "SERVER", "Full-Duplex", encryption_status, PURPLE, WHITE);
            } else if (transmit_data) { // Передача даних
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "SERVER", "Transmitting", encryption_status, RED, WHITE);
            } else if (receiving_data) { // Прийом даних
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "SERVER", "Receiving", encryption_status, GREEN, WHITE);
            } else { // Бездіяльність
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "SERVER", "", encryption_status, BLUE, WHITE);
            }
#else
            if (transmit_data && receiving_data) { // Повний дуплекс    
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "CLIENT", "Full-Duplex", encryption_status, PURPLE, WHITE);
            } else if (transmit_data) { // Передача даних
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "CLIENT", "Transmitting", encryption_status, RED, WHITE);
            } else if (receiving_data) { // Прийом даних
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "CLIENT", "Receiving", encryption_status, GREEN, WHITE);
            } else { // Бездіяльність
                DrawText(&dev, fx16G, CONFIG_WIDTH, CONFIG_HEIGHT, "Walkie-Talkie", "CLIENT", "", encryption_status, BLUE, WHITE);
            }
#endif
        }

        // Перевірка станів кожні 200 мс
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    // Ініціалізація NVS (Non-Volatile Storage)
    ESP_ERROR_CHECK(nvs_flash_init());

    // Ініціалізація мережевого інтерфейсу
    ESP_ERROR_CHECK(esp_netif_init());

    // Створення циклу обробки подій за замовчуванням
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init();
    microphone_init();
    speaker_init();
    
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 12,
        .format_if_mount_failed = true
    };

    esp_err_t spiffs = esp_vfs_spiffs_register(&conf);

    // Перевірка, чи успішно була ініціалізована файлова системи SPIFFS
    if (spiffs != ESP_OK) {
        if (spiffs == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (spiffs == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(spiffs));
        }
        return;
    }

    // Затримка для стабілізації системи перед запуском задач
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    xTaskCreate(udp_send_task, "udp_send_task", 4096, NULL, 2, NULL); 
    xTaskCreate(udp_receive_task, "udp_receive_task", 4096, NULL, 2, NULL);

    xTaskCreate(button_task, "button_task", 4096, NULL, 3, NULL);
    xTaskCreate(encryption_button_task, "encryption_button_task", 4096, NULL, 3, NULL);

    xTaskCreate(ST7789, "ST7789", 4096, NULL, 1, NULL);
}
