#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "ili9341.h"
#include "fonts.h"

// Definición de pines de los botones
#define BOTON_ARRANCAR_PIN GPIO_NUM_35
#define BOTON_RESET_PIN    GPIO_NUM_22  // Botón Reset
#define BOTON_CONGELAR_PIN GPIO_NUM_27  // Botón Congelar

// Definición de pines de los LEDs RGB
#define LED_ROJO_PIN    GPIO_NUM_4
#define LED_VERDE_PIN   GPIO_NUM_16
#define LED_AZUL_PIN    GPIO_NUM_17

// Definición de la constante para el desplazamiento de la pantalla
#define OFFSET_X 0

// Definición de las constantes para el manejo de eventos
#define BIT_ARRANCAR    (1 << 0)
#define BIT_RESET       (1 << 1)
#define BIT_CONGELAR    (1 << 2)

EventGroupHandle_t xEventBotones;  // Variable para el grupo de eventos

// Definiciones de las variables globales para el contador
volatile uint32_t decimas = 0;  // Contador de décimas
volatile bool contador_activo = false;  // Flag para el estado del contador
volatile bool contador_congelado = false;  // Flag para saber si el contador está congelado

// Definición de los últimos tres lapsos
uint32_t lapsos[3] = {0};  // Lapso 1, 2, 3

// Función para mostrar un lapso en la pantalla
void mostrar_lapso(uint16_t y, uint32_t tiempo, uint16_t color, char *label) {
    char texto[32];
    uint32_t mins = (tiempo / 6000) % 100;
    uint32_t secs = (tiempo / 100) % 60;
    uint32_t d = tiempo % 100;

    snprintf(texto, sizeof(texto), "%s %02lu:%02lu.%02lu", label, mins, secs, d);
    ILI9341DrawRectangle(10 + OFFSET_X, y, 320, y + 20, ILI9341_BLACK); // Limpia la línea
    ILI9341DrawString(10 + OFFSET_X, y, texto, &font_11x18, color, ILI9341_BLACK);

    // Debug por consola
    printf("[Pantalla] %s\n", texto);
}

// Configuración de los pines de los botones
void configurar_botones(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOTON_ARRANCAR_PIN) | (1ULL << BOTON_RESET_PIN) | (1ULL << BOTON_CONGELAR_PIN),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
}

// Configuración de los pines de los LEDs RGB
void configurar_led_rgb(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_ROJO_PIN) | (1ULL << LED_VERDE_PIN) | (1ULL << LED_AZUL_PIN),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_ROJO_PIN, 1);
    gpio_set_level(LED_VERDE_PIN, 1);
    gpio_set_level(LED_AZUL_PIN, 1);
}

// Tarea para leer el estado de los botones
void tarea_lectura_botones(void *pvParameters) {
    bool estado_anterior_arrancar = true;
    bool estado_anterior_reset = true;
    bool estado_anterior_congelar = true;

    while (1) {
        // Detectar presionado del botón ARRANCAR
        if (gpio_get_level(BOTON_ARRANCAR_PIN) == 0 && estado_anterior_arrancar) {
            xEventGroupSetBits(xEventBotones, BIT_ARRANCAR);
            printf("Botón ARRANCAR presionado\n");
        }
        estado_anterior_arrancar = gpio_get_level(BOTON_ARRANCAR_PIN);

        // Detectar presionado del botón RESET
        if (gpio_get_level(BOTON_RESET_PIN) == 0 && estado_anterior_reset) {
            xEventGroupSetBits(xEventBotones, BIT_RESET);
            printf("Botón RESET presionado\n");
        }
        estado_anterior_reset = gpio_get_level(BOTON_RESET_PIN);

        // Detectar presionado del botón CONGELAR
        if (gpio_get_level(BOTON_CONGELAR_PIN) == 0 && estado_anterior_congelar) {
            xEventGroupSetBits(xEventBotones, BIT_CONGELAR);
            printf("Botón CONGELAR presionado\n");
        }
        estado_anterior_congelar = gpio_get_level(BOTON_CONGELAR_PIN);

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

// Tarea para manejar los eventos generados por los botones
void tarea_eventos(void *pvParameters) {
    EventBits_t eventos;
    while (1) {
        // Espera por cualquier evento de los botones
        eventos = xEventGroupWaitBits(xEventBotones,
                                       BIT_ARRANCAR | BIT_RESET | BIT_CONGELAR,
                                       pdTRUE,
                                       pdFALSE,
                                       portMAX_DELAY);

        // Evento ARRANCAR
        if (eventos & BIT_ARRANCAR) {
            contador_activo = !contador_activo;
            contador_congelado = !contador_activo;
            printf("Contador %s\n", contador_activo ? "encendido" : "apagado");
        }

        // Evento RESET
        if (eventos & BIT_RESET) {
            if (!contador_activo) {
                decimas = 0;
                for (int i = 0; i < 3; i++) lapsos[i] = 0;
                printf("Contador reseteado\n");
            }
            contador_congelado = false;
        }

        // Evento CONGELAR
        if (eventos & BIT_CONGELAR) {
            if (contador_activo) {
                // Guardar el lapso
                for (int i = 2; i > 0; i--) {
                    lapsos[i] = lapsos[i - 1];
                }
                lapsos[0] = decimas;
                printf(">> Lapso guardado: %lu décimas\n", lapsos[0]);
            } else {
                contador_congelado = !contador_congelado;
                printf("Contador %s\n", contador_congelado ? "congelado" : "descongelado");
            }
        }
    }
}

// Tarea para contar el tiempo
void tarea_contador(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1) {
        if (contador_activo) {
            decimas++;
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

// Tarea para mostrar los valores en la pantalla LCD
void tarea_mostrar(void *pvParameters) {
    char texto[16];
    ILI9341Init();
    ILI9341Fill(ILI9341_BLACK);
    ILI9341Rotate(ILI9341_Landscape_1);

    while (1) {
        static uint32_t total = 0;
        if (!contador_congelado) {
            total = decimas;
        }

        uint32_t mins = (total / 6000) % 100;
        uint32_t secs = (total / 100) % 60;
        uint32_t d = total % 100;

        snprintf(texto, sizeof(texto), "%02lu:%02lu.%02lu", mins, secs, d);
        ILI9341DrawString(10 + OFFSET_X, 80, texto, &font_16x26, ILI9341_WHITE, ILI9341_BLACK);
        printf("[Tiempo] %s\n", texto);

        // Mostrar los 3 últimos lapsos
        mostrar_lapso(120, lapsos[0], ILI9341_CYAN, "Lapso 1:");
        mostrar_lapso(140, lapsos[1], ILI9341_CYAN, "Lapso 2:");
        mostrar_lapso(160, lapsos[2], ILI9341_CYAN, "Lapso 3:");

        vTaskDelay(pdMS_TO_TICKS(45));
    }
}

// Tarea para manejar el estado de los LEDs
void tarea_led_status(void *pvParameters) {
    bool led_verde_estado = true;
    while (1) {
        if (contador_activo) {
            led_verde_estado = !led_verde_estado;
            gpio_set_level(LED_VERDE_PIN, led_verde_estado);
        } else {
            gpio_set_level(LED_VERDE_PIN, 1);
        }

        gpio_set_level(LED_ROJO_PIN, (contador_activo == false && contador_congelado == true) ? 0 : 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Función principal
void app_main(void) {
    configurar_botones();
    configurar_led_rgb();

    xEventBotones = xEventGroupCreate();  // Crear el grupo de eventos

    // Crear las tareas
    xTaskCreate(tarea_lectura_botones, "tarea_botones", 2048, NULL, 5, NULL);
    xTaskCreate(tarea_eventos, "tarea_eventos", 2048, NULL, 5, NULL);
    xTaskCreate(tarea_contador, "tarea_contador", 2048, NULL, 5, NULL);
    xTaskCreate(tarea_mostrar, "tarea_mostrar", 4096, NULL, 5, NULL);
    xTaskCreate(tarea_led_status, "tarea_led_status", 2048, NULL, 5, NULL);
}
