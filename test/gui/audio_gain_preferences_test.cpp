#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/app_config.h"
#include "gui/main_window.h"
#include "gui/preferences_dialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSlider>

#include <cstdio>

namespace {

int passed = 0;
int failed = 0;

void check(const char* id, bool condition)
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL %s\n", id);
    }
}

QSlider* slider(PreferencesDialog& dialog, const char* name)
{
    return dialog.findChild<QSlider*>(QString::fromLatin1(name));
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    AppConfigData initial;
    initial.audio_gain_db = 1.0f;
    initial.audio_gain_beeper_db = 2.0f;
    initial.audio_gain_ay_db[0] = 3.0f;
    initial.audio_gain_ay_db[1] = 4.0f;
    initial.audio_gain_ay_db[2] = 5.0f;
    initial.audio_gain_dac_db = 6.0f;
    PreferencesDialog dialog(initial);

    QSlider* controls[6] = {
        slider(dialog, "audioGainDbSlider"),
        slider(dialog, "audioGainBeeperDbSlider"),
        slider(dialog, "audioGainAy0DbSlider"),
        slider(dialog, "audioGainAy1DbSlider"),
        slider(dialog, "audioGainAy2DbSlider"),
        slider(dialog, "audioGainDacDbSlider")
    };
    bool complete = true;
    for (QSlider* control : controls)
        complete = complete && control &&
                   control->minimum() == -24 && control->maximum() == 24;
    check("SGP-01", complete);

    for (int i = 0; i < 6; ++i)
        check(i == 0 ? "SGP-02" : i == 1 ? "SGP-03" : i == 2 ? "SGP-04" :
              i == 3 ? "SGP-05" : i == 4 ? "SGP-06" : "SGP-07",
              controls[i] && controls[i]->value() == i + 1);

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dialog, &PreferencesDialog::apply_requested,
                     [&collected, &emitted](const AppConfigData& cfg) {
                         collected = cfg;
                         emitted = true;
                     });
    for (int i = 0; i < 6; ++i) controls[i]->setValue(-(i + 1));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    buttons->button(QDialogButtonBox::Apply)->click();
    check("SGP-08", emitted &&
                    collected.audio_gain_db == -1.0f &&
                    collected.audio_gain_beeper_db == -2.0f &&
                    collected.audio_gain_ay_db[0] == -3.0f &&
                    collected.audio_gain_ay_db[1] == -4.0f &&
                    collected.audio_gain_ay_db[2] == -5.0f &&
                    collected.audio_gain_dac_db == -6.0f);

    Emulator emulator;
    EmulatorConfig config;
    config.type = MachineType::ZX48K;
    check("SGP-09", emulator.init(config));
    MainWindow window;
    window.set_emulator(&emulator);
    window.apply_startup_config(collected);
    check("SGP-10", emulator.mixer().output_gain_db() == -1.0f &&
                    emulator.mixer().beeper_gain_db() == -2.0f &&
                    emulator.mixer().ay_gain_db(0) == -3.0f &&
                    emulator.mixer().ay_gain_db(1) == -4.0f &&
                    emulator.mixer().ay_gain_db(2) == -5.0f &&
                    emulator.mixer().dac_gain_db() == -6.0f);

    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n",
                passed + failed, passed, failed);
    return failed == 0 ? 0 : 1;
}
