#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/shapeRenderer/ShapeRenderer.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"
#include "Encoder.h"
#include "Button.h"
#include "Led.h"
#include "uiQueue.h"
#include "CV.h"

using namespace pico_ssd1306;

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5

// Gate outputs
#define OUT1 3
#define OUT2 2
#define OUT4 1
#define OUT8 0

// Encoder
#define ENCA 16
#define ENCB 17
#define ENC_SW 18

// Run button
#define RUN_SW 6
#define RUN_LED 7

// Sync input
#define NSYNC 19

// Analog inputs
#define DUTY_CYCLE 26
#define DUTY_CYCLE_ADC 0
#define BPM_CV 27
#define BPM_CV_ADC 1

Encoder enc;
Button runButton;
uiQueue controlsQ;
Led runLed;
CV bpm_cv;
CV duty_cv;

enum RunState {
    R_HALTED,
    R_RUNNING,
    R_WAITING_FOR_SYNC,
};

enum SyncModeType {
    SYNC_OFF,
    SYNC_START,
    SYNC_ALWAYS,
};
SyncModeType syncMode;

RunState current_state = R_HALTED;

int tcount = 0; // current count of ms for output1

int bpm; // beats-per-minute
int ms_per_beat; // milliseconds per beat
int duty; // duty cycle: 0 = 0%, 4095 = 100%

int high_time; // high time for out1 in ms
int high_time_reg;

int out_idx = 0; // current index of outputs (0 - 7)

void resetTempo() {
    out_idx = 0;
    tcount = 0;
    gpio_put(OUT1, false);
    gpio_put(OUT2, false);
    gpio_put(OUT4, false);
    gpio_put(OUT8, false);
}

void RecalcTempo() {
    ms_per_beat = (60 * 1000) / bpm;
    high_time = ms_per_beat * (1 + duty) / 4096;
    if (high_time < 1)
        high_time = 1;

    if (high_time >= ms_per_beat)
        high_time = ms_per_beat-1;
}

void updateTempo(int new_bpm) {
    if (new_bpm < 1)
        new_bpm = 1;
    if (new_bpm > 600)
        new_bpm = 600;
 
    bpm = new_bpm;
    RecalcTempo();
}

void updateDuty(int new_duty) {
    if (new_duty < 1)
        new_duty = 1;
    if (new_duty > 4095)
        new_duty = 4095;

    duty = new_duty;
    RecalcTempo();
}

bool last_nsync = true;

void updateOutputs() {
    bool nsync = gpio_get(NSYNC);
    if ((current_state == R_WAITING_FOR_SYNC) && (syncMode != SYNC_OFF)) {
        if (!nsync && last_nsync) {
            tcount = 0;
            current_state = R_RUNNING;
            runLed.SetState(Led::LED_ON);
        }
    }
    last_nsync = nsync;
    if (current_state != R_RUNNING)
        return;

    if (((tcount + 1) == ms_per_beat) && (syncMode == SYNC_ALWAYS)) {
        tcount = 0;
        current_state = R_WAITING_FOR_SYNC;
        gpio_put(OUT1, false);
        gpio_put(OUT2, false);
        gpio_put(OUT4, false);
        gpio_put(OUT8, false);
        return;
    }

    tcount++;
    if (tcount >= ms_per_beat) {
        tcount = 0;
        out_idx = (out_idx + 1) % 8;
        high_time_reg = high_time;
        //printf("incremented out_idx, out_idx: %d, high_time: %d, ms_per_beat: %d\n", out_idx, high_time, ms_per_beat);
    }
    gpio_put(OUT1, (tcount < high_time_reg));
    gpio_put(OUT2, ((tcount < high_time_reg) && ((out_idx % 2) == 0)));
    gpio_put(OUT4, ((tcount < high_time_reg) && ((out_idx % 4) == 0)));
    gpio_put(OUT8, ((tcount < high_time_reg) && ((out_idx % 8) == 0)));
}

void updateUi()
{
    enc.Debounce();
    int inc = enc.Increment();
    if (inc < 0)
    {
        controlsQ.Enqueue(uiQueue::MSG_ENC_DEC);
        //puts("Enqueued ENC_DEC message");
    }
    else if (inc > 0)
    {
        controlsQ.Enqueue(uiQueue::MSG_ENC_INC);
        //puts("Enqueued ENC_INC message");
    }

    bool ebutton = enc.RisingEdge();
    if (ebutton)
    {
        controlsQ.Enqueue(uiQueue::MSG_ENC_PRESSED);
        //puts("Enqueued ENC_PRESSED message");
    }

    runButton.Debounce();
    bool rbutton = runButton.RisingEdge();
    if (rbutton)
    {
        controlsQ.Enqueue(uiQueue::MSG_RUN_PRESSED);
        //puts("Enqueued RUN_PRESSED message");
    }

    runLed.Update();
}


bool timer_callback(struct repeating_timer *t) {
    // if (tcount == 0)
    //	puts("Hello, from repeating timer!");
    updateOutputs();
    updateUi();

    return true;
}

int manual_bpm;
int bpm_cv_zero;
int bpm_cv_attenuvert;

void updateManTempo(int man_bpm) {
    if (man_bpm < 1)
        man_bpm = 1;
    if (man_bpm > 600)
        man_bpm = 600;
    manual_bpm = man_bpm;
}

int calibratedCV(int cv, int zero) {
    return (cv + zero) - 2048;
}

int calculateBPM(int man, int cv, int zero, int attenuvert) {
    return man + ((calibratedCV(cv, zero) * 300) / 2048) * (attenuvert / 2048);
}

enum DispModeType {
    DISP_BPM,
    DISP_SYNC,
    DISP_ATTENUVERT,
    DISP_CVZERO,
    DISP_MENU,
};
DispModeType displayMode;
int selectedMenu = DISP_BPM;

char bpmStr[] = "BPM";
char syncStr[] = "Sync";
char attenStr[] = "Attenuvert";
char cvzeroStr[] = "CV Zero";
char menuStr[] = "Menu";

char *dispMode2Str (DispModeType mode) {
    char *ret;
    switch (mode) {
        case DISP_BPM:
            ret = bpmStr;
            break;
        case DISP_SYNC:
            ret = syncStr;
            break;
        case DISP_ATTENUVERT:
            ret = attenStr;
            break;
        case DISP_CVZERO:
            ret = cvzeroStr;
            break;
        default:
            ret = menuStr;
            break;
    }
    return ret;
}


char syncoffStr[] = "Sync Off";
char syncstartStr[] = "Sync Start";
char syncalwaysStr[] = "Sync Always";

char *syncMode2Str (SyncModeType mode) {
    char *ret;
    switch (mode) {
        case SYNC_OFF:
            ret = syncoffStr;
            break;
        case SYNC_START:
            ret = syncstartStr;
            break;
        default:
            ret = syncalwaysStr;
            break;
    }
    return ret;
}

bool getControls() {
    bool update = false;

    int old_tempo = bpm;
    uint16_t bpm_cv_val = bpm_cv.Read();

    if (!controlsQ.Empty())
    {
        uiQueue::uiMsg msg = controlsQ.Dequeue();
        // printf("not empty! msg=%d\n", msg);
        int smode = syncMode;
        switch (msg)
        {
        case uiQueue::MSG_ENC_DEC:
            switch (displayMode)
            {
            case DISP_BPM:
                updateManTempo(manual_bpm - 1);
                break;
            case DISP_SYNC:
                if (smode > SYNC_OFF)
                    smode--;
                syncMode = (SyncModeType) smode;
                break;
            case DISP_ATTENUVERT:
                bpm_cv_attenuvert -= 20;
                if (bpm_cv_attenuvert < -2048)
                    bpm_cv_attenuvert = -2048;
            case DISP_CVZERO:
                bpm_cv_zero--;
                if (bpm_cv_zero < -2048)
                    bpm_cv_zero = -2048;
            case DISP_MENU:
                if (selectedMenu > DISP_BPM)
                    selectedMenu--;
                break;
            }
            update = true;
            //puts("Dequeued enc decrement message");
            break;
        case uiQueue::MSG_ENC_INC:
            switch (displayMode)
            {
            case DISP_BPM:
                updateManTempo(manual_bpm + 1);
                break;
            case DISP_SYNC:
                if (smode < SYNC_ALWAYS)
                    smode++;
                syncMode = (SyncModeType) smode;
                break;
            case DISP_ATTENUVERT:
                bpm_cv_attenuvert += 20;
                if (bpm_cv_attenuvert > 2047)
                    bpm_cv_attenuvert = 2047;
            case DISP_CVZERO:
                bpm_cv_zero++;
                if (bpm_cv_zero > 2047)
                    bpm_cv_zero = 2047;
            case DISP_MENU:
                if (selectedMenu < DISP_CVZERO)
                    selectedMenu++;
                break;
            }
            update = true;
            //puts("Dequeued enc increment message");
            break;
        case uiQueue::MSG_ENC_PRESSED:
            if (displayMode == DISP_MENU) {
                displayMode = (DispModeType) selectedMenu;
            } else if (displayMode == DISP_BPM) {
                displayMode = DISP_MENU;
            } else {
                displayMode = DISP_BPM;   
            }
            update = true;
            //puts("Dequeued enc pressed message");
            break;
        case uiQueue::MSG_RUN_PRESSED:
            switch (current_state)
            {
            case R_HALTED:
                // FIXME: change to be a function of sync mode
                switch (syncMode) {
                    case SYNC_OFF:
                        current_state = R_RUNNING;
                        runLed.SetState(Led::LED_ON);
                        break;
                    case SYNC_START:
                        resetTempo(); // always start from scratch in this mode
                        current_state = R_WAITING_FOR_SYNC;
                        runLed.SetState(Led::LED_SLOW);
                        break;
                    case SYNC_ALWAYS:
                        current_state = R_WAITING_FOR_SYNC; // pick up where we left off
                        runLed.SetState(Led::LED_FAST);
                        break;
                }
                break;
            case R_RUNNING:
            case R_WAITING_FOR_SYNC:
                current_state = R_HALTED;
                runLed.SetState(Led::LED_OFF);
                break;
            }
            //puts("Dequeued run pressed message");
            break;
        }
    }

    updateTempo(calculateBPM(manual_bpm, bpm_cv_val, bpm_cv_zero, bpm_cv_attenuvert));
    if (bpm != old_tempo)
        update = true;

    updateDuty(duty_cv.Read());

    return update;
}

void updateDisplay(SSD1306 &display) {
    display.clear();
    display.sendBuffer();

    // show centered title at top
    char *title = dispMode2Str((DispModeType) displayMode);
    drawText(&display, font_12x16, title, 64 - (strlen(title) * 12)/2, 0, WriteMode::ADD);

    char sbuf[20];
    char *syncstr = syncMode2Str(syncMode);
    char *menustr = dispMode2Str((DispModeType) selectedMenu);
    switch (displayMode)
    {
    case DISP_BPM:
        sprintf(sbuf, "%3.1d", bpm);
        drawText(&display, font_16x32, sbuf, 36, 30, WriteMode::ADD);
        break;
    case DISP_SYNC:
        drawLine(&display, 0, 31, 12 * strlen(syncstr), 31, WriteMode::ADD);
        drawInvText(&display, font_12x16, syncstr, 0, 32, WriteMode::ADD);
        break;
    case DISP_ATTENUVERT:
        sprintf(sbuf, "%2.2d", (bpm_cv_attenuvert * 100) / 2048);
        drawText(&display, font_16x32, sbuf, 36, 30, WriteMode::ADD);
        break;
    case DISP_CVZERO:
        sprintf(sbuf, "%4.4d", calibratedCV(bpm_cv.Read(), bpm_cv_zero));
        drawText(&display, font_16x32, sbuf, 36, 30, WriteMode::ADD);
        break;
    case DISP_MENU:  
        drawLine(&display, 1, 31, 12 * strlen(menustr), 31, WriteMode::ADD);
        drawInvText(&display, font_12x16, menustr, 1, 32, WriteMode::ADD);
        break;
    }
    display.sendBuffer();
}

// Last 4K sector in 2MB flash
#define FLASH_TARGET_OFFSET 0x1ff000
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

#define SAVE_OFFSET_VERSION 0
#define SAVE_OFFSET_BPM 1
#define SAVE_OFFSET_ZERO 2
#define SAVE_OFFSET_ATTENUVERT 3
#define SAVE_OFFSET_SYNCMODE 4

#define SAVE_VERSION 0x0

absolute_time_t save_time;
void restoreState() {
    //puts("entering restoreState");
    save_time = get_absolute_time();
    int *buffer = (int *) flash_target_contents; // cast to allow int access

    if (buffer[SAVE_OFFSET_VERSION] == SAVE_VERSION) {
        //puts("Restoring saved state");
        manual_bpm = buffer[SAVE_OFFSET_BPM];
        bpm_cv_zero = buffer[SAVE_OFFSET_ZERO];
        bpm_cv_attenuvert = buffer[SAVE_OFFSET_ATTENUVERT];
        syncMode = (SyncModeType) buffer[SAVE_OFFSET_SYNCMODE];
    } else {
        // set default
        //puts("Setting factory defaults");
        manual_bpm = 100;
        bpm_cv_zero = 0;
        bpm_cv_attenuvert = 0;
        syncMode = SYNC_OFF;
    }
}

void saveState()
{
    //puts("Entering saveState");
    // only save if data has changed
    //
    int *buffer = (int *) flash_target_contents; // cast to allow int access
    if ((buffer[SAVE_OFFSET_VERSION] != SAVE_VERSION) ||
        (manual_bpm != buffer[SAVE_OFFSET_BPM]) ||
        (bpm_cv_zero != buffer[SAVE_OFFSET_ZERO]) ||
        (bpm_cv_attenuvert != buffer[SAVE_OFFSET_ATTENUVERT]) ||
        (syncMode != (SyncModeType) buffer[SAVE_OFFSET_SYNCMODE])) {
        
        absolute_time_t now = get_absolute_time();
        // save no faster than every 1 minute
        if ((absolute_time_diff_us(save_time, now) > (60 * 1000 * 1000)) || (buffer[SAVE_OFFSET_VERSION] != SAVE_VERSION))
        {
            //puts("Saving state");
            save_time = now;
            uint8_t write_buffer_bytes[FLASH_PAGE_SIZE];
            int *write_buffer = (int *) write_buffer_bytes; // cast to allow int access
            write_buffer[SAVE_OFFSET_VERSION] = SAVE_VERSION;
            write_buffer[SAVE_OFFSET_BPM] = manual_bpm;
            write_buffer[SAVE_OFFSET_ZERO] = bpm_cv_zero;
            write_buffer[SAVE_OFFSET_ATTENUVERT] = bpm_cv_attenuvert;
            write_buffer[SAVE_OFFSET_SYNCMODE] = (int) syncMode;

            uint32_t ints = save_and_disable_interrupts();
            flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
            flash_range_program(FLASH_TARGET_OFFSET, write_buffer_bytes, FLASH_PAGE_SIZE);
            restore_interrupts (ints);
        //} else {
        //    puts ("Not time yet!");
        }
    //} else {
    //    puts ("Data not changed");
    }
}

int main()
{
    struct repeating_timer timer_data; // can't ever go out of scope



    stdio_init_all();

    //sleep_ms(2000);
    //puts("Hello!");

    adc_init();

    // I2C Initialisation. Using it at 1MHz.
    i2c_init(I2C_PORT, 1000 * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Sync input
    gpio_init(NSYNC);
    gpio_set_dir(NSYNC, GPIO_IN);

    // Outputs
    gpio_init(OUT1);
    gpio_set_dir(OUT1, GPIO_OUT);
    gpio_init(OUT2);
    gpio_set_dir(OUT2, GPIO_OUT);
    gpio_init(OUT4);
    gpio_set_dir(OUT4, GPIO_OUT);
    gpio_init(OUT8);
    gpio_set_dir(OUT8, GPIO_OUT);

    restoreState();

    //updateTempo();
    //makupdateDuty(1023);
    resetTempo();
    high_time_reg = high_time;
    current_state = R_HALTED;


    enc.Init(ENCA, ENCB, ENC_SW);
    runButton.Init(RUN_SW, Button::PULLUP, true);
    runLed.Init(RUN_LED, 1000);
    bpm_cv.Init(BPM_CV, BPM_CV_ADC);
    duty_cv.Init(DUTY_CYCLE, DUTY_CYCLE_ADC);

    controlsQ.Init();

    // Create a new display object
    SSD1306 display = SSD1306(I2C_PORT, 0x3C, Size::W128xH64);
    display.setOrientation(false);

    displayMode = DISP_BPM;

    // Start 1ms repeating timer
    add_repeating_timer_ms(-1, timer_callback, NULL, &timer_data);

    bool first_time = true;
    while (1)
    {
        if (getControls() || first_time) {
            updateDisplay(display);
            first_time = false;
        }
        saveState();
    }

    return 0;
}
