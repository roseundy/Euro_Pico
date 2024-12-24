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