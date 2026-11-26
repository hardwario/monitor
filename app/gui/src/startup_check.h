#pragma once

// StartupCheck - opens the setup wizard at start when it is needed (plan/features/0008 § Start-up
// behaviour as amended by 0035 § C; ADR-0071).
//
// run() (main(), after the main window is shown) decides (plan 0056, ADR-0392):
//   setup/completedVersion absent or ui/mode not chosen   -> the wizard at Welcome at once (with
//                                                            the mode question while not chosen)
//   else the `jlink-software` check (DiagWorker, ≤ 5 s) failed and setup/showAtStart
//                                                         -> Welcome "J-Link Software is needed"
//                                                            with "Don't show at start"
//   else                                                  -> nothing
// Nothing is opened when the settings are read-only (settings.newer_schema: completion could not
// be recorded, the wizard would open at every start) or when the wizard is not registered. Before
// the "J-Link Software is needed" variant it waits while any wizard is open (a tab's or the global
// one, AppViewModel::anyWizardOpen) and decides once more (with the keys as they are then). The
// setup wizard runs in AppViewModel::globalWizard(). Nothing is blocked meanwhile.
//
// Ownership: created by main() with the AppViewModel as Qt parent. GUI thread only.

#include <QMetaObject>
#include <QObject>
#include <QtTypes>

#include <cstdint>
#include <optional>
#include <vector>

namespace hwio::diag {
struct CheckResult;
} // namespace hwio::diag

namespace hwio::gui {

class AppViewModel;

class StartupCheck : public QObject {
    Q_OBJECT

public:
    enum class Decision : std::uint8_t { None, Welcome, JLinkNeeded };

    // The ADR-0071 decision table.
    [[nodiscard]] static Decision decide(bool completed, bool jlinkOk, bool showAtStart);

    StartupCheck(AppViewModel& app, QObject* parent);

    void run();
    // The decision once made (tests); nullopt while the check runs or before run().
    [[nodiscard]] std::optional<Decision> decision() const noexcept { return m_decision; }

signals:
    void decided();

private:
    void onFinished(quint64 generation, const std::vector<diag::CheckResult>& results);
    void openWizard();
    [[nodiscard]] bool completed() const;

    AppViewModel& m_app;
    std::uint64_t m_requester; // DiagWorker::newRequester (ADR-0394)
    std::uint64_t m_generation = 0;
    bool m_jlinkOk = true;
    bool m_waiting = false; // for another wizard to close (once)
    QMetaObject::Connection m_waitConnection;
    std::optional<Decision> m_decision;
};

} // namespace hwio::gui
