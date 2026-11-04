#pragma once

// QuitSignals - SIGTERM / SIGINT / SIGHUP end the GUI like closing the window (HIL 2026-11-03,
// plan 0012 G-H4): QCoreApplication::quit(), so that main() saves the state and every session is
// closed (a Bluetooth link released in the system, console files and the log flushed). Without it
// a desktop logout or `kill` left BlueZ connected to the device, which then stopped advertising.
//
// POSIX: sigaction() with SA_RESETHAND sets a lock-free std::atomic<bool> (a signal handler must
// not do more); an owned QTimer polls it every pollInterval on the GUI thread. A second signal
// gets the default action (forced exit). The destructor restores the previous handlers.
// Windows: nothing to do (Qt turns WM_CLOSE / WM_ENDSESSION into the normal close).
// Ownership: a stack object in main(), GUI thread only; at most one at a time (process-wide
// handlers).

#include <QTimer>

#include <chrono>

namespace hwio::gui {

class QuitSignals {
public:
    static constexpr std::chrono::milliseconds pollInterval{100};

    QuitSignals();
    ~QuitSignals();
    QuitSignals(const QuitSignals&) = delete;
    QuitSignals& operator=(const QuitSignals&) = delete;
    QuitSignals(QuitSignals&&) = delete;
    QuitSignals& operator=(QuitSignals&&) = delete;

private:
    QTimer m_poll;
};

} // namespace hwio::gui
