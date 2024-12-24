#define LED_SLOW_PERIOD 1000*1000
#define LED_FAST_PERIOD 300*1000

class Led
{
public:
    enum State
    {
        LED_OFF,
        LED_ON,
        LED_SLOW,
        LED_FAST,
    };

    Led() {}
    ~Led() {}

    // refresh_rate in uS
    void Init(int pin_led, int refresh_rate);
    void SetState(State state);
    void Update();
    inline State GetState() const { return state_; } 

private:
    int rate_;
    int period_;
    State state_;
    int count_;
    int pin_led_;
};

void Led::Init(int pin_led, int refresh_rate) {
    pin_led_ = pin_led;
    rate_ = refresh_rate;

    gpio_init(pin_led_);
    gpio_set_dir(pin_led_, GPIO_OUT);
    gpio_put(pin_led_, false);

    SetState(LED_OFF);
}

void Led::SetState(State state) {
    state_ = state;
    count_ = 0;

    if (state == LED_SLOW)
        period_ = LED_SLOW_PERIOD / rate_;
    else if (state == LED_FAST)
        period_ = LED_FAST_PERIOD / rate_;
}

void Led::Update() {
    switch (state_) {
        case LED_OFF:
            gpio_put(pin_led_, false);
            break;
        case LED_ON:
            gpio_put(pin_led_, true);
            break;
        case LED_SLOW:
        case LED_FAST:
            count_ = (count_ + 1) % period_;
            if (count_ < period_ / 2)
                gpio_put(pin_led_, true);
            else
                gpio_put(pin_led_, false);
            break;
    }

}