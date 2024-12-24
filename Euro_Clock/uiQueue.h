#define UIQUEUE_SIZE 10

class uiQueue {
    public:
        enum uiMsg
        {
            MSG_NULL,
            MSG_ENC_INC,
            MSG_ENC_DEC,
            MSG_ENC_PRESSED,
            MSG_RUN_PRESSED,
        };

    uiQueue() {}
    ~uiQueue() {}

    void Init();
    void Enqueue(uiMsg msg);
    uiMsg Dequeue();
    inline bool Empty() const { return head_ == tail_; }
    inline bool Full() const { return ((head_ + 1) % UIQUEUE_SIZE) == tail_; }

    private:
        uiMsg queue_[UIQUEUE_SIZE];
        int head_;
        int tail_;
};

void uiQueue::Init() {
    head_ = tail_ = 0;
    for (int i=0; i<UIQUEUE_SIZE; i++)
        queue_[i] = MSG_NULL;
}

void uiQueue::Enqueue(uiMsg msg) {
    if (msg == MSG_NULL)
        return;

    queue_[head_] = msg;
    int head = (head_ + 1) % UIQUEUE_SIZE;
    int tail = tail_;
    if (head == tail) {
        // overflow
        tail = (tail_ + 1) % UIQUEUE_SIZE;
    }
    head_ = head;
    tail_ = tail;
}

uiQueue::uiMsg uiQueue::Dequeue() {
    if (head_ == tail_)
        return MSG_NULL;

    uiMsg rval = queue_[tail_];
    tail_ = (tail_ + 1) % UIQUEUE_SIZE;
    return rval;
}