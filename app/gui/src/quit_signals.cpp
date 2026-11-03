#include "quit_signals.h"

#include "gui_log.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QObject>
#include <QTimer>
#include <QtLogging>

#ifndef _WIN32
#include <atomic>
#include <csignal> // sigaction (POSIX, declared by <signal.h>)
#endif

namespace hwio::gui {

#ifndef _WIN32

namespace {

static_assert(std::atomic<int>::is_always_lock_free,
              "the signal number must be lock-free to be used in a signal handler");

// Process-wide state shared with the signal handler. Owner: the single live QuitSignals. The
// function-local static is constant-initialized (no lazy-initialization guard), so using it from a
// signal handler is safe.
struct PosixState {
    std::atomic<int> signal{0}; // the received signal, 0 = none; set by the handler
    struct sigaction previousInt{};
    struct sigaction previousTerm{};
    struct sigaction previousHup{};
};

PosixState& posixState() {
    static PosixState state;
    return state;
}

extern "C" void onQuitSignal(int signal) {
    posixState().signal.store(signal, std::memory_order_relaxed);
}

} // namespace

QuitSignals::QuitSignals() {
    PosixState& state = posixState();
    state.signal.store(0);
    struct sigaction action{};
    action.sa_handler = onQuitSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND; // the second signal gets the default action (forced exit)
    sigaction(SIGINT, &action, &state.previousInt);
    sigaction(SIGTERM, &action, &state.previousTerm);
    sigaction(SIGHUP, &action, &state.previousHup);
    m_poll.setInterval(pollInterval);
    QObject::connect(&m_poll, &QTimer::timeout, &m_poll, [] {
        const int signal = posixState().signal.exchange(0, std::memory_order_relaxed);
        if (signal != 0) {
            qCInfo(lcGui) << "Quitting on signal" << signal;
            QCoreApplication::quit();
        }
    });
    m_poll.start();
}

QuitSignals::~QuitSignals() {
    m_poll.stop();
    const PosixState& state = posixState();
    sigaction(SIGINT, &state.previousInt, nullptr);
    sigaction(SIGTERM, &state.previousTerm, nullptr);
    sigaction(SIGHUP, &state.previousHup, nullptr);
}

#else

QuitSignals::QuitSignals() = default;
QuitSignals::~QuitSignals() = default;

#endif

} // namespace hwio::gui
