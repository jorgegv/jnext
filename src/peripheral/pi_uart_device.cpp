#include "pi_uart_device.h"

#include <utility>

PiUartDevice::PiUartDevice(std::unique_ptr<JoyUartEndpoint> endpoint)
    : link_(std::move(endpoint), /*connector=*/0) {
    link_.set_byte_sink([this](uint8_t byte) {
        // The byte has finished arriving on the Pi's TX pin. Whether UART 1 is
        // listening to that pin is the NR 0xA0 GPIO mux's decision; when it is
        // not, the byte is lost on the wire, exactly as it is on hardware.
        if (!connected()) {
            link_.note_dropped();
            return;
        }
        send_to_guest(byte);
        link_.note_delivered();
    });
}

PiUartDevice::~PiUartDevice() = default;

void PiUartDevice::receive(uint8_t byte) {
    // The guest transmitted on UART 1. With the GPIO mux not routing it to the
    // Pi's RX pin, nothing on the far end hears it.
    if (!connected()) {
        ++tx_disconnected_;
        return;
    }
    link_.send_to_host(byte);
}

void PiUartDevice::poll() {
    link_.poll();
    mirror_gate();
}

void PiUartDevice::tick(uint32_t master_cycles, uint32_t byte_ticks) {
    link_.tick(master_cycles, byte_ticks);
    mirror_gate();
}

void PiUartDevice::set_inert(bool inert) {
    inert_ = inert;
    link_.set_inert(inert);
    mirror_gate();
}
