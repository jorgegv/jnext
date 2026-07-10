#pragma once

#include <string>

/// Task 27: GUI download-confirmation prompt. Shows a modal Qt dialog asking
/// the user whether to download the NextZXOS distribution image. Returns true
/// if the user accepts. Creates a temporary QApplication if none exists yet
/// (this runs before the main QtApp constructs its own).
bool sdcard_gui_confirm_download(const std::string& message);
