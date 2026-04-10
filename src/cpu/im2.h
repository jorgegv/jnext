#pragma once
#include <cstdint>

enum class Im2Level : int {
    FRAME_IRQ = 0, LINE_IRQ, CTC_0, CTC_1, CTC_2, CTC_3,
    UART_TX_0, UART_RX_0, UART_TX_1, UART_RX_1,
    DMA, DIVMMC, ULA_EXTRA, MULTIFACE,
    COUNT = 14
};

class Im2Controller {
public:
    void reset();
    void raise(Im2Level level);
    void clear(Im2Level level);
    bool has_pending() const;
    uint8_t get_vector() const;
    void set_mask(uint16_t mask);
    void on_reti();
    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);
private:
    static constexpr int N = static_cast<int>(Im2Level::COUNT);
    bool     pending_[N]   = {};
    bool     active_[N]    = {};
    uint16_t mask_         = 0xFFFF;
    int      active_level_ = -1;
};
