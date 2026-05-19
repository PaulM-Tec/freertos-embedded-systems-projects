#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

/* =====================  ENUMS & TYPES  ===================== */
typedef enum { TL_GREEN, TL_YELLOW, TL_RED } LightState;
typedef enum { PED_WALK, PED_DONT_WALK } PedState;

typedef struct
{
    LightState ns;
    LightState ew;
    PedState   nsPed;
    PedState   ewPed;
} IntersectionState;

/* =====================  CONSTANTS  ========================= */
/* Timing in milliseconds */
#define NS_GREEN_MS   10000
#define NS_YELLOW_MS  3000
#define NS_RED_MS     (EW_GREEN_MS + EW_YELLOW_MS)  /* 8s + 3s = 11s */

#define EW_GREEN_MS   8000
#define EW_YELLOW_MS  3000
#define EW_RED_MS     (NS_GREEN_MS + NS_YELLOW_MS)  /* 10s + 3s = 13s */

/* Ped walk windows (start of each red period) */
#define NS_PED_WALK_MS 8000   /* During NS red (which lasts 11s) */
#define EW_PED_WALK_MS 6000   /* During EW red (which lasts 13s) */

/* Emergency simulation timing */
#define EMERGENCY_PERIOD_MS 30000
#define EMERGENCY_CLEAR_MS   5000

/* Event bits */
#define EVBIT_EMERGENCY  (1U << 0)

/* =====================  GLOBALS  =========================== */
static FILE* gLogFile = NULL;
static SemaphoreHandle_t   gLogMutex = NULL;
static EventGroupHandle_t  gEvents = NULL;            /* For EMERGENCY detection */
static SemaphoreHandle_t   gEmergencyClearedSem = NULL;

static IntersectionState   gState;                    /* Current visual state */

/* =====================  LOGGING  =========================== */
static void log_line(const char* msg)
{
    if (!msg) return;

    /* Timestamp (seconds since epoch is fine for this sim) */
    time_t now = time(NULL);
    long long ts = (long long)now;

    if (gLogMutex) xSemaphoreTake(gLogMutex, portMAX_DELAY);

    /* Mirror to console */
    printf("[%lld] %s\n", ts, msg);
    fflush(stdout);

    /* Persist to file (if opened) */
    if (gLogFile) {
        fprintf(gLogFile, "[%lld] %s\n", ts, msg);
        fflush(gLogFile);
    }

    if (gLogMutex) xSemaphoreGive(gLogMutex);
}

/* Helper for formatted logging */
static void logf(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    log_line(buf);
}

/* =====================  STATE PRINT  ======================= */
static const char* tl_to_str(LightState s)
{
    switch (s) {
    case TL_GREEN:  return "GREEN";
    case TL_YELLOW: return "YELLOW";
    case TL_RED:    return "RED";
    default:        return "?";
    }
}

static const char* ped_to_str(PedState p)
{
    return (p == PED_WALK) ? "WALK" : "DON'T WALK";
}

static void print_state(void)
{
    /* Single line snapshot to help visually: */
    logf("NS:%s  EW:%s  |  NS Ped:%s  EW Ped:%s",
        tl_to_str(gState.ns), tl_to_str(gState.ew),
        ped_to_str(gState.nsPed), ped_to_str(gState.ewPed));
}

/* Setters that also log */
static void set_ns(LightState s)
{
    gState.ns = s;
    logf("North-South Light: %s", tl_to_str(s));
}

static void set_ew(LightState s)
{
    gState.ew = s;
    logf("East-West Light: %s", tl_to_str(s));
}

static void set_ns_ped(PedState p)
{
    gState.nsPed = p;
    logf("North-South Pedestrian: %s", ped_to_str(p));
}

static void set_ew_ped(PedState p)
{
    gState.ewPed = p;
    logf("East-West Pedestrian: %s", ped_to_str(p));
}

/* =====================  UTILS  ============================= */
/* Wait up to ms for either: timeout OR EMERGENCY bit set.
   Returns true if EMERGENCY occurred (i.e., should preempt). */
static bool wait_or_emergency(TickType_t ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        gEvents,
        EVBIT_EMERGENCY,
        pdFALSE,    /* do not clear on exit */
        pdFALSE,    /* wait for any bit */
        pdMS_TO_TICKS(ms)
    );
    return (bits & EVBIT_EMERGENCY) != 0;
}

/* Block here until EmergencyTask signals clearance. */
static void wait_emergency_clear(void)
{
    /* If we got here, emergency bit is set.
       Just wait for the clear semaphore from EmergencyTask. */
    xSemaphoreTake(gEmergencyClearedSem, portMAX_DELAY);
}

/* Guard: if emergency just started, avoid overriding emergency state. */
static inline bool emergency_active(void)
{
    return (xEventGroupGetBits(gEvents) & EVBIT_EMERGENCY) != 0;
}

/* =====================  TASKS  ============================= */
/* ---- EmergencyTask ----
   Simulates an emergency every 30s:
   - Set EMERGENCY bit
   - Force EW GREEN / NS RED; Peds DON'T WALK
   - Hold 5s
   - Clear EMERGENCY; signal controller to resume
*/
static void EmergencyTask(void* arg)
{
    (void)arg;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(EMERGENCY_PERIOD_MS));

        /* Trigger emergency */
        xEventGroupSetBits(gEvents, EVBIT_EMERGENCY);
        log_line("*** EMERGENCY DETECTED! PRIORITIZING TRAFFIC ***");

        /* Force immediate safe/priority state:
           - Let East-West pass (GREEN)
           - North-South RED
           - Pedestrians DON'T WALK
         */
        set_ns(TL_RED);
        set_ew(TL_GREEN);
        set_ns_ped(PED_DONT_WALK);
        set_ew_ped(PED_DONT_WALK);
        print_state();

        vTaskDelay(pdMS_TO_TICKS(EMERGENCY_CLEAR_MS));

        /* Clear and allow normal cycle to resume */
        xEventGroupClearBits(gEvents, EVBIT_EMERGENCY);
        log_line("*** EMERGENCY CLEARED — returning to normal cycle ***");

        /* Unblock controller (in case it is waiting) */
        xSemaphoreGive(gEmergencyClearedSem);
    }
}

/* ---- ControllerTask ----
   Runs the traffic light cycle:
   - NS: Green 10s, Yellow 3s, then EW runs
   - EW: Green 8s, Yellow 3s
   Pedestrians:
   - NS Ped WALK 8s at start of NS RED (11s total red)
   - EW Ped WALK 6s at start of EW RED (13s total red)
   Any phase wait is preempted immediately on EMERGENCY.
*/
static void ControllerTask(void* arg)
{
    (void)arg;

    /* Initial baseline state */
    set_ns(TL_RED);
    set_ew(TL_RED);
    set_ns_ped(PED_DONT_WALK);
    set_ew_ped(PED_DONT_WALK);
    print_state();

    for (;;)
    {
        /* ================= NS CYCLE ================= */

        /* At start of NS green, EW is RED for the whole NS (G+Y=13s).
           During the first 6s of that red, EW Ped = WALK.
           Then EW Ped = DON'T WALK for remaining 7s.
         */

         /* Begin NS GREEN (10s) */
        if (!emergency_active()) set_ns(TL_GREEN);
        if (!emergency_active()) set_ew(TL_RED);

        /* EW pedestrians WALK (first 6s of EW red) */
        if (!emergency_active()) set_ew_ped(PED_WALK);
        print_state();

        /* Wait up to 6s or preempt on emergency */
        if (wait_or_emergency(EW_PED_WALK_MS)) { wait_emergency_clear(); continue; }

        /* Remainder of EW RED during NS (13s total, 6s already elapsed -> 7s left),
           EW pedestrians DON'T WALK for safety before NS YELLOW & phase change. */
        if (!emergency_active()) set_ew_ped(PED_DONT_WALK);
        print_state();

        /* Continue NS GREEN to reach total 10s:
           We've already waited 6s; 4s remain of NS GREEN. */
        if (wait_or_emergency(NS_GREEN_MS - EW_PED_WALK_MS)) { wait_emergency_clear(); continue; }

        /* NS YELLOW (3s) */
        if (!emergency_active()) set_ns(TL_YELLOW);
        print_state();
        if (wait_or_emergency(NS_YELLOW_MS)) { wait_emergency_clear(); continue; }

        /* Now NS becomes RED; EW will soon go GREEN. */
        if (!emergency_active()) set_ns(TL_RED);
        print_state();

        /* ================= EW CYCLE ================= */

        /* At start of EW green (8s), NS is RED for the whole EW (G+Y=11s).
           During the first 8s of that red, NS Ped = WALK, then DON'T WALK for remaining 3s.
         */

         /* NS pedestrians WALK (first 8s of NS red) */
        if (!emergency_active()) set_ns_ped(PED_WALK);

        /* EW GREEN (8s) */
        if (!emergency_active()) set_ew(TL_GREEN);
        print_state();

        /* Wait up to 8s or preempt on emergency */
        if (wait_or_emergency(EW_GREEN_MS)) { wait_emergency_clear(); continue; }

        /* NS pedestrians DON'T WALK for last 3s of NS red */
        if (!emergency_active()) set_ns_ped(PED_DONT_WALK);
        print_state();

        /* EW YELLOW (3s) */
        if (!emergency_active()) set_ew(TL_YELLOW);
        print_state();
        if (wait_or_emergency(EW_YELLOW_MS)) { wait_emergency_clear(); continue; }

        /* EW turns RED as cycle completes */
        if (!emergency_active()) set_ew(TL_RED);
        print_state();

        /* Loop back for next NS cycle */
    }
}

/* ---- heap_5 initialisation (required) ---------------------- */
/* heap_5 lets you define one or more regions. For this sim we just
   use one contiguous region inside a static array. */
static void prvInitialiseHeap(void)
{
    /* One big region equals configTOTAL_HEAP_SIZE bytes. */
    static uint8_t ucHeap[configTOTAL_HEAP_SIZE];

    /* Regions must be in ascending address order and terminated with { NULL, 0 }. */
    const HeapRegion_t xHeapRegions[] =
    {
        { ucHeap, sizeof(ucHeap) },
        { NULL,   0              }
    };

    vPortDefineHeapRegions(xHeapRegions);
}

/* =====================  MAIN  ============================== */
int main(void)
{
    /* 1) Initialise heap_5 regions BEFORE any RTOS object is created. */
    prvInitialiseHeap();

    /* 2) Then open the log, create mutex/semaphores, and tasks as you already do. */
    gLogFile = fopen("traffic_log.txt", "w");
    if (!gLogFile) {
        printf("[WARN] Could not open traffic_log.txt; logging to console only.\n");
    }

    gLogMutex = xSemaphoreCreateMutex();
    gEvents = xEventGroupCreate();
    gEmergencyClearedSem = xSemaphoreCreateBinary();

    /* Optional: quick sanity asserts */
    configASSERT(gLogMutex != NULL);
    configASSERT(gEvents != NULL);
    configASSERT(gEmergencyClearedSem != NULL);

    /* 3) Create tasks */
    xTaskCreate(ControllerTask, "Controller", 2048, NULL, 2, NULL);
    xTaskCreate(EmergencyTask, "Emergency", 1024, NULL, 3, NULL);

    /* 4) Start the scheduler (Windows port handles the tick) */
    vTaskStartScheduler();
    return 0;
}

/* ============================================================
*                REQUIRED FREERTOS HOOKS (WIN32)
*  Add these at the bottom of main.c to satisfy the demo config
* ============================================================ */

#include <windows.h>   /* For Sleep() in asserts on Windows */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/* ---------- configASSERT() hook ---------- */
void vAssertCalled(const char* file, unsigned long line)
{
    /* Print once (optional) */
    printf("[ASSERT] %s : %lu\n", file ? file : "?", line);

    /* Stop here so you notice during debug. On Windows we just sleep. */
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        Sleep(100);
    }
}

/* ---------- Malloc failed hook (if configUSE_MALLOC_FAILED_HOOK == 1) ---------- */
void vApplicationMallocFailedHook(void)
{
    /* Allocation failed – stop the system so it’s visible in the sim. */
    printf("[ERROR] vApplicationMallocFailedHook: heap allocation failed\n");
    vAssertCalled("malloc_failed", 0);
}

/* ---------- Stack overflow hook (if configCHECK_FOR_STACK_OVERFLOW >= 1) ---------- */
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char* pcTaskName)
{
    (void)pxTask;
    printf("[ERROR] vApplicationStackOverflowHook: task=%s\n", pcTaskName ? pcTaskName : "?");
    vAssertCalled("stack_overflow", 0);
}

/* ---------- Idle hook (if configUSE_IDLE_HOOK == 1) ---------- */
void vApplicationIdleHook(void)
{
    /* On Windows sim do nothing (avoid busy-spinning).
       DO NOT call vTaskDelay here. */
    Sleep(0);
}

/* ---------- Static allocation support (if configSUPPORT_STATIC_ALLOCATION == 1) ---------- */
#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

/* Idle Task memory */
static StaticTask_t xIdleTaskTCB;
static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
    StackType_t** ppxIdleTaskStackBuffer,
    uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}

/* Timer Task memory */
static StaticTask_t xTimerTaskTCB;
static StackType_t  uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer,
    StackType_t** ppxTimerTaskStackBuffer,
    uint32_t* pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = (uint32_t)configTIMER_TASK_STACK_DEPTH;
}
<<<<<<< HEAD
#endif /* configSUPPORT_STATIC_ALLOCATION */
=======
#endif /* configSUPPORT_STATIC_ALLOCATION */
>>>>>>> 2353d8e (Restructured project folders: organized src and docs properly)
