#pragma once

// RemoteJoinViewModel - the joiner side of a remote support session in a remote device tab
// (plan/features/0044 § Joiner, ADR-0325, ADR-0336): the banner state (waiting for the customer,
// joined, ended, idle) and the remote operations of the Flash page (catalog firmware ID only,
// erase, recover, reset; each confirmed by the customer, results as reported by the host). Also the
// validation of the Join dialog's input (static helpers used by AppViewModel).
//
// Ownership: child of its DeviceTabViewModel (Qt parent). GUI thread only.

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <memory>

namespace hwio::remote {
class RemoteOperationClient;
} // namespace hwio::remote

namespace hwio::gui {

class DeviceTabViewModel;

class RemoteJoinViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by DeviceTabViewModel")

    Q_PROPERTY(bool active READ active NOTIFY changed) // a remote tab
    // "connecting" | "waiting" | "joined" | "ended" | "idle"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString bannerText READ bannerText NOTIFY changed)
    // The host is HARDWARIO Monitor with operations (the Flash page offers them).
    Q_PROPERTY(bool operationsAvailable READ operationsAvailable NOTIFY changed)
    Q_PROPERTY(QString operationsUnavailableText READ operationsUnavailableText NOTIFY changed)
    // ADR-0363: the host offers the guided LTE update (its hello's opKinds).
    Q_PROPERTY(bool lteUpdateAvailable READ lteUpdateAvailable NOTIFY changed)
    Q_PROPERTY(bool operationPending READ operationPending NOTIFY operationChanged)
    Q_PROPERTY(QString operationStatus READ operationStatus NOTIFY operationChanged)
    Q_PROPERTY(bool operationFailed READ operationFailed NOTIFY operationChanged)
    // 0..1, -1 = indeterminate.
    Q_PROPERTY(double operationProgress READ operationProgress NOTIFY operationChanged)
    // ADR-0368: the customer's Monitor asks support (the LTE update's Continue, Continue without a
    // copy); "" when no question is open. answerQuestion() answers it.
    Q_PROPERTY(QString operationQuestion READ operationQuestion NOTIFY operationChanged)

public:
    explicit RemoteJoinViewModel(DeviceTabViewModel* tab);
    ~RemoteJoinViewModel() override;
    RemoteJoinViewModel(const RemoteJoinViewModel&) = delete;
    RemoteJoinViewModel& operator=(const RemoteJoinViewModel&) = delete;
    RemoteJoinViewModel(RemoteJoinViewModel&&) = delete;
    RemoteJoinViewModel& operator=(RemoteJoinViewModel&&) = delete;

    [[nodiscard]] bool active() const;
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString bannerText() const;
    [[nodiscard]] bool operationsAvailable() const;
    [[nodiscard]] QString operationsUnavailableText() const;
    [[nodiscard]] bool lteUpdateAvailable() const;
    [[nodiscard]] bool operationPending() const;
    [[nodiscard]] QString operationStatus() const { return m_status; }
    [[nodiscard]] bool operationFailed() const { return m_failed; }
    [[nodiscard]] double operationProgress() const { return m_progress; }
    [[nodiscard]] QString operationQuestion() const;
    Q_INVOKABLE void answerQuestion(bool yes);

    // `kind`: "flash" (with `firmwareId`), "erase", "recover", "reset", "lteUpdate" (ADR-0363,
    // `target` ignored); `target` "app" | "lte".
    Q_INVOKABLE void requestOperation(const QString& kind, const QString& target,
                                      const QString& firmwareId = QString());
    // A valid catalog firmware ID (32 hex digits) for the Flash page's field.
    Q_INVOKABLE [[nodiscard]] static bool isFirmwareId(const QString& text);

    // The Join dialog: {valid, code ("482 913"), foreignRelay ("" or the link's server), error}.
    [[nodiscard]] static QVariantMap checkJoinInput(const QString& text, const QUrl& configured);

signals:
    void changed();
    void operationChanged();

private:
    void attach();

    DeviceTabViewModel* m_tab; // the parent
    std::unique_ptr<remote::RemoteOperationClient> m_client;
    bool m_idle = false;
    QString m_status;
    bool m_failed = false;
    double m_progress = -1.0;
};

} // namespace hwio::gui
