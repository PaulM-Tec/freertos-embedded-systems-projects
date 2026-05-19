#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
 
/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
 
/* ===== Global Shared Variables (Protected by Mutex) ===== */
float gTemperature = 0.0f;
float gHumidity = 0.0f;
SemaphoreHandle_t dataMutex;
 
/* ===== File Pointer ===== */
FILE *logFile = NULL;
 
/* ===== Utility: Random float between min and max ===== */
static float random_float(float min, float max)
{
    float scale = rand() / (float) RAND_MAX;
    return min + scale * (max - min);
}
 
/* ===== Temperature Task (5 seconds) ===== */
void TemperatureTask(void *pv)
{
    for (;;)
    {
        float temp = random_float(18.0f, 28.0f);   // simulate sensor
 
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        gTemperature = temp;
        xSemaphoreGive(dataMutex);
 
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
 
/* ===== Humidity Task (7 seconds) ===== */
void HumidityTask(void *pv)
{
    for (;;)
    {
        float hum = random_float(40.0f, 70.0f);    // simulate sensor
 
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        gHumidity = hum;
        xSemaphoreGive(dataMutex);
 
        vTaskDelay(pdMS_TO_TICKS(7000));
    }
}
 
/* ===== Data Logging Task (10 seconds) ===== */
void LoggingTask(void *pv)
{
    for (;;)
    {
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        float temp = gTemperature;
        float hum = gHumidity;
        xSemaphoreGive(dataMutex);
 
        time_t now = time(NULL);
 
        if (logFile)
        {
            fprintf(logFile,
                    "[%lld] Temp: %.2f C, Humidity: %.2f %%\n",
                    (long long)now, temp, hum);
            fflush(logFile);
        }
 
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
 
/* ===== Display Task (2 seconds) ===== */
void DisplayTask(void *pv)
{
    for (;;)
    {
        float temp, hum;
 
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        temp = gTemperature;
        hum = gHumidity;
        xSemaphoreGive(dataMutex);
 
        /* Colorised console output (pink + teal style like sample) */
        printf("\x1b[38;2;255;105;180mCurrent Temperature:\x1b[0m \x1b[96m%.2f°C\x1b[0m, "
               "\x1b[38;2;255;105;180mHumidity:\x1b[0m \x1b[96m%.2f%%\x1b[0m\n",
               temp, hum);
 
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void prvInitialiseHeap(void)
{
    static uint8_t ucHeap[configTOTAL_HEAP_SIZE];

    const HeapRegion_t xHeapRegions[] =
    {
        { ucHeap, sizeof(ucHeap) },
        { NULL,   0 }
    };

    vPortDefineHeapRegions(xHeapRegions);
}
 
/* ===== Main ===== */
int main(void)
{
    prvInitialiseHeap();   // <<< REQUIRED BEFORE ANY FreeRTOS OBJECT

    srand((unsigned int)time(NULL));
 
    dataMutex = xSemaphoreCreateMutex();
    if (!dataMutex)
    {
        printf("Mutex creation failed.\n");
        return 1;
    }
 
    logFile = fopen("weather_log.txt", "w");
    if (!logFile)
    {
        printf("Could not open weather_log.txt\n");
        return 1;
    }
 
    /* Create tasks */
    xTaskCreate(TemperatureTask, "TempTask", 1024, NULL, 2, NULL);
    xTaskCreate(HumidityTask,    "HumTask",  1024, NULL, 2, NULL);
    xTaskCreate(LoggingTask,     "LogTask",  1024, NULL, 1, NULL);
    xTaskCreate(DisplayTask,     "DispTask", 1024, NULL, 1, NULL);
 
    printf("Weather Station System Running...\n");
 
    vTaskStartScheduler();
 
    return 0;
}

void vAssertCalled(const char* file, unsigned long line)
{
    printf("ASSERT: %s:%lu\n", file, line);
    for (;;) Sleep(100);
}

void vApplicationMallocFailedHook(void)
{
    printf("Malloc failed!\n");
    for (;;) Sleep(100);
}

void vApplicationStackOverflowHook(TaskHandle_t x, char* name)
{
    printf("Stack overflow in %s\n", name);
    for (;;) Sleep(100);
}

void vApplicationIdleHook(void)
{
    Sleep(0);
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticTask_t idleTask;
static StackType_t idleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(
    StaticTask_t** ppxIdleTaskTCBBuffer,
    StackType_t** ppxIdleTaskStackBuffer,
    uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idleTask;
    *ppxIdleTaskStackBuffer = idleStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t timerTask;
static StackType_t timerStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(
    StaticTask_t** ppxTimerTaskTCBBuffer,
    StackType_t** ppxTimerTaskStackBuffer,
    uint32_t* pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &timerTask;
    *ppxTimerTaskStackBuffer = timerStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif
