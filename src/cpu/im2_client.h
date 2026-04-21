#pragma once
#include "im2.h"

/// Convenience facade for peripherals whose interrupt source lives in
/// Im2Controller. The peripheral holds an Im2Client member; the Controller
/// owns the state.
///
/// Phase 2 agents will add Im2Client members to Ctc/Ula/Uart/Line sources so
/// that those peripherals no longer call Im2Controller directly — they route
/// through a strongly-typed DevIdx facade.
class Im2Client {
public:
    explicit Im2Client(Im2Controller& c, Im2Controller::DevIdx d) : c_(c), d_(d) {}

    void raise()     { c_.raise_req(d_); }
    void clear()     { c_.clear_req(d_); }
    void pulse_unq() { c_.raise_unq(d_); }
    bool status() const { return c_.int_status(d_); }

    Im2Controller::DevIdx index() const { return d_; }

private:
    Im2Controller& c_;
    Im2Controller::DevIdx d_;
};
