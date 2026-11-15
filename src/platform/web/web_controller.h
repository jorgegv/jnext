#pragma once
class SdlApp;

// The browser page's controller (GH #465), called from browser_frontend.cpp.
namespace web_controller {

/// Once, when the loop is installed.
void attach(SdlApp& app);

/// After every SdlApp::tick().
void after_tick(SdlApp& app, bool more);

}  // namespace web_controller
