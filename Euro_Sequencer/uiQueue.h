#define UIQUEUE_SIZE 10

class uiQueue
{
public:
    enum uiMsg
    {
        MSG_NULL,
        MSG_ENC_INC,
        MSG_ENC_DEC,
        MSG_ENC_PRESSED,
        MSG_BUTTON_RELEASED,
        MSG_BUTTON_HELD,
    };

    struct uiEntry {
        uiMsg msg;
        uint32_t data;
    };

    uiQueue() {}
    ~uiQueue() {}

    void Init();
    void Enqueue(uiMsg msg, uint32_t data);
    uiEntry Dequeue();
    inline bool Empty() const { return head_ == tail_; }
    inline bool Full() const { return ((head_ + 1) % UIQUEUE_SIZE) == tail_; }

private:
    uiEntry queue_[UIQUEUE_SIZE];
    int head_;
    int tail_;
};

void uiQueue::Init() {
    head_ = tail_ = 0;
    for (int i=0; i<UIQUEUE_SIZE; i++) {
        queue_[i].msg = MSG_NULL;
        queue_[i].data = 0;
    }
}

void uiQueue::Enqueue(uiMsg msg, uint32_t data) {
    if (msg == MSG_NULL)
        return;

    queue_[head_].msg = msg;
    queue_[head_].data = data;
    int head = (head_ + 1) % UIQUEUE_SIZE;
    int tail = tail_;
    if (head == tail) {
        // overflow
        tail = (tail_ + 1) % UIQUEUE_SIZE;
    }
    head_ = head;
    tail_ = tail;
}

uiQueue::uiEntry uiQueue::Dequeue() {
    uiEntry rval;
    if (head_ == tail_) {
        rval.msg = MSG_NULL;
        rval.data = 0;
        return rval;
    }

    rval = queue_[tail_];
    tail_ = (tail_ + 1) % UIQUEUE_SIZE;
    return rval;
}