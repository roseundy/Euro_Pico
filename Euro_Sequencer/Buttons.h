#define BUTTON_DEBOUNCE_US 500

class Buttons {
    public:
        Buttons() {}
        ~Buttons() {}

        void Init(int hold_ms);
        void Debounce(uint32_t vector);
        inline bool RisingEdge(int b) const { return updated_ ? buttons_[b] == 0x7f : false; }
        inline bool FallingEdge(int b) const { return (updated_ && !held_[b]) ? buttons_[b] == 0x80 : false; }
        inline bool Pressed(int b) const { return buttons_[b] == 0xff; }
        bool Held(int b);

    private:
        absolute_time_t last_update_;
        absolute_time_t rising_edge_time_[24];
        int hold_us_;
        bool updated_;
        bool held_[24];
        uint8_t buttons_[24];
};

void Buttons::Init(int hold_ms) {
    hold_us_ = hold_ms * 1000;
    last_update_ = get_absolute_time();
    updated_ = false;
    for (int i=0; i<24; i++)
        buttons_[i] = 0x00;
}

bool Buttons::Held(int b) {
    if (!updated_)
        return false;
    
    absolute_time_t now = get_absolute_time();
    if (!held_[b] && (buttons_[b] == 0xff) && absolute_time_diff_us(rising_edge_time_[b], now) >= hold_us_) {
        held_[b] = true;
        return true;
    }
    return false;
}

void Buttons::Debounce(uint32_t vector) {
    absolute_time_t now = get_absolute_time();
    updated_ = false;
    if (absolute_time_diff_us(last_update_, now) > BUTTON_DEBOUNCE_US) {
        last_update_ = now;
        updated_ = true;

        for (int i=0; i<24; i++) {
            buttons_[i] = (buttons_[i] << 1) | ((~vector >> i) & 0x1);
            if (buttons_[i] == 0x7f)
                rising_edge_time_[i] = now;
            if (buttons_[i] == 0x00)
                held_[i] = false;
        }
   }
}