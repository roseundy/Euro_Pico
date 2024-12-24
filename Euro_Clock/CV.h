class CV {
    public:
        CV() {}
        ~CV() {}

        void Init(int pin_cv, int input_cv);
        uint16_t Read();

    private:
        int pin_cv_;
        int input_cv_;
};

void CV::Init(int pin_cv, int input_cv) {
    pin_cv_ = pin_cv;
    input_cv_ = input_cv;

    adc_gpio_init(pin_cv_);
}

uint16_t CV::Read() {
    adc_select_input(input_cv_);

    return adc_read();
}