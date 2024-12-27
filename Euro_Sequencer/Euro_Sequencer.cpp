#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico-ssd1306/ssd1306.h"
#include "pico-ssd1306/shapeRenderer/ShapeRenderer.h"
#include "pico-ssd1306/textRenderer/TextRenderer.h"
#include "Euro_Sequencer.h"
#include "Encoder.h"
#include "Buttons.h"
#include "uiQueue.h"

using namespace pico_ssd1306;

Encoder enc;
Buttons buttons;
uiQueue inQ;

uint32_t __not_in_flash("ram") *spi0_dr = (uint32_t *)0x4003c008;
uint32_t __not_in_flash("ram") *spi0_sr = (uint32_t *)0x4003c00c;
uint32_t __not_in_flash("ram") *spi0_icr = (uint32_t *)0x4003c020;
uint32_t __not_in_flash("ram") *gp_in = (uint32_t *)0xd0000004;
uint32_t __not_in_flash("ram") *gp_set = (uint32_t *)0xd0000014;
uint32_t __not_in_flash("ram") *gp_clr = (uint32_t *)0xd0000018;



// These variables are how the dacLoop routine communicates with the
// rest of the code
// Note that led blinking is done elsewhere
uint32_t led_levels[24]; // 0-256for each led (0: fully off, 256 fully on)
uint32_t button_vector;
uint16_t dac_buf[4];

// "Local" variables used by dac_loop
int tcount = 0;
int led_cycle = 0;
int dac_idx = 0;
bool dac_cycle = true;
int dummy = 0;
uint32_t button_vector_tmp;

///////////////////////////////////////////////////////////////////////////
// Loop to constantly update of DAC, LEDs and buttons
// Runs completely out of RAM to avoid impact of flash updates
//
void __not_in_flash_func(core1_dacLoop) () {
    while (1)
    {
        if (dac_cycle)
        {
            // turn off last S/H switch in anticipation of DAC output changing
            *gp_clr = 1ul << dac_idx; // turn off switch
            dac_idx = (dac_idx + 1) % 4;
        }
        else
        {
            // DAC output should now be stable: turn on S/H switch
            *gp_set = 1ul << dac_idx; // turn on switch
        }

        // Flash "sign of life" on-board LED
        if (tcount < (250 * 10)) {
            *gp_set = 1ul << PICO_DEFAULT_LED_PIN;
        }
        else
        {
            *gp_clr = 1ul << PICO_DEFAULT_LED_PIN;
        }
        //tcount = (tcount + 1) % (500 * 10); // apparently "%" causes a reference to flash
        tcount++;
        if (tcount > 5000)
            tcount = 0;

        if (dac_cycle)
        {
            // kill time to let the FET switch off
            for (int x =0; x < 40; x++) {
                dummy = 0; // pause
            }

            // Update DAC
            //
            *gp_clr = 1ul << PIN_CS;

            *spi0_dr = (uint32_t)dac_buf[3 - dac_idx];
            while (*spi0_sr & SPI_SSPSR_RNE_BITS)
                dummy = *spi0_dr; // dummy read
            while (*spi0_sr & SPI_SSPSR_BSY_BITS)
                dummy = 0; // spin
            while (*spi0_sr & SPI_SSPSR_RNE_BITS)
                dummy = *spi0_dr; // dummy read
            *spi0_icr = SPI_SSPICR_RORIC_BITS;
            
            *gp_set = 1ul << PIN_CS;

            // kill time to let DAC output settle
            for (int x =0; x < 200; x++) {
                dummy = 0; // pause
            }
        }
        else
        {
            // Write LEDs/Read buttons
            //
            button_vector_tmp = 0;

            *gp_set = 1ul << LDPISO; // PISO->1, stop sampling switches, enable shift
            for (int i = 0; i < 24; i++)
            {
                // light led based on level and cycle
                if (led_levels[i] > led_cycle)
                    *gp_set = 1ul << SHDO;
                else
                    *gp_clr = 1ul << SHDO;
                
                // grab switch value
                button_vector_tmp = (button_vector_tmp << 1) | ((*gp_in >> SHDI) & 0x1);
                
                for (int x = 0; x < 10; x++)
                    dummy = 0; // pause
                
                *gp_set = 1ul << SHCLK; // clock->1

                for (int x = 0; x < 10; x++)
                    dummy = 0; // pause

                *gp_clr = 1ul << SHCLK; // clock->0
            }

            *gp_clr = 1ul << LDPISO; // PISO->0, start sampling switches
            
            // Update LEDs with pulse on SIPO
            *gp_set = 1ul << LDSIPO; // SIPO->1
            for (int x = 0; x < 10; x++)
                dummy = 0; // pause
            *gp_clr = 1ul << LDSIPO; // SIPO->0

            button_vector = button_vector_tmp;
            led_cycle = (led_cycle + 1) % 256;
        }
        dac_cycle = !dac_cycle;
    }
}

//void *save_addr;
void core1_entry() {
    //save_addr = (void *) core1_dacLoop;
    core1_dacLoop();
}

uint16_t midi2cv(uint8_t midi) {
    double fcv = 1092.2666667f * ((double) midi);
    uint32_t icv = (uint32_t) fcv;
    return icv & 0xffff;
}

#define CV_SLOP 10
uint8_t cv2midi(uint16_t cv) {
    double fmidi = ((float) cv + CV_SLOP) / 1092.2666667f;
    uint32_t imidi = (uint32_t) fmidi;
    return imidi & 0xff;
}


SequencerState_t sequencerState;
TempState_t tempState;

void calculateTempo() {
    uint32_t period_ms = 60000;
    period_ms = period_ms / (uint32_t) sequencerState.tempo_bpm;
    if (period_ms < 5)
        period_ms = 5;

    uint32_t high_ms = (uint32_t) sequencerState.tempo_duty * period_ms / 100;
    if (high_ms == period_ms)
        high_ms = period_ms - 1;
    if (high_ms == 0)
        high_ms = 1;

    tempState.tempo_high_ms = high_ms;
    tempState.tempo_period_ms = period_ms;
}


void nextLegalNote(int &midi, bool add, QuantMode_t qmode, int qkey) {
    if (qmode == QUANT_CHROMATIC) {
        midi = add ? midi + 1 : midi - 1;
        if (midi > 59)
            midi = 59;
        else if (midi < 0)
            midi = 0;
    } else {
        int note;
        int new_note = midi;
        if (add) {
            note = qkey - 12;
            int idx = 0;
            while (note <= midi) {
                note += (quant_steps[(int) qmode][idx] - '0');
                if (note < 60)
                    new_note = note;
                idx = (idx + 1) % quant_num_steps[(int) qmode];
            }
        } else {
            note = qkey + 60;
            int idx = quant_num_steps[(int) qmode] - 1;
            while (note >= midi) {
                note -= (quant_steps[(int) qmode][idx] - '0');
                if (note >= 0)
                    new_note = note;
                idx--;
                if (idx < 0)
                    idx = quant_num_steps[(int) qmode] - 1;
            }
        }
        midi = new_note;
    }
}

int maxGroupLength(int g) {
    int total = 0;
    for (int i=0; i<MAX_BANKS_IN_GROUP; i++) {
        if (sequencerState.group[g].bank_list[i] < 0)
            break;
        total += sequencerState.bank[sequencerState.group[g].bank_list[i]].length;
    }
    return total;
}

int totalGroupWeight(int g) {
    int total = 0;
    for (int i=0; i<MAX_BANKS_IN_GROUP; i++) {
        if (sequencerState.group[g].bank_list[i] < 0)
            break;
        total += sequencerState.bank[sequencerState.group[g].bank_list[i]].weight;
    }
    return total;
}
 
// update "RUN" and slot LEDs based on current state

int led_time = 0;
int led_sweep = 0;
int led_glow_inc = 1;
uint16_t led_glow = 1;

void showLEDState()
{
    led_time = (led_time + 1) % LED_FLASH_PERIOD;
    led_sweep = (led_sweep + 1) % LED_SWEEP_PERIOD;
    if ((led_glow == (BRT_FULL * 4)) || (led_glow == BRT_OFF))
        led_glow_inc = -led_glow_inc;
    led_glow += led_glow_inc;

    // RUN LED
    if (tempState.running)
    {
        led_levels[LED_RUN] = tempState.clock_in ? BRT_FULL : BRT_DIM;
    }
    else
    {
        led_levels[LED_RUN] = 0;
    }

    // slot LEDs
    int slot_playing[4];
    int abank = tempState.active_bank;
    for (int g = 0; g < 4; g++)
    {
        if (tempState.current_group_ptr[g].bank_idx == abank)
            slot_playing[g] = tempState.current_group_ptr[g].slot_idx;
        else
            slot_playing[g] = -1;
    }

    for (int s = 0; s < 16; s++)
    {
        if (tempState.major_mode == MM_PERFORM)
        {
            if ((slot_playing[0] == s) || (slot_playing[1] == s) || (slot_playing[2] == s) || (slot_playing[3] == s))
                led_levels[NUM2LED(s)] = BRT_FULL;
            else if (sequencerState.bank[abank].length <= s)
                led_levels[NUM2LED(s)] = BRT_OFF;
            else if (sequencerState.bank[abank].slot[s].mute)
                led_levels[NUM2LED(s)] = BRT_OFF;
            else
                led_levels[NUM2LED(s)] = BRT_DIM;
        }
        else if (tempState.major_mode == MM_SLOT_EDIT)
        {
            if (tempState.active_slot == s)
                led_levels[NUM2LED(s)] = led_glow >> 2;
            else if (sequencerState.bank[abank].length <= s)
                led_levels[NUM2LED(s)] = BRT_OFF;
            else
                led_levels[NUM2LED(s)] = sequencerState.bank[abank].slot[s].mute ? BRT_VERY_DIM : BRT_DIM;
        }
        else if (tempState.major_mode == MM_BANK_EDIT)
        {
            if (tempState.active_bank == s)
                led_levels[NUM2LED(s)] = led_glow >> 2;
            else
                led_levels[NUM2LED(s)] = BRT_OFF;
        }
        else if (tempState.major_mode == MM_GROUP_EDIT)
        {
            // deal with bank list editting.
            if (tempState.sub_mode == GROUP_BANK_LIST) {
                if (led_sweep/100 == s)
                    led_levels[NUM2LED(s)] = BRT_HALF;
                else
                    led_levels[NUM2LED(s)] = BRT_OFF;
            } else {
            if (tempState.active_group == s)
                led_levels[NUM2LED(s)] = led_glow >> 2;
            else
                led_levels[NUM2LED(s)] = BRT_OFF;
            }
        }
        else if (tempState.major_mode == MM_GROUP_VIEW)
        {
            switch (sequencerState.group[tempState.active_group].mode) {
                case GM_SEQUENCED:
                case GM_RANDOM_SLOT:
                case GM_TURING_SLOT:
                    if ((tempState.current_group_ptr[tempState.active_group].bank_idx == abank) && (tempState.current_group_ptr[tempState.active_group].slot_idx == s))
                        led_levels[NUM2LED(s)] = BRT_FULL;
                    else if (sequencerState.bank[abank].length <= s)
                        led_levels[NUM2LED(s)] = BRT_OFF;
                    else if (sequencerState.bank[abank].slot[s].mute)
                        led_levels[NUM2LED(s)] = BRT_OFF;
                    else
                        led_levels[NUM2LED(s)] = BRT_DIM;
                    break;
                default:
                    led_levels[NUM2LED(s)] = BRT_OFF;
                    break;
            }
        }
        else
        {
            led_levels[NUM2LED(s)] = BRT_OFF;
        }
    }
}

bool last_clock = false;
absolute_time_t last_clock_time = nil_time;

absolute_time_t gate_end_time[4];
bool gate_end_time_valid[4];

int gate_end_cycles[4];
bool gate_end_cycles_valid[4];

bool gate_triggered[4]; // fire gate next iteration (ms)
bool gate_pass[4]; // use clock for gate

void triggerGate(int group, absolute_time_t now, bool relative_time, uint16_t duration)
{
    if (relative_time)
    {
        if (duration == DURATION_PASS)
        {
            // pass clock pulse through
            gate_pass[group] = true;
        }
        else if (duration < DURATION_1CLOCK)
        {
            // duration based on fractions of a clock period
            int64_t clock_period = absolute_time_diff_us(last_clock_time, now) / 1000;
            int32_t duration_ms = clock_period / duration_divisors[duration];
            if (duration_ms < 10)
                duration_ms = 10;
            gate_end_time_valid[group] = true;
            gate_end_time[group] = make_timeout_time_ms(duration_ms);
        }
        else
        {
            // clock cycle based duration
            gate_end_cycles_valid[group] = true;
            gate_end_cycles[group] = duration - DURATION_1CLOCK + 1;
        }
        gate_triggered[group] = true;
    }
    else if (duration > 0)
    {
        // time based duration
        gate_triggered[group] = true;
        gate_end_time_valid[group] = true;
        gate_end_time[group] = make_timeout_time_ms(duration * 10);
    }
}

void groupNormalOutput(int g, absolute_time_t now)
{
    if ((sequencerState.group[g].bank_list[0] != -1) && (sequencerState.group[g].length != 0))
    {
        int &bidx = tempState.current_group_ptr[g].bank_idx;
        int &sidx = tempState.current_group_ptr[g].slot_idx;
        int &lidx = tempState.current_group_ptr[g].list_idx;
        int &count = tempState.current_group_ptr[g].note_count;

        sidx++;
        count++;
        if ((sidx > 16) || (sidx >= sequencerState.bank[bidx].length))
        {
            sidx = 0;
            lidx++;
            if (lidx < MAX_BANKS_IN_GROUP)
                bidx = sequencerState.group[g].bank_list[lidx];
            else
                bidx = -1;
        }
        if ((bidx < 0) || ((sequencerState.group[g].length != GRP_LENGTH_AUTO) && (count >= sequencerState.group[g].length)))
        {
            sidx = 0;
            count = 0;
            lidx = 0;
            bidx = sequencerState.group[g].bank_list[0];
        }
        if (sequencerState.bank[bidx].relative_time || (sequencerState.bank[bidx].slot[sidx].duration > 0)) {
            dac_buf[g] = sequencerState.bank[bidx].slot[sidx].note;
            tempState.current_group_ptr[g].note = sequencerState.bank[bidx].slot[sidx].note;
        }

        if (!sequencerState.bank[bidx].slot[sidx].mute && !sequencerState.group[g].mute)
        {
            triggerGate(g, now, sequencerState.bank[bidx].relative_time, sequencerState.bank[bidx].slot[sidx].duration);
        }
    }
    else
    {
        // group is disabled
        dac_buf[g] = 0;
        gpio_put(GATEA + g, 0);
    }
}

void groupRandomBankOutput(int g, absolute_time_t now)
{
    if ((sequencerState.group[g].bank_list[0] != -1) && (sequencerState.group[g].length != 0))
    {
        int &bidx = tempState.current_group_ptr[g].bank_idx;
        int &sidx = tempState.current_group_ptr[g].slot_idx;
        int &lidx = tempState.current_group_ptr[g].list_idx;
        int &count = tempState.current_group_ptr[g].note_count;

        sidx++;
        count = 0; // not used in this mode

        if ((sidx > 16) || (sidx >= sequencerState.bank[bidx].length))
        {
            sidx = 0;
            if ((get_rand_32() % 100) < sequencerState.group[g].random_bank_percent) {
                // New bank randomly chosen from list
                int total_weight = totalGroupWeight(g);
                uint32_t r = get_rand_32() % total_weight;
                int running_weight = 0;
                for (int i = 0; i < MAX_BANKS_IN_GROUP; i++) {
                    if (sequencerState.group[g].bank_list[i] < 0) {
                        // should never get here
                        lidx = 0;
                        break;
                    }
                    running_weight += sequencerState.bank[sequencerState.group[g].bank_list[i]].weight;
                    if (r < running_weight) {
                        lidx = i;
                        break;
                    }
                }
                bidx = sequencerState.group[g].bank_list[lidx];
            }
        }

        if (sequencerState.bank[bidx].relative_time || (sequencerState.bank[bidx].slot[sidx].duration > 0)) {
            dac_buf[g] = sequencerState.bank[bidx].slot[sidx].note;
            tempState.current_group_ptr[g].note = sequencerState.bank[bidx].slot[sidx].note;
        }

        if (!sequencerState.bank[bidx].slot[sidx].mute && !sequencerState.group[g].mute)
        {
            triggerGate(g, now, sequencerState.bank[bidx].relative_time, sequencerState.bank[bidx].slot[sidx].duration);
        }
    }
    else
    {
        // group is disabled
        dac_buf[g] = 0;
        gpio_put(GATEA + g, 0);
    }
}

void groupRandomOutput(int group, absolute_time_t now) {

    QuantMode_t qmode = sequencerState.group[group].random_qmode;
    int qkey = sequencerState.group[group].random_qkey;
    if (qmode == QUANT_ACTIVE) {
        qmode = sequencerState.quant_mode;
        qkey = sequencerState.quant_key;
    }

    int range = sequencerState.group[group].random_upper - sequencerState.group[group].random_lower + 1;
    if (range <= 0)
        range = sequencerState.group[group].random_upper;
    
    uint16_t note = (get_rand_32() % range) + sequencerState.group[group].random_lower;

    if (qmode != QUANT_OFF)
        note += 546; // bias up by 1/2 note due to floor funcion of quantization

    if (sequencerState.group[group].mute) {
        // group is disabled
        dac_buf[group] = 0;
        gpio_put(GATEA + group, 0);
        return;
    }

    if (qmode != QUANT_OFF) {
        int midi = cv2midi(note);
        nextLegalNote(midi, true, qmode, qkey);
        nextLegalNote(midi, false, qmode, qkey);
        note = midi2cv(midi);
    }

    dac_buf[group] = note;
    tempState.current_group_ptr[group].note = note;
    triggerGate(group, now, true, sequencerState.group[group].random_duration);
}

void setGroupPtr(int group, int pos) {
    int bank = sequencerState.group[group].bank_list[0];
    int slot = 0;
    int lidx = 0;

    for (int i=0; i<pos; i++) {
        if (slot < (sequencerState.bank[bank].length - 1)) {
            slot++;
        } else if ((lidx < (MAX_BANKS_IN_GROUP-1)) && (sequencerState.group[group].bank_list[lidx+1] != -1)) {
            slot = 0;
            lidx++;
            bank = sequencerState.group[group].bank_list[lidx];
        } else {
            break; // failsafe - shouldn't be needed
        }
    }

    tempState.current_group_ptr[group].bank_idx = bank;
    tempState.current_group_ptr[group].slot_idx = slot;
    tempState.current_group_ptr[group].list_idx = lidx;
    tempState.current_group_ptr[group].note_count = pos;
}

void groupRandomSlotOutput(int group, absolute_time_t now) {

    int length = sequencerState.group[group].length;
    if (length == GRP_LENGTH_AUTO)
        length = maxGroupLength(group);

    if ((length == 0) || (sequencerState.group[group].bank_list[0] == -1)) {
        // group is disabled
        dac_buf[group] = 0;
        gpio_put(GATEA + group, 0);
        return;
    }

    int idx = get_rand_32() % length;
    //printf("random idx: %d\n", idx);
    setGroupPtr(group, idx);

    uint16_t note = sequencerState.bank[tempState.current_group_ptr[group].bank_idx].slot[tempState.current_group_ptr[group].slot_idx].note;

    dac_buf[group] = note;
    tempState.current_group_ptr[group].note = note;

    if (!sequencerState.bank[tempState.current_group_ptr[group].bank_idx].slot[tempState.current_group_ptr[group].slot_idx].mute && !sequencerState.group[group].mute) {
        uint16_t duration = sequencerState.bank[tempState.current_group_ptr[group].bank_idx].slot[tempState.current_group_ptr[group].slot_idx].duration;
        bool relative_time = sequencerState.bank[tempState.current_group_ptr[group].bank_idx].relative_time;
        triggerGate(group, now, relative_time, duration);
    }
}

uint16_t nextTuring(int group) {
        // calculate next bit
    bool bit_out = sequencerState.group[group].turing_state[sequencerState.group[group].turing_size-1];
    bool rand_bit = get_rand_32() & 0x1;
    bool choose_random = !sequencerState.group[group].turing_lock && (get_rand_32() % 65) < tempState.randomness;
    bool force_zero = tempState.turing_zero && (tempState.active_group == group);
    bool force_one = tempState.turing_one && (tempState.active_group == group);

    bool bit_in = force_one || (choose_random ? rand_bit : bit_out) && !force_zero;

    // advance state machine
    for (int i=15; i>0; i--)
        sequencerState.group[group].turing_state[i] = sequencerState.group[group].turing_state[i-1];
    sequencerState.group[group].turing_state[0] = bit_in;

    // vector is always 16 bits
    uint16_t turing_vector = 0;
    for (int i=15; i>0; i--)
        turing_vector = (turing_vector << 1) | (sequencerState.group[group].turing_state[i] & 0x1);
    turing_vector = (turing_vector << 1) | bit_in;

    return turing_vector;
}

void groupTuringOutput(int group, absolute_time_t now) {
    QuantMode_t qmode = sequencerState.group[group].random_qmode;
    int qkey = sequencerState.group[group].random_qkey;
    if (qmode == QUANT_ACTIVE) {
        qmode = sequencerState.quant_mode;
        qkey = sequencerState.quant_key;
    }

    uint16_t turing_vector = nextTuring(group);
    bool pulse = sequencerState.group[group].turing_pulse == GRP_PULSE_OFF;
    pulse = pulse || ((turing_vector >> sequencerState.group[group].turing_pulse) & 0x1);
    turing_vector = turing_vector & 0xff;

    if (sequencerState.group[group].mute) {
        // group is disabled
        dac_buf[group] = 0;
        gpio_put(GATEA + group, 0);
        return;
    }

    // calculate V/OCT
    int32_t range = sequencerState.group[group].random_upper - sequencerState.group[group].random_lower + 1;
    if (range <= 0)
        range = sequencerState.group[group].random_upper;
    int32_t note = (turing_vector & 0xff) * range / 256 + sequencerState.group[group].random_lower + 546;

    // quantize
    if (qmode != QUANT_OFF) {
        int midi = cv2midi(note);
        nextLegalNote(midi, true, qmode, qkey);
        nextLegalNote(midi, false, qmode, qkey);
        note = midi2cv(midi);
    }

    if (sequencerState.group[group].turing_hammer_on) {
        dac_buf[group] = note;
        tempState.current_group_ptr[group].note = note;
    }
 
    if (pulse) {
        // play note
        if (!sequencerState.group[group].turing_hammer_on) {
            dac_buf[group] = note;
            tempState.current_group_ptr[group].note = note;
        }
        triggerGate(group, now, true, sequencerState.group[group].random_duration);
    }
}


void groupTuringSlotOutput(int group, absolute_time_t now) {
    uint16_t turing_vector = nextTuring(group);
    bool pulse = sequencerState.group[group].turing_pulse == GRP_PULSE_OFF;
    pulse = pulse || ((turing_vector >> sequencerState.group[group].turing_pulse) & 0x1);
    turing_vector = turing_vector & 0xff;

    int length = sequencerState.group[group].length;
    if (length == GRP_LENGTH_AUTO)
        length = maxGroupLength(group);

    if ((length == 0) || (sequencerState.group[group].bank_list[0] == -1)) {
        // group is disabled
        dac_buf[group] = 0;
        gpio_put(GATEA + group, 0);
        return;
    }

    setGroupPtr(group, turing_vector % length);
    uint16_t note = sequencerState.bank[tempState.current_group_ptr[group].bank_idx].slot[tempState.current_group_ptr[group].slot_idx].note;

    if (sequencerState.group[group].turing_hammer_on) {
        dac_buf[group] = note;
        tempState.current_group_ptr[group].note = note;
    }
    
    if (pulse && !sequencerState.bank[tempState.current_group_ptr[group].bank_idx].slot[tempState.current_group_ptr[group].slot_idx].mute && !sequencerState.group[group].mute) {
        // play note
        if (!sequencerState.group[group].turing_hammer_on) {
            dac_buf[group] = note;
            tempState.current_group_ptr[group].note = note;
        }
        
        uint16_t duration = sequencerState.bank[tempState.current_group_ptr[group].bank_idx].slot[tempState.current_group_ptr[group].slot_idx].duration;
        bool relative_time = sequencerState.bank[tempState.current_group_ptr[group].bank_idx].relative_time;
        triggerGate(group, now, relative_time, duration);
    }
}


void updateGroups(bool clock)
{
    absolute_time_t now = get_absolute_time();

    if (!tempState.running)
    {
        for (int g = 0; g < 4; g++)
        {
            gpio_put(GATEA + g, 0);
            gate_end_time_valid[g] = false;
            gate_end_cycles_valid[g] = false;
            gate_triggered[g] = false;
        }
    }


    for (int g = 0; g < 4; g++) {
        // set gates from previous ms
        if (gate_triggered[g]) {
            gpio_put(GATEA + g, 1);
            gate_triggered[g] = false;
        }

        // clear gates from previous clock(s)
        if (gate_end_time_valid[g] && (absolute_time_diff_us(gate_end_time[g], now) > 0))
        {
            gpio_put(GATEA + g, 0);
            gate_end_time_valid[g] = false;
        }
        if (gate_end_cycles_valid[g] && clock && !last_clock) {
            gate_end_cycles[g]--;
            if (gate_end_cycles[g] <= 0) {
                gpio_put(GATEA + g, 0);
                gate_end_cycles_valid[g] = false;
            }
        }
        if (gate_pass[g] && !clock) {
            gpio_put(GATEA + g, 0);
            gate_pass[g] = false;
        }
    }

    // calculate new gates and cvs
    if (tempState.running && clock && !last_clock) {
        for (int g = 0; g < 4; g++)
        {
            tempState.current_group_ptr[g].clock_count++;
            if (tempState.current_group_ptr[g].clock_count >= sequencerState.group[g].divide) {
                tempState.current_group_ptr[g].clock_count = 0;
                switch (sequencerState.group[g].mode) {
                    case GM_SEQUENCED:
                        groupNormalOutput(g, now);
                        break;
                    case GM_RANDOM:
                        groupRandomOutput(g, now);
                        break;
                    case GM_RANDOM_SLOT:
                        groupRandomSlotOutput(g, now);
                        break;
                    case GM_RANDOM_BANK:
                        groupRandomBankOutput(g, now);
                        break;
                    case GM_TURING:
                        groupTuringOutput(g, now);
                        break;
                    case GM_TURING_SLOT:
                        groupTuringSlotOutput(g, now);
                }
            }
        }
    }

    if (clock && !last_clock)
        last_clock_time = now;
    last_clock = clock;
}


int tempo_timer = 0;
uint16_t tempo_high_ms_staged = 10;
void updateTempo() {
    if (!sequencerState.tempo_enabled || !tempState.running) {
        tempState.tempo_clock = false;
        tempo_timer = 0;
        return;
    }

    tempo_timer++;
    if (tempo_timer >= tempState.tempo_period_ms) {
        tempo_timer = 0;
        tempo_high_ms_staged = tempState.tempo_high_ms;
        tempState.tempo_clock = true;
    }
    if (tempo_timer >= tempState.tempo_high_ms) {
        tempState.tempo_clock = false;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////
// Main millisecond timer
// Enqueues button presses
// Detects clock ticks and manages sequencing 
//
bool timer_callback (repeating_timer_t *rt) {

    enc.Debounce();
    buttons.Debounce(button_vector);
        int inc = enc.Increment();
    if (inc < 0)
    {
        inQ.Enqueue(uiQueue::MSG_ENC_DEC, 0);
        //puts("Enqueued ENC_DEC message");
    }
    else if (inc > 0)
    {
        inQ.Enqueue(uiQueue::MSG_ENC_INC, 0);
        //puts("Enqueued ENC_INC message");
    }

    bool ebutton = enc.RisingEdge();
    if (ebutton)
    {
        inQ.Enqueue(uiQueue::MSG_ENC_PRESSED, 0);
        //puts("Enqueued ENC_PRESSED message");
    }

    for (int b=0; b<24; b++) {
        if (buttons.FallingEdge(b)) {
            inQ.Enqueue(uiQueue::MSG_BUTTON_RELEASED, b);
            //puts("Enqueued BUTTON_RELEASED message");
        }
        if (buttons.Held(b)) {
            inQ.Enqueue(uiQueue::MSG_BUTTON_HELD, b);
            //puts("Enqueued BUTTON_HELD message");
        }
    }

    updateTempo();
    tempState.clock_in = !gpio_get(NCLOCK) || tempState.tempo_clock;
    updateGroups(tempState.clock_in);
    showLEDState();

    return true;
}

void initIOs() {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    // DAC controls
    for (int dac=DACD; dac<=DACA; dac++) {
        gpio_init(dac);
        gpio_set_dir(dac, GPIO_OUT);
        gpio_put(dac, false);
    }

    // SPI initialisation. This will use SPI at 10MHz.
    spi_init(SPI_PORT, 10*1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    spi_set_format(SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 1000*1000);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Shift register controls
    gpio_init(SHCLK);
    gpio_set_dir(SHCLK, GPIO_OUT);
    gpio_put(SHCLK, 0);

    gpio_init(SHDO);
    gpio_set_dir(SHDO, GPIO_OUT);
    gpio_put(SHDO, 0);

    gpio_init(SHDI);
    gpio_set_dir(SHDI, GPIO_IN);
    
    gpio_init(LDPISO);
    gpio_set_dir(LDPISO, GPIO_OUT);
    gpio_put(LDPISO, 0);

    gpio_init(LDSIPO);
    gpio_set_dir(LDSIPO, GPIO_OUT);
    gpio_put(LDSIPO, 0);

    gpio_init(GATEA);
    gpio_set_dir(GATEA, GPIO_OUT);
    gpio_put(GATEA, 0);

    gpio_init(GATEB);
    gpio_set_dir(GATEB, GPIO_OUT);
    gpio_put(GATEB, 0);

    gpio_init(GATEC);
    gpio_set_dir(GATEC, GPIO_OUT);
    gpio_put(GATEC, 0);

    gpio_init(GATED);
    gpio_set_dir(GATED, GPIO_OUT);
    gpio_put(GATED, 0);

    // Clock input
    gpio_init(NCLOCK);
    gpio_set_dir(NCLOCK, GPIO_IN);

    // RANDOM control
    adc_init();
    gpio_init(RANDOM);
    adc_select_input(RANDOM_ADC);
}

uint32_t wave(int pos, int led) {
    int d = abs(pos - led);
    switch (d) {
        case 0:
            return 256;
        case 1:
            return 64;
        case 2:
            return 16;
        case 3:
            return 8;
        case 4:
            return 2;
        case 5:
            return 1;
        default:
            return 0;
    }
    return 0;
}

void ledTest()
{
    for (int x=-6; x<30; x++) {   
        for (int led = 0; led < 24; led++) {
            led_levels[led] = wave(x, led);
        }
        busy_wait_ms(50);
    }
}

void initGroupPtrs() {
    for (int g=0; g<4; g++) {
        tempState.current_group_ptr[g].list_idx = 0;
        tempState.current_group_ptr[g].bank_idx = sequencerState.group[g].bank_list[0];
        tempState.current_group_ptr[g].slot_idx = 0;
        tempState.current_group_ptr[g].note_count = 0;
        tempState.current_group_ptr[g].clock_count = 0;
        tempState.current_group_ptr[g].note = 0;

    }
}
void initTempState () {
    tempState.running = false;
    tempState.clock_in = false;
    tempState.major_mode = MM_PERFORM;
    tempState.digit_pos = 0;
    tempState.active_bank = 0;
    tempState.active_slot = 0;
    tempState.active_group = 0;
    initGroupPtrs();
    tempState.randomness = 0;
    tempState.tempo_clock = false;
    tempState.turing_one = false;
    tempState.turing_zero = false;
    calculateTempo();
    tempState.mem_page = 0;
    tempState.mem_confirm = false;
    tempState.show_message = false;
    strcpy(tempState.message, "");
}

void factoryInitState() {
    sequencerState.version = SAVE_VERSION;

    for (int b=0; b<16; b++) {
        // init bank
        sequencerState.bank[b].length = 8;
        sequencerState.bank[b].relative_time = true;
        sequencerState.bank[b].default_duration = 0;
        sequencerState.bank[b].weight = 1;

        for (int s=0; s<16; s++) {
            // init slot
            sequencerState.bank[b].slot[s].note = 0;
            sequencerState.bank[b].slot[s].duration = 0;
            sequencerState.bank[b].slot[s].mute = false;
        }
    }
    for (int g=0; g<4; g++) {
        // init group
        sequencerState.group[g].mode = GM_SEQUENCED;
        sequencerState.group[g].length = 8;
        sequencerState.group[g].bank_list[0] = g*4;
        for (int i=1; i<MAX_BANKS_IN_GROUP; i++) {
            sequencerState.group[g].bank_list[i] = -1;
        }
        sequencerState.group[g].divide = 1;
        sequencerState.group[g].mute = false;
        sequencerState.group[g].random_duration = 0;
        sequencerState.group[g].random_lower = 0;
        sequencerState.group[g].random_upper = 0xffff;
        sequencerState.group[g].random_qmode = QUANT_CHROMATIC;
        sequencerState.group[g].random_qkey = 0;
        sequencerState.group[g].random_bank_percent = 100;
        sequencerState.group[g].turing_size = 8;
        sequencerState.group[g].turing_pulse = GRP_PULSE_OFF;
        sequencerState.group[g].turing_hammer_on = false;
        sequencerState.group[g].turing_lock = false;
        for (int i=0; i< 16; i++)
            sequencerState.group[g].turing_state[i] = false;
    }

    sequencerState.tempo_bpm = 60;
    sequencerState.tempo_duty = 50;
    sequencerState.tempo_enabled = false;
    sequencerState.quant_mode = QUANT_CHROMATIC;
    sequencerState.quant_key = 0;
}

// Last 4K sector in 2MB flash
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);
uint8_t write_buffer[FLASH_SECTOR_SIZE];

void restoreState(int page) {
    SequencerState_t *flash_state = (SequencerState_t *) (flash_target_contents + FLASH_SECTOR_SIZE * page); // cast to allow int access

    if (flash_state->version == SAVE_VERSION) {
        //puts("Restoring saved state");
        memcpy((char *) &sequencerState, flash_state, sizeof(SequencerState_t));
    } else {
        // set default
        //puts("Setting factory defaults");
        factoryInitState();
    }
    calculateTempo();
}

void saveState(int page)
{
    //puts("Saving state");

    memcpy(write_buffer, (char *) &sequencerState, sizeof(SequencerState_t));
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE * page, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE * page, write_buffer, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    //puts("Done!");
}

void editNote(uint16_t &value, bool add, QuantMode_t qmode, int qkey) {
    int midi = cv2midi(value);
    nextLegalNote(midi, add, qmode, qkey);
    uint16_t new_value = midi2cv(midi);
    value = new_value;
}

void editHexDigits(uint16_t &value, bool add, int pos) {
    int digit;
    uint16_t new_value;

    digit = (value >> ((3-pos)*4)) & 0xf;
    digit = add ? (digit + 1) : (digit - 1);

    if (digit > 0xf)
        digit -= 0x10;
    else if (digit < 0)
        digit += 0x10;

    new_value = value & ~(0xf << ((3-pos) * 4));
    new_value |= digit << ((3-pos) * 4);
    value = new_value;
}

void editDecDigits(uint16_t &value, bool add, int pos) {
    char sdigits[10];
    int digit; // actually pair of digits
    char cdigit_tens, cdigit_ones;
    uint16_t new_value;

    if (value >= 10000)
        value = 9999;

    sprintf(sdigits, "%4.4hu", value);
    digit = sdigits[tempState.digit_pos*2] - '0';
    digit = digit * 10 + (sdigits[tempState.digit_pos*2+1] - '0');
    digit = add ? (digit + 1) : (digit - 1);

    if (digit >= 100)
        digit -= 100;
    else if (digit < 0)
        digit += 100;
    
    cdigit_tens = '0' + (digit / 10);
    cdigit_ones = '0' + (digit % 10);
    sdigits[tempState.digit_pos*2] = cdigit_tens;
    sdigits[tempState.digit_pos*2 +1] = cdigit_ones;
    sscanf(sdigits, "%hu", &new_value);
    value = new_value;
}

int numBanks(int16_t blist[]) {
    int count = 0;
    for (count=0; count<MAX_BANKS_IN_GROUP; count++)
        if (blist[count] < 0)
            return count;
    return MAX_BANKS_IN_GROUP;
}

void initializeBank(int bank) {
    uint16_t default_note = midi2cv(sequencerState.quant_key);
    for (int i=0; i<16; i++) {
        sequencerState.bank[bank].slot[i].duration = sequencerState.bank[bank].default_duration;
        sequencerState.bank[bank].slot[i].note = default_note;
        sequencerState.bank[bank].slot[i].mute = false;
    }
}

void clearModeLeds() {
    for (int i=0; i<NUM_MODE_LEDS; i++)
        led_levels[modeleds[i]] = BRT_OFF;
}

// Respond to queued events
//
void getControls() {
    bool update = false;
    int temp_bank;

    if (!inQ.Empty())
    {
        uiQueue::uiEntry entry = inQ.Dequeue();
        // printf("not empty! msg=%d\n", msg);
        update = true;
        int number = BUTTON2NUM(entry.data);

        switch (entry.msg)
        {
            case uiQueue::MSG_ENC_DEC:
                switch (tempState.major_mode) {
                    case MM_PERFORM:
                        temp_bank = tempState.active_bank - 1;
                        if (temp_bank < 0)
                            temp_bank += 16;
                        tempState.active_bank = temp_bank;
                        break;

                    case MM_MEM:
                        if ((tempState.digit_pos == 0) && (tempState.mem_page > 0))
                            tempState.mem_page--;
                        else if (tempState.digit_pos == 1)
                            tempState.mem_confirm = !tempState.mem_confirm;
                        break;

                    case MM_TEMPO:
                        if (tempState.sub_mode == TEMPO_BPM_EDIT) {
                            if (sequencerState.tempo_enabled && (sequencerState.tempo_bpm > 1))
                                sequencerState.tempo_bpm--;
                        } else {
                            if (sequencerState.tempo_duty > 1)
                                sequencerState.tempo_duty--;
                        }
                        calculateTempo();
                        break;

                    case MM_QUANT:
                        if (tempState.digit_pos == 0) {
                            if (sequencerState.quant_key > 0)
                                sequencerState.quant_key--;
                        } else {
                            if (sequencerState.quant_mode != QUANT_FIRST)
                                sequencerState.quant_mode = (QuantMode_t) (sequencerState.quant_mode - 1);
                        }
                        break;

                    case MM_SLOT_EDIT:
                        if (tempState.sub_mode == SLOT_NOTE_EDIT) {
                            if (tempState.digit_pos == 0)
                                editNote(sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].note, false, sequencerState.quant_mode, sequencerState.quant_key);
                            else
                                 editHexDigits(sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].note, false, tempState.digit_pos-1);
                        }
                        else {
                            if (sequencerState.bank[tempState.active_bank].relative_time) {
                                if (sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration > 0)
                                    sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration--;
                            } else {
                                editDecDigits(sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration, false, tempState.digit_pos);
                            }
                        }
                        break;

                    case MM_BANK_EDIT:
                        switch (tempState.sub_mode) {
                            case BANK_LENGTH_EDIT:
                                if (sequencerState.bank[tempState.active_bank].length > 1)
                                    sequencerState.bank[tempState.active_bank].length--;
                                break;
                            case BANK_RELATIVE_TIME_EDIT:
                                sequencerState.bank[tempState.active_bank].relative_time = !sequencerState.bank[tempState.active_bank].relative_time;
                                break;
                            case BANK_DEFAULT_DURATION_EDIT:
                                if (sequencerState.bank[tempState.active_bank].relative_time) {
                                    if (sequencerState.bank[tempState.active_bank].default_duration > 0)
                                        sequencerState.bank[tempState.active_bank].default_duration--;
                                } else {
                                    editDecDigits(sequencerState.bank[tempState.active_bank].default_duration, false, tempState.digit_pos);
                                }
                                break;
                            case BANK_WEIGHT_EDIT:
                                if (sequencerState.bank[tempState.active_bank].weight > 1)
                                    sequencerState.bank[tempState.active_bank].weight--;
                                break;
                        }
                        break;

                    case MM_GROUP_EDIT:
                        switch (tempState.sub_mode) {
                            case GROUP_MODE_EDIT:
                                if (sequencerState.group[tempState.active_group].mode > GM_FIRST)
                                    sequencerState.group[tempState.active_group].mode = (GroupMode_t) (sequencerState.group[tempState.active_group].mode - 1);
                                break;
                            case GROUP_RANDOM_DURATION:
                                if (sequencerState.group[tempState.active_group].random_duration > 0)
                                        sequencerState.group[tempState.active_group].random_duration--;
                                break;
                            case GROUP_RANDOM_LOWER:
                                if (tempState.digit_pos == 0)
                                    editNote(sequencerState.group[tempState.active_group].random_lower, false, sequencerState.group[tempState.active_group].random_qmode, sequencerState.group[tempState.active_group].random_qkey);
                                else
                                    editHexDigits(sequencerState.group[tempState.active_group].random_lower, false, tempState.digit_pos-1);
                                break;
                            case GROUP_RANDOM_UPPER:
                                if (tempState.digit_pos == 0)
                                    editNote(sequencerState.group[tempState.active_group].random_upper, false, sequencerState.group[tempState.active_group].random_qmode, sequencerState.group[tempState.active_group].random_qkey);
                                else
                                    editHexDigits(sequencerState.group[tempState.active_group].random_upper, false, tempState.digit_pos-1);
                                break;
                            case GROUP_RANDOM_QUANT:
                                if (tempState.digit_pos == 0) {
                                    if (sequencerState.group[tempState.active_group].random_qkey > 0)
                                        sequencerState.group[tempState.active_group].random_qkey--;
                                } else {
                                    if (sequencerState.group[tempState.active_group].random_qmode != QUANT_OFF)
                                        sequencerState.group[tempState.active_group].random_qmode = (QuantMode_t) (sequencerState.group[tempState.active_group].random_qmode - 1);
                                }
                                break;
                            case GROUP_TURING_SIZE:
                                if (sequencerState.group[tempState.active_group].turing_size > 2)
                                    sequencerState.group[tempState.active_group].turing_size--;
                                break;
                            case GROUP_TURING_PULSE:
                                if (sequencerState.group[tempState.active_group].turing_pulse >= 0)
                                    sequencerState.group[tempState.active_group].turing_pulse--;
                                break;
                            case GROUP_TURING_HAMMER:
                                sequencerState.group[tempState.active_group].turing_hammer_on = !sequencerState.group[tempState.active_group].turing_hammer_on;
                                break;
                            case GROUP_LENGTH_EDIT:
                                if (sequencerState.group[tempState.active_group].length > GRP_LENGTH_AUTO)
                                    sequencerState.group[tempState.active_group].length--;
                                break;
                            case GROUP_BANK_LIST:
                                if (tempState.digit_pos > 0)
                                    tempState.digit_pos--;
                                break;
                            case GROUP_RANDOM_BANK_PERCENT:
                                if (sequencerState.group[tempState.active_group].random_bank_percent > 1)
                                    sequencerState.group[tempState.active_group].random_bank_percent--;
                                break;
                            case GROUP_DIVIDER:
                                if (sequencerState.group[tempState.active_group].divide > 1)
                                    sequencerState.group[tempState.active_group].divide--;
                                break;
                        }
                        break;
                        case MM_GROUP_VIEW:
                            switch (sequencerState.group[tempState.active_group].mode) {
                                case GM_SEQUENCED:
                                case GM_RANDOM_SLOT:
                                    temp_bank = tempState.active_bank - 1;
                                    if (temp_bank < 0)
                                        temp_bank += 16;
                                    tempState.active_bank = temp_bank;
                                    break;
                                case GM_TURING:
                                case GM_TURING_SLOT:
                                    if (tempState.turing_one) {
                                        tempState.turing_one = false;
                                        tempState.turing_zero = false;
                                    } else {
                                        tempState.turing_one = false;
                                        tempState.turing_zero = true;
                                    }
                                    break;
                            }
                            break;
                }
                break;
            case uiQueue::MSG_ENC_INC:
                switch (tempState.major_mode) {
                    case MM_PERFORM:
                        tempState.active_bank = (tempState.active_bank + 1) % 16;
                        break;

                    case MM_MEM:
                        if ((tempState.digit_pos == 0) && (tempState.mem_page < 7))
                            tempState.mem_page++;
                        else if (tempState.digit_pos == 1)
                            tempState.mem_confirm = !tempState.mem_confirm;
                        break;

                    case MM_TEMPO:
                        if (tempState.sub_mode == TEMPO_BPM_EDIT) {
                            if (sequencerState.tempo_enabled && (sequencerState.tempo_bpm < 998))
                                sequencerState.tempo_bpm++;
                        } else {
                            if (sequencerState.tempo_duty < 99)
                                sequencerState.tempo_duty++;
                        }
                        calculateTempo();
                        break;

                    case MM_QUANT:
                        if (tempState.digit_pos == 0) {
                            if (sequencerState.quant_key < 11)
                                    sequencerState.quant_key++;
                        } else {
                            if (sequencerState.quant_mode != QUANT_LAST)
                                    sequencerState.quant_mode = (QuantMode_t) (sequencerState.quant_mode + 1);
                        }
                        break;
                        
                    case MM_SLOT_EDIT:
                        if (tempState.sub_mode == SLOT_NOTE_EDIT) {
                            if (tempState.digit_pos == 0)
                                editNote(sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].note, true, sequencerState.quant_mode, sequencerState.quant_key);
                            else
                                 editHexDigits(sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].note, true, tempState.digit_pos-1);
                        }
                        else {
                            if (sequencerState.bank[tempState.active_bank].relative_time) {
                                if (sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration < DURATION_MAX)
                                    sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration++;
                            } else {
                                editDecDigits(sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration, true, tempState.digit_pos);
                            }
                        }
                        break;

                    case MM_BANK_EDIT:
                        switch (tempState.sub_mode) {
                            case BANK_LENGTH_EDIT:
                                if (sequencerState.bank[tempState.active_bank].length < 16)
                                    sequencerState.bank[tempState.active_bank].length++;
                                break;
                            case BANK_RELATIVE_TIME_EDIT:
                                sequencerState.bank[tempState.active_bank].relative_time = !sequencerState.bank[tempState.active_bank].relative_time;
                                break;
                            case BANK_DEFAULT_DURATION_EDIT:
                                if (sequencerState.bank[tempState.active_bank].relative_time) {
                                    if (sequencerState.bank[tempState.active_bank].default_duration < DURATION_MAX)
                                        sequencerState.bank[tempState.active_bank].default_duration++;
                                } else {
                                    editDecDigits(sequencerState.bank[tempState.active_bank].default_duration, true, tempState.digit_pos);
                                }
                                break;
                            case BANK_WEIGHT_EDIT:
                                if (sequencerState.bank[tempState.active_bank].weight < 99)
                                    sequencerState.bank[tempState.active_bank].weight++;
                                break;
                        }
                        break;

                   case MM_GROUP_EDIT:
                        switch (tempState.sub_mode) {
                            case GROUP_MODE_EDIT:
                                if (sequencerState.group[tempState.active_group].mode < GM_LAST)
                                    sequencerState.group[tempState.active_group].mode = (GroupMode_t) (sequencerState.group[tempState.active_group].mode + 1);
                                break;
                            case GROUP_RANDOM_DURATION:
                                if (sequencerState.group[tempState.active_group].random_duration < DURATION_MAX)
                                        sequencerState.group[tempState.active_group].random_duration++;
                                break;
                            case GROUP_RANDOM_LOWER:
                                if (tempState.digit_pos == 0)
                                    editNote(sequencerState.group[tempState.active_group].random_lower, true, sequencerState.group[tempState.active_group].random_qmode, sequencerState.group[tempState.active_group].random_qkey);
                                else
                                    editHexDigits(sequencerState.group[tempState.active_group].random_lower, true, tempState.digit_pos-1);
                                break;
                            case GROUP_RANDOM_UPPER:
                                if (tempState.digit_pos == 0)
                                    editNote(sequencerState.group[tempState.active_group].random_upper, true, sequencerState.group[tempState.active_group].random_qmode, sequencerState.group[tempState.active_group].random_qkey);
                                else
                                    editHexDigits(sequencerState.group[tempState.active_group].random_upper, true, tempState.digit_pos-1);
                                break;
                            case GROUP_RANDOM_QUANT:
                                if (tempState.digit_pos == 0) {
                                    if (sequencerState.group[tempState.active_group].random_qkey < 11)
                                        sequencerState.group[tempState.active_group].random_qkey++;
                                } else {
                                    if (sequencerState.group[tempState.active_group].random_qmode != QUANT_LAST)
                                        sequencerState.group[tempState.active_group].random_qmode = (QuantMode_t) (sequencerState.group[tempState.active_group].random_qmode + 1);
                                }
                                break;
                            case GROUP_TURING_SIZE:
                                if (sequencerState.group[tempState.active_group].turing_size < 16)
                                    sequencerState.group[tempState.active_group].turing_size++;
                                break;
                            case GROUP_TURING_PULSE:
                                if (sequencerState.group[tempState.active_group].turing_pulse < 15)
                                    sequencerState.group[tempState.active_group].turing_pulse++;
                                break;
                            case GROUP_TURING_HAMMER:
                                sequencerState.group[tempState.active_group].turing_hammer_on = !sequencerState.group[tempState.active_group].turing_hammer_on;
                                break;
                            case GROUP_LENGTH_EDIT:
                                if (sequencerState.group[tempState.active_group].length < maxGroupLength(tempState.active_group))
                                    sequencerState.group[tempState.active_group].length++;
                                break;
                            case GROUP_BANK_LIST:
                                if (tempState.digit_pos < numBanks(sequencerState.group[tempState.active_group].bank_list))
                                    tempState.digit_pos++;
                                break;
                            case GROUP_RANDOM_BANK_PERCENT:
                                if (sequencerState.group[tempState.active_group].random_bank_percent < 100)
                                    sequencerState.group[tempState.active_group].random_bank_percent++;
                                break;
                            case GROUP_DIVIDER:
                                if (sequencerState.group[tempState.active_group].divide < 99)
                                    sequencerState.group[tempState.active_group].divide++;
                                break;
                        }
                        break;
                        
                        case MM_GROUP_VIEW:
                            switch (sequencerState.group[tempState.active_group].mode) {
                                case GM_SEQUENCED:
                                    tempState.active_bank = (tempState.active_bank + 1) % 16;
                                    break;
                                case GM_TURING:
                                case GM_TURING_SLOT:
                                    if (tempState.turing_zero) {
                                        tempState.turing_one = false;
                                        tempState.turing_zero = false;
                                    } else {
                                        tempState.turing_one = true;
                                        tempState.turing_zero = false;
                                    }
                                    break;
                            }
                            break;
                }
                break;
            case uiQueue::MSG_ENC_PRESSED:
                switch (tempState.major_mode) {
                    case MM_QUANT:
                        tempState.digit_pos = (tempState.digit_pos + 1) % 2;
                        break;
                    
                    case MM_MEM:
                        if (tempState.digit_pos == 1) {
                            if (tempState.mem_confirm) {
                                if (tempState.sub_mode == MEM_READ) {
                                    restoreState(tempState.mem_page);
                                    strcpy(tempState.message, "Read complete");
                                }
                                else {
                                    saveState(tempState.mem_page);
                                    strcpy(tempState.message, "Write complete");
                                }
                                tempState.show_message = true;
                                tempState.mem_confirm = false;
                            }
                        }
                        tempState.digit_pos = (tempState.digit_pos + 1) % 2;
                        break;

                    case MM_TEMPO:
                        if (tempState.sub_mode == TEMPO_BPM_EDIT)
                            sequencerState.tempo_enabled = !sequencerState.tempo_enabled;
                        calculateTempo();
                        break;
                    
                    case MM_SLOT_EDIT:
                        if (tempState.sub_mode == SLOT_NOTE_EDIT)
                            tempState.digit_pos = (tempState.digit_pos + 1) % 5;
                        else
                            tempState.digit_pos = (tempState.digit_pos + 1) % 2;
                        break;
                    
                    case MM_BANK_EDIT:
                        if (tempState.sub_mode == BANK_DEFAULT_DURATION_EDIT)
                            tempState.digit_pos = (tempState.digit_pos + 1) % 2;
                        break;

                    case MM_GROUP_EDIT:
                        switch (tempState.sub_mode) {
                            case GROUP_RANDOM_DURATION:
                            case GROUP_RANDOM_QUANT:
                                tempState.digit_pos = (tempState.digit_pos + 1) % 2;
                                break;
                            case GROUP_RANDOM_LOWER:
                            case GROUP_RANDOM_UPPER:
                                tempState.digit_pos = (tempState.digit_pos + 1) % 5;
                                break;
                            case GROUP_BANK_LIST:
                                if ((tempState.digit_pos >= 0) && (tempState.digit_pos <= 15)) {
                                    sequencerState.group[tempState.active_group].bank_list[tempState.digit_pos] = -1;
                                    if (tempState.digit_pos > 0)
                                        tempState.digit_pos--;
                                }
                                break;
                        }
                        break;

                    case MM_GROUP_VIEW:
                        switch (sequencerState.group[tempState.active_group].mode) {
                            case GM_TURING:
                            case GM_TURING_SLOT:
                                sequencerState.group[tempState.active_group].turing_lock = !sequencerState.group[tempState.active_group].turing_lock;
                            break;
                        }
                        break;
                }
                break;

            case uiQueue::MSG_BUTTON_RELEASED:
                switch (entry.data)
                {
                case BUTTON_RUN:
                    tempState.running = !tempState.running;
                    break;

                case BUTTON_MEM:
                    tempState.digit_pos = 0;
                    tempState.mem_confirm = false;
                    if (tempState.major_mode == MM_MEM) {
                        if (tempState.sub_mode == MEM_READ)
                            tempState.sub_mode = MEM_WRITE;
                        else
                            tempState.sub_mode = MEM_READ;
                    } else {
                        tempState.major_mode = MM_MEM;
                        tempState.sub_mode = MEM_READ;
                        clearModeLeds();
                        led_levels[LED_MEM] = BRT_HALF;
                    }
                    break;

                case BUTTON_TEMPO:
                    if (tempState.major_mode == MM_TEMPO) {
                        if (tempState.sub_mode == TEMPO_BPM_EDIT)
                            tempState.sub_mode = TEMPO_DUTY_EDIT;
                        else
                            tempState.sub_mode = TEMPO_BPM_EDIT;
                    } else {
                        tempState.major_mode = MM_TEMPO;
                        tempState.sub_mode = TEMPO_BPM_EDIT;
                        clearModeLeds();
                        led_levels[LED_TEMPO] = BRT_HALF;
                    }
                    break;

                case BUTTON_QUANT:
                    tempState.major_mode = MM_QUANT;
                    clearModeLeds();
                    led_levels[LED_QUANT] = BRT_HALF;
                    tempState.digit_pos = 1;
                    break;

                case BUTTON_SLOT:
                    if (tempState.major_mode == MM_SLOT_EDIT)
                    {
                        if (tempState.sub_mode == SLOT_NOTE_EDIT)
                            tempState.sub_mode = SLOT_DURATION_EDIT;
                        else
                            tempState.sub_mode = SLOT_NOTE_EDIT;
                    }
                    else {
                        clearModeLeds();
                        tempState.major_mode = MM_SLOT_EDIT;
                        tempState.sub_mode = SLOT_NOTE_EDIT;
                        led_levels[LED_SLOT] = BRT_HALF;
                    }
                    tempState.digit_pos = 0;
                    break;

                case BUTTON_BANK:
                    if (tempState.major_mode == MM_BANK_EDIT) {
                        switch (tempState.sub_mode) {
                            case BANK_LENGTH_EDIT:
                                tempState.sub_mode = BANK_RELATIVE_TIME_EDIT;
                                break;
                            case BANK_RELATIVE_TIME_EDIT:
                                tempState.sub_mode = BANK_DEFAULT_DURATION_EDIT;
                                break;
                            case BANK_DEFAULT_DURATION_EDIT:
                                tempState.sub_mode = BANK_WEIGHT_EDIT;
                                break;
                            case BANK_WEIGHT_EDIT:
                                tempState.sub_mode = BANK_LENGTH_EDIT;
                                break;
                        }
                    } else {
                        tempState.major_mode = MM_BANK_EDIT;
                        tempState.sub_mode = BANK_LENGTH_EDIT;
                        clearModeLeds();
                        led_levels[LED_BANK] = BRT_HALF;
                    }
                    break;

                case BUTTON_GEDIT:
                    tempState.digit_pos = 0;
                    if (tempState.major_mode == MM_GROUP_EDIT) {
                        switch (tempState.sub_mode) {
                            case GROUP_MODE_EDIT:
                                switch (sequencerState.group[tempState.active_group].mode) {
                                    case GM_SEQUENCED:
                                    case GM_RANDOM_SLOT:
                                        tempState.sub_mode = GROUP_LENGTH_EDIT;
                                        break;
                                    case GM_RANDOM_BANK:
                                        tempState.sub_mode = GROUP_RANDOM_BANK_PERCENT;
                                        break;
                                    case GM_TURING:
                                    case GM_TURING_SLOT:
                                        tempState.sub_mode = GROUP_TURING_SIZE;
                                        break;
                                    default:
                                        tempState.sub_mode = GROUP_RANDOM_DURATION;
                                        break;
                                }
                                break;
                            case GROUP_LENGTH_EDIT:
                                tempState.sub_mode = GROUP_BANK_LIST;
                                break;
                            case GROUP_RANDOM_BANK_PERCENT:
                                tempState.sub_mode = GROUP_BANK_LIST;
                                break;
                            case GROUP_BANK_LIST:
                                tempState.sub_mode = GROUP_DIVIDER;
                                break;
                            case GROUP_TURING_SIZE:
                                tempState.sub_mode = GROUP_TURING_PULSE;
                                break;
                            case GROUP_TURING_PULSE:
                                tempState.sub_mode = GROUP_TURING_HAMMER;
                                break;
                            case GROUP_TURING_HAMMER:
                                if (sequencerState.group[tempState.active_group].mode == GM_TURING_SLOT)
                                    tempState.sub_mode = GROUP_LENGTH_EDIT;
                                else
                                    tempState.sub_mode = GROUP_RANDOM_DURATION;
                                break;
                            case GROUP_RANDOM_DURATION:
                                tempState.sub_mode = GROUP_RANDOM_QUANT;
                                tempState.digit_pos = 1;
                                break;
                            case GROUP_RANDOM_QUANT:
                                tempState.sub_mode = GROUP_RANDOM_LOWER;
                                break;
                            case GROUP_RANDOM_LOWER:
                                tempState.sub_mode = GROUP_RANDOM_UPPER;
                                break;
                            case GROUP_RANDOM_UPPER:
                                tempState.sub_mode = GROUP_DIVIDER;
                                break;
                            case GROUP_DIVIDER:
                                tempState.sub_mode = GROUP_MODE_EDIT;
                                break;
                        }
                    } else {
                        tempState.major_mode = MM_GROUP_EDIT;
                        tempState.sub_mode = GROUP_MODE_EDIT;
                        clearModeLeds();
                        led_levels[LED_GEDIT] = BRT_HALF;
                    }
                    break;

                case BUTTON_GVIEW:
                    tempState.major_mode = MM_GROUP_VIEW;
                    clearModeLeds();
                    led_levels[LED_GVIEW] = BRT_HALF;
                    break;

                default:
                    if (entry.data >= BUTTON_FIRST_NUM)
                    {
                        // number was pressed
                        switch (tempState.major_mode) {
                            case MM_PERFORM:
                                sequencerState.bank[tempState.active_bank].slot[number].mute = !sequencerState.bank[tempState.active_bank].slot[number].mute;
                                break;

                            case MM_SLOT_EDIT:
                                tempState.active_slot = number;
                                break;
                            
                            case MM_BANK_EDIT:
                                tempState.active_bank = number;
                                break;

                            case MM_GROUP_EDIT:
                                if (tempState.sub_mode == GROUP_BANK_LIST) {
                                    if ((tempState.digit_pos >= 0) && (tempState.digit_pos <= 15))
                                        sequencerState.group[tempState.active_group].bank_list[tempState.digit_pos] = number;
                                        if (tempState.digit_pos < numBanks(sequencerState.group[tempState.active_group].bank_list))
                                            tempState.digit_pos++;
                                }
                                else
                                    tempState.active_group = number % 4;
                                break;
                            case MM_GROUP_VIEW:
                                switch (sequencerState.group[tempState.active_group].mode) {
                                    case GM_SEQUENCED:
                                    case GM_RANDOM_SLOT:
                                    case GM_TURING_SLOT:
                                        sequencerState.bank[tempState.active_bank].slot[number].mute = !sequencerState.bank[tempState.active_bank].slot[number].mute;
                                        break;
                                }
                                break;
                        }
                    }
                }
                break;

            case uiQueue::MSG_BUTTON_HELD:
                switch (entry.data) {
                    case BUTTON_RUN:
                        initGroupPtrs();
                        break;
                    default:
                        if (entry.data >= BUTTON_FIRST_NUM) {
                            switch (tempState.major_mode) {
                                case MM_SLOT_EDIT: 
                                    tempState.active_slot = number;
                                    sequencerState.bank[tempState.active_bank].slot[number].mute = !sequencerState.bank[tempState.active_bank].slot[number].mute;
                                    if (sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].mute)
                                        strcpy(tempState.message, "Slot muted");
                                    else
                                        strcpy(tempState.message, "Slot unmuted");
                                    tempState.show_message = true;
                                    break;
                                case MM_BANK_EDIT:
                                    tempState.active_bank = number;
                                    initializeBank(number);
                                    strcpy(tempState.message, "Bank cleared");
                                    tempState.show_message = true;
                                    break;
                                case MM_GROUP_EDIT:
                                    tempState.active_group = number % 4;
                                    sequencerState.group[number % 4].mute = !sequencerState.group[number % 4].mute;
                                    if (sequencerState.group[tempState.active_group].mute)
                                        strcpy(tempState.message, "Group muted");
                                    else
                                        strcpy(tempState.message, "Group unmuted");
                                    tempState.show_message = true;
                                    break;
                            }
                        } else {
                            tempState.major_mode = MM_PERFORM;
                            clearModeLeds();
                        }
                }
                break;
        }
    }
}

void showGroupPtrs(SSD1306 &display) {
    char sbuf[20];
    drawText(&display, font_8x8, "    b  s", 0, 14, WriteMode::ADD);

    for (int g=0; g<4; g++) {
        if ((sequencerState.group[g].bank_list[0] >= 0) && (sequencerState.group[g].length != 0))
            sprintf(sbuf, "%c: %2d %2d", ('A' + g), tempState.current_group_ptr[g].bank_idx, tempState.current_group_ptr[g].slot_idx);
        else
            sprintf(sbuf, "%c: ------", ('A' + g));  
        drawText(&display, font_8x8, sbuf, 0, 10*g+24, WriteMode::ADD);
    }
}

void showMem(SSD1306 &display) {
    char sbuf[20];

    if (tempState.sub_mode == MEM_READ)
        drawText(&display, font_8x8, "Memory Read", 0, 0, WriteMode::ADD);
    else
        drawText(&display, font_8x8, "Memory Write", 0, 0, WriteMode::ADD);

    sprintf(sbuf, "Page: %1d", tempState.mem_page);
    drawText(&display, font_12x16, sbuf, 0, 15, WriteMode::ADD);
    sprintf(sbuf, "Confirm? %c", tempState.mem_confirm ? 'Y' : 'N');
    drawText(&display, font_12x16, sbuf, 0, 40, WriteMode::ADD);

    if (tempState.digit_pos == 0) {
        drawLine(&display, 12*6, 15+16, 12*7-1, 15+16, WriteMode::ADD);
    } else {
        drawLine(&display, 12*9, 40+16, 12*10-1, 40+16);
    }
}

void note2Str(char *buf, uint8_t midi) {
    int key = midi % 12;
    int octave = midi / 12;
    sprintf(buf, "%s%d", key_names[key], octave);
}

void showTempo(SSD1306 &display) {
    char sbuf[20];
    int len = 1;

    drawText(&display, font_8x8, "Internal Tempo", 0, 0, WriteMode::ADD);
    if (tempState.sub_mode == TEMPO_BPM_EDIT)
    {
        if (!sequencerState.tempo_enabled)
        {
            sprintf(sbuf, "BPM: OFF");
            len = 3;
        }
        else
        {
            sprintf(sbuf, "BPM: %3d", sequencerState.tempo_bpm);

            if (sequencerState.tempo_bpm > 99)
                len = 3;
            else if (sequencerState.tempo_bpm > 9)
                len = 2;
        }
        drawText(&display, font_12x16, sbuf, 0, 15, WriteMode::ADD);
        drawLine(&display, (8 - len) * 12, 31, 8 * 12 - 1, 31);
    } else {
        sprintf(sbuf, "Duty: %2d%%", sequencerState.tempo_duty);
        drawText(&display, font_12x16, sbuf, 0, 15, WriteMode::ADD);
        drawLine(&display, 6*12, 31, 8*12-1, 31);
    }
}

void showEditQuant(SSD1306 &display, int qkey, int qmode, int y) {
    char sbuf[20];
    sprintf(sbuf, "%2s %s", key_names[qkey], quant_names[qmode]);
    drawText(&display, font_8x8, sbuf, 0, y, WriteMode::ADD);

    // draw line under field to edit
    if (tempState.digit_pos == 0) {
        drawLine(&display, 0, y+8, 15, y+8, WriteMode::ADD);
    } else {
        int numchars = strlen(quant_names[qmode]);
        drawLine(&display, 24, y+8, 23 + numchars * 8, y+8, WriteMode::ADD);
    }
}

void showEditNote(SSD1306 &display, uint16_t note, int y)
{
    char sbuf[31];
    char note_buf[10];
    
    display.addBitmapImage(0, y, 16, 16, note_bitmap);
    uint8_t midi = cv2midi(note);
    note2Str(note_buf, midi);
    sprintf(sbuf, "   %3s", note_buf);
    drawText(&display, font_12x16, sbuf, 0, y, WriteMode::ADD);
    sprintf(sbuf, "%2.2u", midi);
    drawText(&display, font_8x8, sbuf, 96, y+3, WriteMode::ADD);
    sprintf(sbuf, "CV %4.4hX", note);
    drawText(&display, font_12x16, sbuf, 0, y+20, WriteMode::ADD);

    // draw line under field to edit
    if (tempState.digit_pos == 0)
    {
        // editing midi
        drawLine(&display, 36, y + 16, 71, y + 16);
    }
    else
    {
        // editing cv
        drawLine(&display, 36 + 12 * (tempState.digit_pos - 1), y + 20 + 16, 35 + 12 * tempState.digit_pos, y + 20 + 16);
    }
}

void showDuration(SSD1306 &display, uint16_t duration, int y)
{
    char sbuf[31];
    if (sequencerState.bank[tempState.active_bank].relative_time)
    {
        // relative time
        int len;
        drawText(&display, font_8x8, "Clocks:", 0, y, WriteMode::ADD);
        if (duration == DURATION_PASS)
        {
            sprintf(sbuf, "Pass-Thru");
            len = 9;
        }
        else if (duration > DURATION_1CLOCK)
        {
            sprintf(sbuf, "%d cycles", duration - DURATION_1CLOCK + 1);
            len = (duration >= DURATION_2DIGIT) ? 2 : 1;
        }
        else if (duration == DURATION_1CLOCK)
        {
            sprintf(sbuf, "1 cycle");
            len = 1;
        }
        else
        {
            sprintf(sbuf, "%s cycle", duration_names[duration]);
            len = strlen(duration_names[duration]);
        }
        drawText(&display, font_12x16, sbuf, 0, y+10, WriteMode::ADD);
        // draw line under field to edit
        drawLine(&display, 0, y + 10 + 16, 12 * len - 1, y + 10 + 16, WriteMode::ADD);
    }
    else
    {
        // absolute time
        drawText(&display, font_8x8, "Time:", 0, y, WriteMode::ADD);
        sprintf(sbuf, "%2d.%2.2d sec", duration / 100, duration % 100);
        drawText(&display, font_12x16, sbuf, 0, y+10, WriteMode::ADD);
        // draw line under field to edit
        if (tempState.digit_pos == 0)
            drawLine(&display, 0, y + 10 + 16, 12 * 2 - 1, y + 10 + 16);
        else
            drawLine(&display, 12 * 3, y + 10 + 16, 12 * 5 - 1, y + 10 + 16);
    }
}

void showSlot(SSD1306 &display) {
    char sbuf[31];

    if (sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].mute) {
        drawText(&display, font_8x8, "MUTE", 95, 0, WriteMode::ADD);
    }

    if (tempState.sub_mode == SLOT_NOTE_EDIT) {
        drawText(&display, font_8x8, "Slot note", 0, 10, WriteMode::ADD);
        showEditNote(display, sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].note, 27);
    } else {
        drawText(&display, font_8x8, "Slot duration", 0, 10, WriteMode::ADD);
        showDuration(display, sequencerState.bank[tempState.active_bank].slot[tempState.active_slot].duration, 35);
    }
}

void showBank(SSD1306 &display) {
    char sbuf[20];

    switch (tempState.sub_mode) {
        case BANK_LENGTH_EDIT:
            drawText(&display, font_8x8, "Bank length", 0, 10, WriteMode::ADD);
            sprintf(sbuf, "%2.2d", sequencerState.bank[tempState.active_bank].length);
            drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
            drawLine(&display, 0, 25+16, 2*12-1, 25+16);
            break;

        case BANK_RELATIVE_TIME_EDIT:
            drawText(&display, font_8x8, "Timing basis", 0, 10, WriteMode::ADD);
            if (sequencerState.bank[tempState.active_bank].relative_time) {
                drawText(&display, font_12x16, "Clocks", 0, 35, WriteMode::ADD);
                drawLine(&display, 0, 35+16, 6*12-1, 35+16);
            } else {
                drawText(&display, font_12x16, "Time", 0, 35, WriteMode::ADD);
                drawLine(&display, 0, 35+16, 4*12-1, 35+16);
            }
            break;
            
        case BANK_DEFAULT_DURATION_EDIT:
            drawText(&display, font_8x8, "Default duration", 0, 10, WriteMode::ADD);
            showDuration(display, sequencerState.bank[tempState.active_bank].default_duration, 35);
            break;
                    
        case BANK_WEIGHT_EDIT:
            drawText(&display, font_8x8, "Random weight", 0, 10, WriteMode::ADD);
            sprintf(sbuf, "%2.2d", sequencerState.bank[tempState.active_bank].weight);
            drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
            drawLine(&display, 0, 25+16, 2*12-1, 25+16);
            break;
    }

}

void showBankList(SSD1306 &display, int16_t blist[], int y, int pos)
{
    char sbuf[10];
    bool done = false;
    int idx;

    for (int by=0; by<4; by++) {
        for (int bx=0; bx<4; bx++) {
            idx = by * 4 + bx;
            if (blist[idx] < 0)
                done = true;

            if (!done) {
                sprintf(sbuf, "%2.2d", blist[idx]);
                drawText(&display, font_8x8, sbuf, bx*18, y+by*10, WriteMode::ADD);
            } else {
                drawText(&display, font_8x8, "..", bx*18, y+by*10, WriteMode::ADD);
            }
            if (idx == pos)
                drawLine(&display, bx*18, y+by*10+8, bx*18 + 16 - 1, y+by*10+8);
        }
    }

}

void showGroupEdit(SSD1306 &display)
{
    char sbuf[20];

    if (sequencerState.group[tempState.active_group].mute) {
        drawText(&display, font_8x8, "MUTE", 95, 0, WriteMode::ADD);
    }

    int len;
    switch (tempState.sub_mode) {
        case GROUP_MODE_EDIT:
            drawText(&display, font_8x8, "Mode", 0, 10, WriteMode::ADD);
            drawText(&display, font_8x8, group_mode_names[sequencerState.group[tempState.active_group].mode], 0, 35, WriteMode::ADD);
            len = strlen(group_mode_names[sequencerState.group[tempState.active_group].mode]);
            drawLine(&display, 0, 35+8, len*8-1, 35+8, WriteMode::ADD);
            break;
        
        case GROUP_RANDOM_DURATION:
            drawText(&display, font_8x8, "Random duration", 0, 10, WriteMode::ADD);
            showDuration(display, sequencerState.group[tempState.active_group].random_duration, 35);
            break;

        case GROUP_RANDOM_QUANT:
            drawText(&display, font_8x8, "Random quant", 0, 10, WriteMode::ADD);
            showEditQuant(display, sequencerState.group[tempState.active_group].random_qkey, sequencerState.group[tempState.active_group].random_qmode, 27);
            break;

        case GROUP_RANDOM_LOWER:
            drawText(&display, font_8x8, "Lower rand note", 0, 10, WriteMode::ADD);
            showEditNote(display, sequencerState.group[tempState.active_group].random_lower, 27);
            break;
        
        case GROUP_RANDOM_UPPER:
            drawText(&display, font_8x8, "Upper rand note", 0, 10, WriteMode::ADD);
            showEditNote(display, sequencerState.group[tempState.active_group].random_upper, 27);
            break;

        case GROUP_TURING_SIZE:
            drawText(&display, font_8x8, "Turing length", 0, 10, WriteMode::ADD);
            sprintf(sbuf, "%2.2d", sequencerState.group[tempState.active_group].turing_size);
            drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
            drawLine(&display, 0, 25+16, 2*12-1, 25+16);
            break;

        case GROUP_TURING_PULSE:
            drawText(&display, font_8x8, "Turing pulse", 0, 10, WriteMode::ADD);
            if (sequencerState.group[tempState.active_group].turing_pulse >= 0) {
                sprintf(sbuf, "%2.2d", sequencerState.group[tempState.active_group].turing_pulse);
                drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
                len = 2;
            }
            else {
                drawText(&display, font_12x16, "OFF", 0, 25, WriteMode::ADD);
                len = 3;
            }
            drawLine(&display, 0, 25+16, len*12-1, 25+16);
            break;         
        
        case GROUP_TURING_HAMMER:
            drawText(&display, font_8x8, "Turing hammer on", 0, 10, WriteMode::ADD);
            if (sequencerState.group[tempState.active_group].turing_hammer_on) {
                drawText(&display, font_12x16, "ON", 0, 25, WriteMode::ADD);
                len = 2;
            } else {
                drawText(&display, font_12x16, "OFF", 0, 25, WriteMode::ADD);
                len = 3;
            }
            drawLine(&display, 0, 25+16, len*12-1, 25+16);
            break;
            
        case GROUP_LENGTH_EDIT:
            drawText(&display, font_8x8, "Group Length", 0, 10, WriteMode::ADD);
            if (sequencerState.group[tempState.active_group].length == GRP_LENGTH_AUTO) {
                sprintf (sbuf, "Auto");
                len = 4;
            } else {
                sprintf(sbuf, "%3.3d", sequencerState.group[tempState.active_group].length);
                len = 3;
            }
            drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
            drawLine(&display, 0, 25+16, len*12-1, 25+16);
            sprintf(sbuf, "(max %d)", maxGroupLength(tempState.active_group));
            drawText(&display, font_8x8, sbuf, 0, 56, WriteMode::ADD);
            break;

        case GROUP_RANDOM_BANK_PERCENT:
            drawText(&display, font_8x8, "Bank Chg Pct", 0, 10, WriteMode::ADD);
            sprintf(sbuf, "%3.3d%%", sequencerState.group[tempState.active_group].random_bank_percent);
            drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
            drawLine(&display, 0, 25+16, 3*12-1, 25+16);
            break;

        case GROUP_BANK_LIST:
            drawText(&display, font_8x8, "Bank List", 0, 10, WriteMode::ADD);
            showBankList(display, sequencerState.group[tempState.active_group].bank_list, 22, tempState.digit_pos);
            break;

        case GROUP_DIVIDER:
            drawText(&display, font_8x8, "Clock Divide", 0, 10, WriteMode::ADD);
            sprintf(sbuf, "%2.2d", sequencerState.group[tempState.active_group].divide);
            drawText(&display, font_12x16, sbuf, 0, 25, WriteMode::ADD);
            drawLine(&display, 0, 25+16, 2*12-1, 25+16);
            break;
    }
}

void showGroupViewNote(SSD1306 &display, int g, int x, int y, bool big, bool show_symbol) {
    char sbuf[20];
    char note_buf[10];
    int extra_y = 0;
    int extra_x = 0;
    if (show_symbol) {
        display.addBitmapImage(0, y, 16, 16, note_bitmap);
        extra_y = big ? 0 : 4;
        extra_x = 18;
    }
    int char_x = big ? 12 : 8;
    const unsigned char *font = big ? font_12x16 : font_8x8;

    note2Str(note_buf, cv2midi(tempState.current_group_ptr[g].note));
    sprintf(sbuf, "%3s", note_buf);
    drawText(&display, font, sbuf, x + extra_x, y + extra_y, WriteMode::ADD);
    sprintf(sbuf, "%4.4X", tempState.current_group_ptr[g].note);
    drawText(&display, font, sbuf, x + extra_x + 4 * char_x, y + extra_y, WriteMode::ADD);
}

void showTuringState(SSD1306 &display, int g, int x, int y) {
    char sbuf[10];
    //for (int i=0; i<sequencerState.group[g].turing_size; i++) {
    int size = sequencerState.group[g].turing_size;
    for (int i=0; i<16; i++) {
        if (sequencerState.group[g].turing_state[i]) {
            fillRect(&display, x+i*7, y, x+i*7+7, y+7, WriteMode::ADD);
        } else {
            drawRect(&display, x+i*7, y, x+i*7+7, y+7, WriteMode::ADD);
        }
        if ((i == 0) || (i == (size - 1))) {
            drawLine(&display, x+4+i*7, y+8, x+4+i*7, y+19, WriteMode::ADD);
        }
    }
    // draw arrow
    drawLine(&display, x+4, y+7, x+1, y+10, WriteMode::ADD);
    drawLine(&display, x+4, y+7, x+7, y+10, WriteMode::ADD);

    // draw line with user input
    if (tempState.turing_one) {
        drawText(&display, font_8x8, "1", x, y+21, WriteMode::ADD);
    } else if (tempState.turing_zero) {
        drawText(&display, font_8x8, "0", x, y+21, WriteMode::ADD);
    } else {
        drawLine(&display, x+4, y+19, x+4+(size-1)*7, y+19);
    }

    // display randomness
    if (sequencerState.group[g].turing_lock) {
        sprintf(sbuf, "LOCKED");
    } else {
        sprintf(sbuf, "rand %d", tempState.randomness);
    }
    drawText(&display, font_8x8, sbuf, x+64, y+21);
}

void showGroupView(SSD1306 &display)
{
    char sbuf[20];
    int g = tempState.active_group;

    if (sequencerState.group[g].mute) {
        drawText(&display, font_8x8, "MUTE", 95, 0, WriteMode::ADD);
    }

    switch (sequencerState.group[g].mode) {
        case GM_SEQUENCED:
        case GM_RANDOM_BANK:
            if ((sequencerState.group[g].bank_list[0] >= 0) && (sequencerState.group[g].length != 0)) {
                sprintf(sbuf, "Bank %2d", tempState.current_group_ptr[g].bank_idx);
                drawText(&display, font_12x16, sbuf, 0, 12, WriteMode::ADD);
                sprintf(sbuf, "Slot %2d", tempState.current_group_ptr[g].slot_idx);
                drawText(&display, font_12x16, sbuf, 0, 30, WriteMode::ADD);
            }
            else {
                sprintf(sbuf, "Bank -- Slot --");
                drawText(&display, font_12x16, "Bank --", 0, 12, WriteMode::ADD);
                drawText(&display, font_12x16, "Slot --", 0, 30, WriteMode::ADD);
            }
            showGroupViewNote(display, g, 0, 50, true, true);
            break;

        case GM_RANDOM:
            showGroupViewNote(display, g, 0, 30, true, true);
            break;

        case GM_RANDOM_SLOT:
            sprintf(sbuf, "Bank %2d", tempState.current_group_ptr[g].bank_idx);
            drawText(&display, font_12x16, sbuf, 0, 12, WriteMode::ADD);
            sprintf(sbuf, "Slot %2d", tempState.current_group_ptr[g].slot_idx);
            drawText(&display, font_12x16, sbuf, 0, 30, WriteMode::ADD);
            showGroupViewNote(display, g, 0, 50, true, true);
            break;

        case GM_TURING:
            showTuringState(display, g, 0, 12);
            showGroupViewNote(display, g, 0, 56, false, false);
            break;

        case GM_TURING_SLOT:
            showTuringState(display, g, 0, 12);
            sprintf(sbuf, "Bank %2.2d Slot %2.2d", tempState.current_group_ptr[g].bank_idx, tempState.current_group_ptr[g].slot_idx);
            drawText(&display, font_8x8, sbuf, 0, 47, WriteMode::ADD);
            showGroupViewNote(display, g, 0, 56, false, false);
            break;
    }
}

// show bank and possibly slow or possibly group at top
void showHeader(SSD1306 &display)
{
    char sbuf[20];

    // Bank
    if ((tempState.major_mode == MM_PERFORM) ||
        (tempState.major_mode == MM_SLOT_EDIT) ||
        (tempState.major_mode == MM_BANK_EDIT) ||
        ((tempState.major_mode == MM_GROUP_VIEW) && (sequencerState.group[tempState.active_group].mode == GM_SEQUENCED)))
    {
        sprintf(sbuf, "%2.2d", tempState.active_bank);
        drawLine(&display, 0, 0, 8, 0, WriteMode::ADD);
        drawLine(&display, 0, 0, 0, 8, WriteMode::ADD);
        drawInvChar(&display, font_8x8, 'B', 1, 1, WriteMode::ADD);
        drawText(&display, font_8x8, sbuf, 11, 1, WriteMode::ADD);
    }

    // Slot
    if (tempState.major_mode == MM_SLOT_EDIT)
    {
        sprintf(sbuf, "%2.2d", tempState.active_slot);
        drawLine(&display, 34, 0, 42, 0, WriteMode::ADD);
        drawLine(&display, 34, 0, 34, 8, WriteMode::ADD);
        drawInvChar(&display, font_8x8, 'S', 35, 1, WriteMode::ADD);
        drawText(&display, font_8x8, sbuf, 45, 1, WriteMode::ADD);
    }

    // Group
    if ((tempState.major_mode == MM_GROUP_EDIT) || (tempState.major_mode == MM_GROUP_VIEW))
    {
        sprintf(sbuf, "%c", tempState.active_group + 'A');
        drawLine(&display, 68, 0, 76, 0, WriteMode::ADD);
        drawLine(&display, 68, 0, 68, 8, WriteMode::ADD);
        drawInvChar(&display, font_8x8, 'G', 69, 1, WriteMode::ADD);
        drawText(&display, font_8x8, sbuf, 79, 1, WriteMode::ADD);
    }

}

void updateMessage(SSD1306 &display) {
    int width = strlen(tempState.message);
    int height;
    if (width <= 10) {
        width *= 12;
        height = 12; 
        drawText(&display, font_12x16, tempState.message, 64 - width/2, 32-height/2, WriteMode::ADD);
    } else {
        width *= 8;
        height = 8;
        drawText(&display, font_8x8, tempState.message, 64 - width/2, 32-height/2, WriteMode::ADD);
    }
    drawRect(&display, 64 - width/2 - 2, 32-height/2 - 2, 64 + width/2 + 2, 32 + height/2 + 2, WriteMode::ADD);
}

#define MESSAGE_TIME_MS 1000

void updateDisplay(SSD1306 &display) {
    display.clear();

    if (tempState.show_message) {
        updateMessage(display);
        display.sendBuffer();
        tempState.show_message = false;
        busy_wait_ms(MESSAGE_TIME_MS);
        display.clear();
    }

    showHeader(display);

    switch (tempState.major_mode) {
        case MM_PERFORM:
            showGroupPtrs(display);
            break;

        case MM_MEM:
            showMem(display);
            break;

        case MM_QUANT:
            drawText(&display, font_8x8, "Quantization", 0, 0, WriteMode::ADD);
            showEditQuant(display, sequencerState.quant_key, sequencerState.quant_mode, 20);
            break;

        case MM_TEMPO:
            showTempo(display);
            break;

        case MM_SLOT_EDIT:
            showSlot(display);
            break;

        case MM_BANK_EDIT:
            showBank(display);
            break;

        case MM_GROUP_EDIT:
            showGroupEdit(display);
            break;

        case MM_GROUP_VIEW:
            showGroupView(display);
            break;
    }

    display.sendBuffer();
}

int main()
{
    struct repeating_timer timer_data; // can't ever go out of scope
    
    for (int g=0; g<4; g++) {
        gate_end_time_valid[g]= false;
        gate_end_cycles_valid[g] = false;
        gate_triggered[g] = false;
    }

    get_rand_32(); // seed RNG;

    factoryInitState();
    initTempState();

    for (int led=0; led<24; led++)
        led_levels[led] = 0;
    
    stdio_init_all();
    initIOs();

    enc.Init(ENCA, ENCB, ENC_SW);
    buttons.Init(HOLD_MS);
    inQ.Init();

    // Start OLED display
    SSD1306 display = SSD1306(I2C_PORT, 0x3C, Size::W128xH64);
    display.setOrientation(false);
    display.clear();
    drawText(&display, font_12x16, "Hello!", 0, 0, WriteMode::ADD);
    display.sendBuffer();

    // start core1
    multicore_launch_core1(core1_entry);

    ledTest();

    // start core0 ms timer
    add_repeating_timer_ms(-1, timer_callback, NULL, &timer_data);

    //puts("Hello, world!");
    //printf("address = %p\n", save_addr);
    //printf("core1_entry = %p", core1_entry);

    while (1)
    {
        getControls();
        tempState.randomness = (adc_read() >> 6) & 0x3f; // round to 0-63
        updateDisplay(display);
        //saveState();
    }
    
    return 0;
}
