#define ENCODER_DEBOUNCE_US 500

class Encoder {
    public:
        Encoder() {}
        ~Encoder() {}

        void Init(int pin_a, int pin_b, int pin_sw);
        void Debounce();
        inline int Increment() const { return updated_ ? inc_ : 0; }
        inline bool RisingEdge() const { return updated_ ? button_ == 0x7f : false; }
        inline bool FallingEdge() const { return updated_ ? button_ == 0x80 : false; }
        inline bool Pressed() const { return button_ == 0xff; }

    private:
        absolute_time_t last_update_;
        bool updated_;
        uint8_t a_, b_, button_;
        int pin_a_, pin_b_, pin_sw_;
        int inc_;
};

void Encoder::Init(int pin_a, int pin_b, int pin_sw) {
    pin_a_ = pin_a;
    pin_b_ = pin_b;
    pin_sw_ = pin_sw;

    gpio_init(pin_a_);
    gpio_set_dir(pin_a_, GPIO_IN);

    gpio_init(pin_b_);
    gpio_set_dir(pin_b_, GPIO_IN);

    gpio_init(pin_sw_);
    gpio_set_dir(pin_sw_, GPIO_IN);
    gpio_pull_up(pin_sw_);

    last_update_ = get_absolute_time();
    updated_ = false;
    inc_ = 0;
    a_ = b_ = button_ = 0xff;
}

void Encoder::Debounce() {
    absolute_time_t now = get_absolute_time();
    updated_ = false;
    if (absolute_time_diff_us(last_update_, now) > ENCODER_DEBOUNCE_US) {
        last_update_ = now;
        updated_ = true;

        a_ = (a_ << 1) | (gpio_get(pin_a_) & 0x1);
        b_ = (b_ << 1) | (gpio_get(pin_b_) & 0x1);
        button_ = (button_ << 1) | (~gpio_get(pin_sw_) & 0x1);
        
        inc_ = 0; // reset inc_ first
        if((a_ & 0x03) == 0x02 && (b_ & 0x03) == 0x00) {
            inc_ = -1;
        } else if((b_ & 0x03) == 0x02 && (a_ & 0x03) == 0x00) {
            inc_ = 1;
        }
   }
}