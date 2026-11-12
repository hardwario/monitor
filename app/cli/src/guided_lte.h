#pragma once

// The terminal flow of the guided LTE update (plan 0033 D7, ADR-0234): `hwio chester lte flash
// --guided` and, for a remote lteUpdate request, `hwio remote host` (plan 0044, ADR-0363).
//
// GuidedTerminal drives one LteUpdateProcedure in the terminal: its prompts and progress on
// stderr, the y/N questions (plan 0046, ADR-0366: "Continue? [y/N] " once the APP connector
// answers, before anything changes - no ends the update as cancelled; and the question of a failed
// copy), Ctrl+C in run() (cancelled where the procedure allows it). run() blocks until the
// procedure ends; without run() (the remote host, whose event loop keeps running) it only follows
// the procedure, and the y/N questions go to `ask` (the host's stdin reader answers them). An empty
// application (no copy) ends the procedure as Cancelled after the LTE update (noCopyEnd()).
//
// designatedLteSource(): the profile's designated LTE firmware (nRF Util checked, downloaded or
// taken from the cache, SHA-256 verified) as the procedure's source.
//
// Thread safety: owner thread of the session. CLI texts: English only (ADR-0027).

#include "cli_app.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/device/lte_update_procedure.h"
#include "progress_ui.h"
#include "terminal.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QtCore/qnamespace.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace hwio::cli {

class GuidedTerminal {
public:
    using Step = device::LteUpdateProcedure::Step;
    // Asks a y/N question and answers later (false = no).
    using AskYesNo = std::function<void(const std::string& question, std::function<void(bool)>)>;

    // `ask` empty = the blocking askYesNo() on the terminal.
    GuidedTerminal(const Context& context, ProgressUi& ui, device::LteUpdateProcedure& procedure,
                   AskYesNo ask = {})
        : m_context(context), m_ui(ui), m_procedure(procedure), m_ask(std::move(ask)) {
        QObject::connect(&procedure, &device::LteUpdateProcedure::changed, &m_loop,
                         [this] { onChanged(); });
        QObject::connect(&procedure, &device::LteUpdateProcedure::progressChanged, &m_loop,
                         [this] { onProgress(); });
        QObject::connect(&procedure, &device::LteUpdateProcedure::finished, &m_loop,
                         &QEventLoop::quit);
        QObject::connect(&m_stopCheck, &QTimer::timeout, &m_loop, [this] { onStopCheck(); });
    }
    ~GuidedTerminal() = default;
    GuidedTerminal(const GuidedTerminal&) = delete;
    GuidedTerminal& operator=(const GuidedTerminal&) = delete;
    GuidedTerminal(GuidedTerminal&&) = delete;
    GuidedTerminal& operator=(GuidedTerminal&&) = delete;

    // Runs until the procedure ends (it was started by the caller).
    void run() {
        show();
        m_stopCheck.start(std::chrono::milliseconds(100));
        if (!ended()) {
            m_loop.exec();
        }
        m_stopCheck.stop();
        m_ui.finish();
    }

    // Shows the current prompt (after start(), without run()).
    void show() {
        const Step step = m_procedure.step();
        const std::string prompt = toStdString(m_procedure.promptText());
        if (step == Step::Failed || step == Step::Cancelled || prompt == m_lastPrompt) {
            return;
        }
        m_ui.finish();
        m_lastPrompt = prompt;
        m_ui.info(prompt);
    }

    [[nodiscard]] bool noCopyEnd() const noexcept { return m_noCopyEnd; }

    // ADR-0368: a remote update whose questions support answers (LteUpdateReporter): the
    // customer is told to wait instead of being asked.
    void setSupportDecides(bool decides) noexcept { m_supportDecides = decides; }

private:
    [[nodiscard]] bool ended() const {
        const Step step = m_procedure.step();
        return step == Step::Done || step == Step::Failed || step == Step::Cancelled;
    }

    void onChanged() {
        show();
        const Step step = m_procedure.step();
        if (step == Step::Confirm && m_supportDecides) {
            m_ui.info("HARDWARIO Support confirms the update. Keep the J-Link cable on the APP "
                      "connector for now.");
        } else if (step == Step::Confirm) {
            // Asked on the next turn of the event loop, after the prompt that says what happens.
            QMetaObject::invokeMethod(&m_loop, [this] { askToContinue(); }, Qt::QueuedConnection);
        } else if (step == Step::BackupFailed && m_supportDecides) {
            if (m_procedure.error()) {
                m_ui.warning(*m_procedure.error());
            }
            m_ui.info("HARDWARIO Support decides whether to continue without a copy.");
        } else if (step == Step::BackupFailed) {
            if (m_procedure.error()) {
                m_ui.warning(*m_procedure.error());
            }
            // Asked on the next turn of the event loop; nothing runs on the device meanwhile.
            QMetaObject::invokeMethod(&m_loop, [this] { askAboutTheCopy(); }, Qt::QueuedConnection);
        } else if (step == Step::ChooseApplication && !m_procedure.hasBackup()) {
            // No copy (an empty or unreadable application): the LTE MCU is updated; the
            // application is the user's to flash.
            m_noCopyEnd = true;
            QMetaObject::invokeMethod(
                &m_loop, [this] { (void)m_procedure.cancel(); }, Qt::QueuedConnection);
        }
    }

    void askToContinue() {
        if (m_procedure.step() != Step::Confirm) {
            return;
        }
        const std::string question = "Continue? [y/N] ";
        if (m_ask) {
            m_ask(question, [this](bool yes) { answerToContinue(yes); });
            return;
        }
        answerToContinue(askYesNo(question));
    }

    void answerToContinue(bool yes) {
        if (m_procedure.step() != Step::Confirm) {
            return;
        }
        if (yes) {
            m_procedure.confirm();
        } else {
            (void)m_procedure.cancel(); // nothing was changed
        }
    }

    void askAboutTheCopy() {
        if (m_procedure.step() != Step::BackupFailed) {
            return;
        }
        const std::string question = "Continue without a copy of the application? [y/N] ";
        if (m_ask) {
            m_ask(question, [this](bool yes) { answerAboutTheCopy(yes); });
            return;
        }
        answerAboutTheCopy(askYesNo(question));
    }

    void answerAboutTheCopy(bool yes) {
        if (m_procedure.step() != Step::BackupFailed) {
            return;
        }
        if (yes) {
            m_procedure.continueWithoutBackup();
        } else {
            (void)m_procedure.cancel();
        }
    }

    void onProgress() {
        const Step step = m_procedure.step();
        if (step == Step::Backup) {
            m_ui.progress("reading", m_procedure.progress().done, m_procedure.progress().total);
        } else if (step == Step::Erase || step == Step::UpdateLte || step == Step::Restore) {
            m_ui.progress(m_procedure.progress());
        }
    }

    void onStopCheck() {
        if (!m_context.stop.stop_requested()) {
            return;
        }
        if (!m_procedure.cancel() && !m_cancelRefusedShown) {
            m_cancelRefusedShown = true;
            m_ui.info("The MCU is being written and cannot be interrupted; cancelling afterwards.");
        }
    }

    const Context& m_context;
    ProgressUi& m_ui;
    device::LteUpdateProcedure& m_procedure;
    AskYesNo m_ask;
    QEventLoop m_loop;
    QTimer m_stopCheck;
    std::string m_lastPrompt;
    bool m_noCopyEnd = false;
    bool m_supportDecides = false;
    bool m_cancelRefusedShown = false;
};

// Plan 0044 (ADR-0363): the designated LTE firmware as the guided update's source; `download`
// gets the download's bytes (done, total). Errors: dependency.nrfutil_*, network.*, firmware.*,
// cancelled (context.stop).
[[nodiscard]] Result<device::LteUpdateProcedure::Source>
designatedLteSource(const Context& context, const GlobalOptions& globals, ProgressUi& ui,
                    const std::function<void(std::uint64_t, std::uint64_t)>& download);

} // namespace hwio::cli
