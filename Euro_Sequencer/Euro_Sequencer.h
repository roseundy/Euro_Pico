// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 4
#define I2C_SCL 5

// DAC controls
#define DACA 3
#define DACB 2
#define DACC 1
#define DACD 0

// Shift registers
#define SHCLK 11
#define SHDO 12
#define SHDI 13
#define LDPISO 14
#define LDSIPO 15

// Gate outputs
#define GATEA 6
#define GATEB 7
#define GATEC 8
#define GATED 9

// Encoder
#define ENCA 20
#define ENCB 21
#define ENC_SW 22

// Clock in
#define NCLOCK 10

// Random control
#define RANDOM 26
#define RANDOM_ADC 0

#define HOLD_MS 1000 // time for a button press to be considered held

#define BUTTON_TEMPO 0
#define BUTTON_QUANT 1
#define BUTTON_MEM 2
#define BUTTON_RUN 3
#define BUTTON_SLOT 4
#define BUTTON_BANK 5
#define BUTTON_GEDIT 6
#define BUTTON_GVIEW 7

#define BUTTON_FIRST_NUM 8
const int button2num[16] = {15, 11, 7, 3, 2, 6, 10, 14, 13, 9, 5, 1, 0, 4, 8, 12};
#define BUTTON2NUM(button) button2num[button-8]

#define LED_0 15
#define LED_1 14
#define LED_2 13
#define LED_3 12
#define LED_4 11
#define LED_5 10
#define LED_6 9
#define LED_7 8
#define LED_8 4
#define LED_9 5
#define LED_10 6
#define LED_11 7
#define LED_12 3
#define LED_13 2
#define LED_14 1
#define LED_15 0

#define LED_GVIEW 19
#define LED_GEDIT 18
#define LED_BANK 17
#define LED_SLOT 16

#define LED_TEMPO 20
#define LED_QUANT 21
#define LED_MEM 22
#define LED_RUN 23
const int modeleds[7] = {16, 17, 18, 19, 20, 21, 22};
#define NUM_MODE_LEDS 7

const int num2led[16] = {15, 14, 13, 12, 11, 10, 9, 8, 4, 5, 6, 7, 3, 2, 1, 0};
#define NUM2LED(num) num2led[num]

#define MAX_BANKS_IN_GROUP 16

unsigned char note_bitmap[] = {
            0b00000000, 0b10000000,
            0b00000000, 0b11000000,
            0b00000000, 0b10100000,
            0b00000000, 0b10000000,
            0b00000000, 0b10000000,
            0b00000000, 0b10000000,
            0b00000000, 0b10000000,
            0b00000000, 0b10000000,
            0b00000000, 0b10000000,
            0b00111110, 0b10000000,
            0b01111111, 0b10000000,
            0b11111111, 0b10000000,
            0b01111111, 0b10000000,
            0b00111110, 0b00000000,
            0b00000000, 0b00000000,
            0b00000000, 0b00000000
    };

enum SeqMajorMode_t {
    MM_PERFORM,
    MM_MEM,
    MM_QUANT,
    MM_TEMPO,
    MM_SLOT_EDIT,
    MM_BANK_EDIT,
    MM_GROUP_EDIT,
    MM_GROUP_VIEW,
};

enum SeqSubMode_t {
    MEM_READ,
    MEM_WRITE,

    TEMPO_BPM_EDIT,
    TEMPO_DUTY_EDIT,

    SLOT_NOTE_EDIT,
    SLOT_DURATION_EDIT,
    
    BANK_LENGTH_EDIT,
    BANK_RELATIVE_TIME_EDIT,
    BANK_DEFAULT_DURATION_EDIT,
    BANK_WEIGHT_EDIT,
    
    GROUP_MODE_EDIT,
    GROUP_LENGTH_EDIT,
    GROUP_BANK_LIST,
    GROUP_TURING_SIZE,
    GROUP_TURING_PULSE,
    GROUP_TURING_HAMMER,
    GROUP_RANDOM_DURATION,
    GROUP_RANDOM_LOWER,
    GROUP_RANDOM_UPPER,
    GROUP_RANDOM_QUANT,
    GROUP_RANDOM_BANK_PERCENT,
    GROUP_DIVIDER,
};

enum QuantMode_t {
    QUANT_OFF,
    QUANT_ACTIVE,
    QUANT_CHROMATIC,
    QUANT_MAJOR,
    QUANT_DORIAN,
    QUANT_PHRYGIAN,
    QUANT_LYDIAN,
    QUANT_MIXOLYDIAN,
    QUANT_MINOR,
    QUANT_LOCRIAN,
    QUANT_MAJOR_PENTATONIC,
    QUANT_MINOR_PENTATONIC,
    QUANT_BLUES,
};
#define QUANT_FIRST QUANT_CHROMATIC
#define QUANT_LAST QUANT_BLUES
const char *quant_names[] = {"Off", "Active", "Chromatic", "Major", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Minor", "Locrian", "Major Pent", "Minor Pent", "Blues"};
const char *quant_steps[] = {"", "", "1111111111", "2212221", "2122212", "1222122", "2221221", "2212212", "2122122", "1221222", "22323", "32232", "211323"};
const int quant_num_steps[] = {0, 0, 11, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6};
const char *key_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

enum GroupMode_t {
    GM_SEQUENCED,
    GM_RANDOM,
    GM_RANDOM_SLOT,
    GM_RANDOM_BANK,
    GM_TURING,
    GM_TURING_SLOT,
};
const char *group_mode_names[] = {"Sequenced", "Random", "Random Slot", "Random Bank", "Turing", "Turing Slot"};
#define GM_FIRST GM_SEQUENCED
#define GM_LAST GM_TURING_SLOT

struct GroupPtr_t {
    int bank_idx;
    int slot_idx;
    int list_idx;
    int note_count;
    int clock_count;
    uint16_t note;

};

#define DURATION_PASS 0
#define DURATION_1CLOCK 7
#define DURATION_2DIGIT (10 + DURATION_1CLOCK - 1)
#define DURATION_MAX (99 + DURATION_1CLOCK - 1)
const char *duration_names[] = {"pass", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2"};
const int duration_divisors[] = {1, 64, 32, 16, 8, 4, 2};
struct SlotData_t {
    uint16_t note;
    uint16_t duration; // either 10's of milliseconds or an encoded number of clocks
    bool mute;
};

struct BankData_t {
    SlotData_t slot[16];
    bool relative_time;
    int length;
    uint16_t default_duration;
    int weight;
};

struct GroupData_t {
    int16_t bank_list[MAX_BANKS_IN_GROUP];
    int16_t length;
    GroupMode_t mode;
    bool mute;
    int16_t divide;
    uint16_t random_duration;
    uint16_t random_lower;
    uint16_t random_upper;
    QuantMode_t random_qmode;
    int16_t random_qkey;
    int16_t random_bank_percent;
    bool turing_state[16];
    int16_t turing_size;
    int16_t turing_pulse;
    int16_t turing_hammer_on;
    bool turing_lock;
};

#define GRP_LENGTH_AUTO -1
#define GRP_PULSE_OFF -1

// Persistant state
struct SequencerState_t {
    uint32_t version;
    BankData_t bank[16];
    GroupData_t group[4];
    uint16_t tempo_bpm;
    uint16_t tempo_duty;
    bool tempo_enabled;
    QuantMode_t quant_mode;
    int quant_key;
};

// Non-persistant state
struct TempState_t {
    bool running;
    bool clock_in;
    int active_slot;
    int active_bank;
    int active_group;
    SeqMajorMode_t major_mode;
    SeqSubMode_t sub_mode;
    int digit_pos;
    GroupPtr_t current_group_ptr[4];

    bool turing_one;
    bool turing_zero;
    uint16_t randomness;
    uint16_t tempo_period_ms;
    uint16_t tempo_high_ms;
    bool tempo_clock;
    int mem_page;
    bool mem_confirm;
    char message[20];
    bool show_message;
};

#define BRT_FULL 256
#define BRT_HALF 64
#define BRT_DIM 16
#define BRT_VERY_DIM 1
#define BRT_OFF 0
#define LED_FLASH_PERIOD 250
#define LED_FLASH_HALF_PERIOD 125
#define LED_SWEEP_PERIOD 1600

#define FLASH_TARGET_OFFSET 0x1f8000

#define SAVE_VERSION 0x1

