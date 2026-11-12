#pragma once

// RemoteShareViewModel - sharing a device tab's console with HARDWARIO Support (plan/features/0044
// § Host, ADR-0334, ADR-0335, ADR-0324): the Share dialog texts, the banner with the code, the join
// approval and the operation confirmation (it is the GUI's remote::ConsentProvider), the idle
// warning. It owns the RemoteHost (and, for a J-Link console, its DeviceSessionOperations) while
// sharing. Sharing ends when the tab's session changes (disconnect, another device, tab close,
// quit: DeviceTabViewModel::sessionChanged), and on End sharing.
//
// Thin for QML: the questions are open while joinQuestionOpen / operationQuestionOpen; QML shows
// them as modal dialogs whose default button refuses (End session / Deny) and calls answerJoin()
// / answerOperation(). A withdrawn question (timeout, session end) closes the dialog.
//
// Plan 0044 (ADR-0363): it is also the host's remote::LteUpdateRunner: an allowed lteUpdate opens
// Flash › Update LTE firmware… on the tab (the wizard takes the request with
// takeLteUpdateRequest() and starts with the recommended firmware); the request's reporter stays
// here, the wizard keeps it as a QPointer.
//
// Ownership: child of its DeviceTabViewModel (Qt parent). GUI thread only.

#include "hwio/device/command_tree.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

#include <memory>
#include <optional>

namespace hwio::remote {
class DeviceSessionOperations;
class LteUpdateReporter;
class RemoteHost;
} // namespace hwio::remote

namespace hwio::gui {

class DeviceTabViewModel;

class RemoteShareViewModel : public QObject,
                             public remote::ConsentProvider,
                             public remote::LteUpdateRunner {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel")

    // Share with support… is possible: a connected J-Link or Bluetooth console, not a remote tab.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // "none" | "connecting" | "sharing" | "reconnecting"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool sharing READ sharing NOTIFY changed) // connecting, sharing or reconnecting
    Q_PROPERTY(QString code READ code NOTIFY changed)    // "482 913" (grouped)
    Q_PROPERTY(QString link READ link NOTIFY changed)
    Q_PROPERTY(QString relayHost READ relayHost NOTIFY changed)
    // The Share dialog's explanation (the relay host name, the limits, the warning).
    Q_PROPERTY(QString shareExplanation READ shareExplanation NOTIFY changed)
    Q_PROPERTY(bool participantAllowed READ participantAllowed NOTIFY changed)
    Q_PROPERTY(bool joinQuestionOpen READ joinQuestionOpen NOTIFY questionsChanged)
    Q_PROPERTY(QString joinQuestionText READ joinQuestionText NOTIFY questionsChanged)
    Q_PROPERTY(bool operationQuestionOpen READ operationQuestionOpen NOTIFY questionsChanged)
    Q_PROPERTY(QString operationQuestionText READ operationQuestionText NOTIFY questionsChanged)
    Q_PROPERTY(bool operationRunning READ operationRunning NOTIFY changed)
    Q_PROPERTY(QString operationText READ operationText NOTIFY changed)
    Q_PROPERTY(bool idleWarning READ idleWarning NOTIFY changed)
    // The last end reason ("" after End sharing or while sharing).
    Q_PROPERTY(QString endMessage READ endMessage NOTIFY changed)

public:
    explicit RemoteShareViewModel(DeviceTabViewModel* tab);
    ~RemoteShareViewModel() override;
    RemoteShareViewModel(const RemoteShareViewModel&) = delete;
    RemoteShareViewModel& operator=(const RemoteShareViewModel&) = delete;
    RemoteShareViewModel(RemoteShareViewModel&&) = delete;
    RemoteShareViewModel& operator=(RemoteShareViewModel&&) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] bool sharing() const { return m_host != nullptr; }
    [[nodiscard]] QString code() const;
    [[nodiscard]] QString link() const;
    [[nodiscard]] QString relayHost() const;
    [[nodiscard]] QString shareExplanation() const;
    [[nodiscard]] bool participantAllowed() const;
    [[nodiscard]] bool joinQuestionOpen() const { return static_cast<bool>(m_joinAnswer); }
    [[nodiscard]] QString joinQuestionText() const { return m_joinText; }
    [[nodiscard]] bool operationQuestionOpen() const {
        return static_cast<bool>(m_operationAnswer);
    }
    [[nodiscard]] QString operationQuestionText() const { return m_operationText; }
    [[nodiscard]] bool operationRunning() const { return m_operationRunning; }
    [[nodiscard]] QString operationText() const { return m_operationStatus; }
    [[nodiscard]] bool idleWarning() const { return m_idleWarning; }
    [[nodiscard]] QString endMessage() const { return m_endMessage; }

    // [Start sharing] in the Share dialog.
    Q_INVOKABLE void startSharing();
    // [End sharing].
    Q_INVOKABLE void endSharing();
    Q_INVOKABLE void copyLink();
    Q_INVOKABLE void copyCode();
    Q_INVOKABLE void keepSharing();
    Q_INVOKABLE void answerJoin(bool allowed);
    Q_INVOKABLE void answerOperation(bool allowed);

    // remote::ConsentProvider
    [[nodiscard]] Withdraw askJoin(const remote::JoinQuestion& question, Answer answer) override;
    [[nodiscard]] Withdraw askOperation(const remote::OperationQuestion& question,
                                        Answer answer) override;

    // remote::LteUpdateRunner (ADR-0363). Errors: internal.busy (the tab is busy, another wizard
    // is open).
    [[nodiscard]] Status start(Progress progress, Finished finished) override;
    void abandon() override;
    // ADR-0364: the tab's learned command tree for support's autocomplete (ConsoleViewModel, on
    // every change; nullptr = none). Kept for the next sharing too.
    void setCommandTree(std::shared_ptr<const device::CommandTree> tree);
    // The LTE update wizard takes the open request (once); nullptr when there is none.
    [[nodiscard]] remote::LteUpdateReporter* takeLteUpdateRequest();

signals:
    void changed();
    void questionsChanged();
    // ADR-0364: a participant's autocomplete needs the subcommands of `path` (ConsoleViewModel
    // learns them with its silent `<command> -h`).
    void learnRequested(const QStringList& path);

private:
    void stopSharing(bool notify);

    DeviceTabViewModel* m_tab; // the parent
    std::unique_ptr<remote::DeviceSessionOperations> m_operations;
    std::unique_ptr<remote::RemoteHost> m_host; // declared after the operations it uses
    Answer m_joinAnswer;
    QString m_joinText;
    Answer m_operationAnswer;
    QString m_operationText;
    bool m_operationRunning = false;
    QString m_operationStatus;
    bool m_idleWarning = false;
    bool m_reconnecting = false;
    QString m_endMessage;
    std::unique_ptr<remote::LteUpdateReporter> m_lteUpdate; // the last remote LTE update
    bool m_lteUpdateOffered = false; // start() waits for the wizard to take it
    std::shared_ptr<const device::CommandTree> m_commandTree;
};

} // namespace hwio::gui
