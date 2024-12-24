#define BUTTON_DEBOUNCE_US 500

class Button {
    public:
        enum buttonPull
        {
            NONE,
            PULLUP,
            PULLDOWN,
        };

        Button() {}
        ~Button() {}

        void Init(int pin_sw, buttonPull pull = NONE, bool invert = true);
        void Debounce();
        inline bool RisingEdge() const { return updated_ ? button_ == 0x7f : false; }
        inline bool FallingEdge() const { return updated_ ? button_ == 0x80 : false; }
        inline bool Pressed() const { return button_ == 0xff; }

    private:
        absolute_time_t last_update_;
        bool updated_;
        uint8_t button_;
        int pin_sw_;
        bool invert_;
};

void Button::Init(int pin_sw, buttonPull pull, bool invert) {
    pin_sw_ = pin_sw;
    invert_ = invert;

    gpio_init(pin_sw_);
    gpio_set_dir(pin_sw_, GPIO_IN);
    if (pull == PULLUP)
        gpio_pull_up(pin_sw_);
    else if (pull == PULLDOWN)
        gpio_pull_down(pin_sw);

    last_update_ = get_absolute_time();
    updated_ = false;
    button_ = 0xff;
}

void Button::Debounce() {
    absolute_time_t now = get_absolute_time();
    updated_ = false;
    if (absolute_time_diff_us(last_update_, now) > BUTTON_DEBOUNCE_US) {
        last_update_ = now;
        updated_ = true;

        if (invert_)
            button_ = (button_ << 1) | (~gpio_get(pin_sw_) & 0x1);
        else
            button_ = (button_ << 1) | (gpio_get(pin_sw_) & 0x1);
   }
}