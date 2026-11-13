#include "remote_commands.h"

#include "ble_commands.h"
#include "chester_commands.h"
#include "cli_app.h"
#include "cli_log.h"
#include "command_collector.h"
#include "console_learning.h"
#include "console_ui.h"
#include "guided_lte.h"
#include "hwio/core/error.h"
#include "hwio/core/json.h"
#include "hwio/core/result.h"
#include "hwio/core/settings.h"
#include "hwio/core/version.h"
#include "hwio/device/command_tree.h"
#include "hwio/device/command_tree_cache.h"
#include "hwio/device/command_tree_learner.h"
#include "hwio/device/command_tree_source.h"
#include "hwio/device/completer.h"
#include "hwio/device/console_line.h"
#include "hwio/device/console_log.h"
#include "hwio/device/console_session.h"
#include "hwio/device/device_session.h"
#include "hwio/device/lte_update_procedure.h"
#include "hwio/device/profile.h"
#include "hwio/device/session_registry.h"
#include "hwio/firmware/firmware_cache.h"
#include "hwio/firmware/firmware_catalog.h"
#include "hwio/hw/nrfutil/nrfutil.h"
#include "hwio/remote/consent.h"
#include "hwio/remote/host_operations.h"
#include "hwio/remote/lte_update_report.h"
#include "hwio/remote/remote_console_session.h"
#include "hwio/remote/remote_host.h"
#include "hwio/remote/remote_operation_client.h"
#include "hwio/remote/session_code.h"
#include "hwio/remote/tunnel.h"
#include "jlink_commands.h"
#include "output.h"
#include "probe_options.h"
#include "progress_ui.h"
#include "terminal.h"

#include <QEventLoop>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringLiteral>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QtContainerFwd>
#include <QtEnvironmentVariables>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hwio::cli {

namespace {

namespace fs = std::filesystem;
namespace tunnel = remote::tunnel;
using namespace std::chrono_literals;

// The customer has 2 minutes to allow a join (ADR-0335), plus a margin.
constexpr std::chrono::milliseconds allowWait{150000};
// After the sync, a Monitor host's hello follows at once.
constexpr std::chrono::milliseconds helloWait{5000};
constexpr std::chrono::milliseconds relayConnectWait{20000};

struct HostCommandOptions {
    ProbeOptions probe;
    bool ble = false;
    BleDeviceSelection bleDevice;
    int latencyMs = 0;
    std::string consoleFile;
    bool consoleFileGiven = false;
    bool autoReconnect = false;
    std::string relay;
    bool denyOperations = false;
};

struct JoinCommandOptions {
    std::string target; // code or link
    std::string relay;
    std::string command;
    double timeoutSeconds = 3.0;
    std::string flash;
    bool erase = false;
    bool recover = false;
    bool reset = false;
    bool lteUpdate = false; // ADR-0363
    std::string core = "app";
    double lingerSeconds = 1.0;
};

void status(const Context& context, const std::string& text) {
    qCInfo(lcCli).noquote() << toQString(text);
    if (context.mode == OutputMode::Human) {
        std::cerr << text << '\n';
    }
}

void event(const Context& context, QJsonObject object) {
    if (context.mode == OutputMode::Json && context.output != nullptr) {
        object.insert(QStringLiteral("type"), QStringLiteral("remote"));
        context.output->printEvent(object, {});
    }
}

Result<QUrl> relayOf(const std::string& option) {
    if (!option.empty()) {
        return remote::parseRelayUrl(option);
    }
    auto settings = Settings::openDefault();
    return remote::configuredRelayUrl(settings ? &*settings : nullptr);
}

// Runs the event loop until `done` or the stop request (cancelled) or `timeout` (nullopt = none).
Status runUntil(const std::function<bool()>& done, std::optional<std::chrono::milliseconds> timeout,
                const std::stop_token& stop, ErrorCode timeoutCode = ErrorCode::Timeout) {
    const auto deadline =
        timeout ? std::optional(std::chrono::steady_clock::now() + *timeout) : std::nullopt;
    QEventLoop loop;
    while (!done()) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::Cancelled);
        }
        if (deadline && std::chrono::steady_clock::now() >= *deadline) {
            return std::unexpected(makeError(timeoutCode, "remote session wait"));
        }
        QTimer::singleShot(25, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return {};
}

// ---- host ---------------------------------------------------------------------------------------

// A terminal on stdin (ADR-0335: joins can be allowed).
bool hostInteractive() {
#ifdef HWIO_TEST_OVERRIDES
    // Test builds only (as `lte flash --guided`): the end-to-end tests answer through a pipe.
    if (qEnvironmentVariableIsSet("HWIO_TEST_INTERACTIVE")) {
        return true;
    }
#endif
    return stdinIsTerminal();
}

// The host's limits (defaults). Test builds only: HWIO_TEST_OPERATION_TIMEOUT_MS shortens the
// operation timeout (support's answer to an LTE update question) for the end-to-end tests.
remote::HostLimits hostLimits() {
    remote::HostLimits limits;
#ifdef HWIO_TEST_OVERRIDES
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("HWIO_TEST_OPERATION_TIMEOUT_MS", &ok);
    if (ok && ms > 0) {
        limits.operationTimeout = std::chrono::milliseconds(ms);
    }
#endif
    return limits;
}

// The CLI's ConsentProvider: the question on stderr, the answer is the next stdin line (y / yes
// allows). The host loop feeds the lines (answer()).
class CliConsent final : public remote::ConsentProvider {
public:
    CliConsent(const Context& context, bool denyOperations)
        : m_context(&context), m_denyOperations(denyOperations) {}

    [[nodiscard]] Withdraw askJoin(const remote::JoinQuestion& question, Answer answer) override {
        return ask(toStdString(remote::joinQuestionText(question)), "join", std::move(answer),
                   "[y/N, Enter = end the session]");
    }
    [[nodiscard]] Withdraw askOperation(const remote::OperationQuestion& question,
                                        Answer answer) override {
        if (m_denyOperations) {
            status(*m_context, "Operation request denied (--deny-operations).");
            answer(false);
            return {};
        }
        return ask(toStdString(remote::operationQuestionText(question)), "operation",
                   std::move(answer), "[y/N]");
    }

    // ADR-0363: a y/N question of the guided LTE update (no question may be open: then no).
    void askQuestion(const std::string& text, Answer answer) {
        if (m_pending) {
            answer(false);
            return;
        }
        (void)ask(text, "lte-update", std::move(answer), "[y/N]");
    }

    // True when the line answered a question.
    bool answer(const std::string& line) {
        if (!m_pending) {
            return false;
        }
        std::string lower;
        for (const char c : line) {
            if (c != ' ') {
                lower += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
        }
        const bool allowed = lower == "y" || lower == "yes";
        Answer pending = std::exchange(m_pending, {});
        pending(allowed);
        return true;
    }

private:
    Withdraw ask(const std::string& text, const char* kind, Answer answer, const char* choices) {
        m_pending = std::move(answer);
        QJsonObject question;
        question.insert(QStringLiteral("event"), QStringLiteral("question"));
        question.insert(QStringLiteral("kind"), QString::fromLatin1(kind));
        question.insert(QStringLiteral("text"), toQString(text));
        event(*m_context, question);
        std::cerr << text << ' ' << choices << '\n' << std::flush;
        qCInfo(lcCli).noquote() << "Asked:" << toQString(text);
        return [this] {
            if (m_pending) {
                m_pending = {};
                status(*m_context, "The question was withdrawn (no answer in time).");
            }
        };
    }

    const Context* m_context;
    bool m_denyOperations;
    Answer m_pending;
};

// Denies every operation request; joins as `joins`.
class DenyOperations final : public remote::ConsentProvider {
public:
    explicit DenyOperations(remote::ConsentProvider& joins) : m_joins(&joins) {}
    [[nodiscard]] Withdraw askJoin(const remote::JoinQuestion& question, Answer answer) override {
        return m_joins->askJoin(question, std::move(answer));
    }
    [[nodiscard]] Withdraw askOperation(const remote::OperationQuestion& /*question*/,
                                        Answer answer) override {
        answer(false);
        return {};
    }

private:
    remote::ConsentProvider* m_joins;
};

firmware::CatalogOptions chesterCatalogOptions() {
    const device::DeviceProfile* profile = device::ProfileRegistry::find("chester");
    return firmware::CatalogOptions{.baseUrl =
                                        firmware::catalogBaseUrl(profile->firmwareCatalogUrl),
                                    .transferTimeout = std::chrono::seconds(60),
                                    .maxBodyBytes = 32ULL * 1024 * 1024,
                                    .lookupTimeout = std::chrono::seconds(15),
                                    .maxPageBytes = 1024ULL * 1024};
}

// The shared console session of `remote host`: J-Link by default, BLE with --ble. The members
// are declared in destruction order.
struct HostSession {
    std::unique_ptr<device::SessionRegistry> registry;
    std::unique_ptr<device::DeviceSession> jlinkSession;
    std::optional<OpenedBleSession> bleSession;
    device::ConsoleSession* session = nullptr;
    std::chrono::milliseconds connectTimeout{95000};
};

Result<HostSession> openHostSession(const Context& context, const GlobalOptions& globals,
                                    const ProbeOptions& group, const HostCommandOptions& options) {
    HostSession opened;
    if (options.ble) {
        auto ble = openBleSession(
            context, options.bleDevice, options.autoReconnect,
            options.consoleFileGiven
                ? std::optional<fs::path>(fs::path(toQString(options.consoleFile).toStdU16String()))
                : std::nullopt,
            [&context](std::string_view text) { status(context, std::string(text)); });
        if (!ble) {
            return std::unexpected(std::move(ble).error());
        }
        opened.bleSession.emplace(std::move(*ble));
        opened.session = opened.bleSession->session.get();
        status(context, std::format("Connecting to {}...", opened.bleSession->description));
        return opened;
    }
    auto location = locateJLinkLibrary(globals);
    if (!location) {
        return std::unexpected(std::move(location).error());
    }
    auto sessionOptions =
        appConsoleSessionOptions(group, options.probe, options.latencyMs, options.autoReconnect,
                                 false, options.consoleFile, options.consoleFileGiven);
    if (!sessionOptions) {
        return std::unexpected(std::move(sessionOptions).error());
    }
    // ADR-0363: a remote LTE update uses --nrfutil (as `chester lte flash`).
    if (!globals.nrfutil.empty()) {
        auto settings = Settings::openDefault();
        sessionOptions->nrfUtil = nrfutil::defaultLocatorOptions(
            fs::path(toQString(globals.nrfutil).toStdU16String()), settings ? &*settings : nullptr);
    }
    opened.connectTimeout = device::connectTimeoutForSpeed(sessionOptions->speedKhz);
    opened.registry = std::make_unique<device::SessionRegistry>();
    auto jlink =
        device::DeviceSession::open(*opened.registry, *location, std::move(*sessionOptions));
    if (!jlink) {
        return std::unexpected(std::move(jlink).error());
    }
    opened.jlinkSession = std::move(*jlink);
    opened.session = opened.jlinkSession.get();
    status(context, "Connecting to the CHESTER application core...");
    return opened;
}

// The status lines and JSON events of `remote host`.
void connectHostStatus(const Context& context, remote::RemoteHost& host) {
    QObject::connect(&host, &remote::RemoteHost::sharingStarted, &host, [&context, &host] {
        status(context,
               std::format("Sharing with support. Session code: {}", host.code().grouped()));
        status(context, std::format("Link: {}", toStdString(host.link().toString())));
        status(context, "Read the code only to HARDWARIO Support: anyone who knows it sees the "
                        "console output. Press Ctrl+C to end sharing.");
        QJsonObject sharing;
        sharing.insert(QStringLiteral("event"), QStringLiteral("sharing"));
        sharing.insert(QStringLiteral("code"), toQString(host.code().text()));
        sharing.insert(QStringLiteral("link"), host.link().toString());
        sharing.insert(QStringLiteral("relay"), host.relay().host());
        event(context, sharing);
    });
    QObject::connect(&host, &remote::RemoteHost::idleWarning, &host,
                     [&context](std::chrono::seconds remaining) {
                         status(context, std::format("Sharing ends in {} s without activity. "
                                                     "Press Enter to keep sharing.",
                                                     remaining.count()));
                     });
    QObject::connect(&host, &remote::RemoteHost::connectionLost, &host, [&context] {
        status(context, "Connection to the support server lost, reconnecting...");
    });
    QObject::connect(&host, &remote::RemoteHost::warning, &host, [](const Error& error) {
        qCWarning(lcCli).noquote()
            << "Remote:" << toQString(error.message) << toQString(error.detail);
    });
    QObject::connect(
        &host, &remote::RemoteHost::operationStarted, &host,
        [&context](const QString& summary) { status(context, toStdString(summary) + "..."); });
    QObject::connect(
        &host, &remote::RemoteHost::operationFinished, &host,
        [&context](bool /*ok*/, const QString& message) { status(context, toStdString(message)); });
}

// Reads the host's pending stdin lines: consent answers, else keep-alive input.
void pumpHostInput(StdinLineReader* reader, bool& inputEnded, CliConsent& consent,
                   remote::RemoteHost& host) {
    while (reader != nullptr && !inputEnded) {
        auto item = reader->poll();
        if (!item) {
            return;
        }
        if (!*item) {
            inputEnded = true; // Ctrl+D: keep sharing until Ctrl+C
            return;
        }
        if (!consent.answer(**item)) {
            host.keepSharing(); // ADR-0328: any input extends idle
        }
    }
}

// ADR-0363: a remote lteUpdate runs the terminal flow of `chester lte flash --guided` on the shared
// session with the designated LTE firmware; the customer follows its prompts on stderr and answers
// its y/N question like the other questions (stdin). Started on the next turn of the event loop
// (the download runs a nested loop). Owner thread only.
class CliLteUpdate final : public remote::LteUpdateRunner {
public:
    CliLteUpdate(const Context& context, const GlobalOptions& globals,
                 device::DeviceSession& session, CliConsent& consent)
        : m_context(&context), m_globals(&globals), m_session(&session), m_consent(&consent) {}
    ~CliLteUpdate() override { abandon(); }
    CliLteUpdate(const CliLteUpdate&) = delete;
    CliLteUpdate& operator=(const CliLteUpdate&) = delete;
    CliLteUpdate(CliLteUpdate&&) = delete;
    CliLteUpdate& operator=(CliLteUpdate&&) = delete;

    [[nodiscard]] Status start(Progress progress, Finished finished,
                               remote::AskSupport ask) override {
        if (m_reporter && !m_reporter->done()) {
            return std::unexpected(makeError(ErrorCode::InternalBusy, "an LTE update runs"));
        }
        m_terminal.reset();
        m_procedure.reset();
        m_reporter =
            std::make_unique<remote::LteUpdateReporter>(std::move(progress), std::move(finished));
        m_reporter->setAskSupport(std::move(ask)); // ADR-0368: support answers when it can
        QTimer::singleShot(0, m_reporter.get(), [this] { run(); });
        return {};
    }

    void abandon() override {
        if (m_reporter) {
            m_reporter->abandon(); // the guided update goes on in the terminal, unreported
        }
    }

private:
    void run() {
        m_ui = std::make_unique<ProgressUi>(*m_context, std::cerr, stderrIsTerminal());
        auto source = designatedLteSource(*m_context, *m_globals, *m_ui,
                                          [this](std::uint64_t done, std::uint64_t total) {
                                              if (m_reporter) {
                                                  m_reporter->download(done, total);
                                              }
                                          });
        if (!m_reporter || m_reporter->done()) {
            return; // the session ended meanwhile
        }
        if (!source) {
            m_ui->warning(source.error());
            m_reporter->finish(std::unexpected(std::move(source).error()));
            return;
        }
        m_procedure = std::make_unique<device::LteUpdateProcedure>(
            *m_session, device::LteUpdateProcedure::Options{.pollInterval = std::chrono::seconds(1),
                                                            .backupDirectory = {},
                                                            .autoRestore = true,
                                                            .now = {}});
        m_terminal = std::make_unique<GuidedTerminal>(
            *m_context, *m_ui, *m_procedure,
            [this](const std::string& question, std::function<void(bool)> answer) {
                // CliConsent adds its own "[y/N]".
                std::string text = question;
                if (const std::string suffix = " [y/N] "; text.ends_with(suffix)) {
                    text.resize(text.size() - suffix.size());
                }
                m_consent->askQuestion(text, std::move(answer));
            });
        // ADR-0368: support answers the questions; the customer only follows the prompts.
        m_terminal->setSupportDecides(m_reporter->supportDecides());
        m_reporter->follow(*m_procedure);
        // The terminal flow does not retry: every end is the result.
        QObject::connect(
            m_procedure.get(), &device::LteUpdateProcedure::finished, m_reporter.get(), [this] {
                m_ui->finish();
                if (const QString text = m_reporter->cancelledText(); !text.isEmpty()) {
                    m_ui->info(toStdString(text)); // ADR-0368: support's No / none
                }
                m_reporter->finish();
            });
        if (Status started = m_procedure->start(*source); !started) {
            m_ui->warning(started.error());
            m_reporter->finish(std::move(started));
            return;
        }
        m_terminal->show();
    }

    const Context* m_context;
    const GlobalOptions* m_globals;
    device::DeviceSession* m_session;
    CliConsent* m_consent;
    std::unique_ptr<ProgressUi> m_ui;
    std::unique_ptr<remote::LteUpdateReporter> m_reporter;
    std::unique_ptr<device::LteUpdateProcedure> m_procedure;
    std::unique_ptr<GuidedTerminal> m_terminal; // declared after the procedure it follows
};

Result<CommandResult> host(const Context& context, const GlobalOptions& globals,
                           const ProbeOptions& group, const HostCommandOptions& options) {
    auto relay = relayOf(options.relay);
    if (!relay) {
        return std::unexpected(std::move(relay).error());
    }
    // The console's own stop request (Ctrl+C / SIGTERM, a closed stdout).
    std::stop_source consoleStop;
    const std::stop_callback forwardStop(context.stop,
                                         [&consoleStop] { consoleStop.request_stop(); });
    Context hostContext = context;
    hostContext.stop = consoleStop.get_token();

    auto opened = openHostSession(context, globals, group, options);
    if (!opened) {
        return std::unexpected(std::move(opened).error());
    }
    HostSession& hostSession = *opened;
    device::ConsoleSession* session = hostSession.session;
    const std::unique_ptr<device::DeviceSession>& jlinkSession = hostSession.jlinkSession;
    const std::chrono::milliseconds connectTimeout = hostSession.connectTimeout;
    ConsoleView view(hostContext, *session, false, jlinkSession != nullptr);
    view.setOutputClosedHandler([&consoleStop] { consoleStop.request_stop(); });
    if (const Status connected = waitForConsoleConnection(hostContext, *session, view,
                                                          options.autoReconnect, connectTimeout);
        !connected) {
        return std::unexpected(connected.error());
    }

    // ADR-0335: without a terminal on stdin no join is ever allowed.
    const bool interactive = hostInteractive();
    CliConsent cliConsent(hostContext, options.denyOperations);
    remote::AutoDenyConsent autoDeny;
    remote::ConsentProvider& joins =
        interactive ? static_cast<remote::ConsentProvider&>(cliConsent) : autoDeny;
    DenyOperations denyOperations(joins);
    remote::ConsentProvider& consent =
        options.denyOperations ? static_cast<remote::ConsentProvider&>(denyOperations) : joins;
    if (!interactive) {
        status(context, "No terminal on stdin: joins cannot be allowed; the session ends at the "
                        "first join.");
    }

    // Operations: J-Link only (a BLE console has none). Declared after what they use.
    firmware::FirmwareCache cache(firmware::FirmwareCache::defaultDirectory());
    std::optional<CliLteUpdate> lteUpdate;
    std::unique_ptr<remote::DeviceSessionOperations> operations;
    if (jlinkSession) {
        operations = std::make_unique<remote::DeviceSessionOperations>(
            *jlinkSession, chesterCatalogOptions(), &cache);
        // ADR-0363: the guided LTE update needs the customer at the terminal.
        if (interactive && !options.denyOperations) {
            lteUpdate.emplace(hostContext, globals, *jlinkSession, cliConsent);
            operations->setLteUpdateRunner(&*lteUpdate);
        }
    }

    remote::RemoteHost host(*session, consent, operations.get(),
                            remote::HostOptions{.relay = *relay,
                                                .limits = hostLimits(),
                                                .appVersion = std::string(buildInfo().version),
                                                .code = std::nullopt});
    // ADR-0364: the host learns the command tree as `chester app console` does (`info show`, the
    // cached tree or `help`) and gives it to the session; a participant's learn request runs its
    // silent `<command> -h`.
    std::optional<ConsoleLearning> learning;
    {
        auto settings = Settings::openDefault();
        learning.emplace(*session, session->profile().id, session->consoleTargetId(),
                         !settings || settings->get(device::readCommandsOnConnectSetting),
                         device::CommandTreeCache::defaultDirectory());
    }
    device::CommandTreeLearner& learner = learning->learner();
    QObject::connect(&learner, &device::CommandTreeLearner::treeChanged, &host,
                     [&host, &learner] { host.setCommandTree(learner.tree()); });
    QObject::connect(&host, &remote::RemoteHost::learnRequested, &learner,
                     [&learner](const QStringList& path) {
                         std::vector<std::string> words;
                         for (const QString& word : path) {
                             words.push_back(toStdString(word));
                         }
                         learner.requestChildren(words);
                     });
    host.setCommandTree(learner.tree());
    bool ended = false;
    connectHostStatus(context, host);
    QObject::connect(&host, &remote::RemoteHost::ended, &host, [&] { ended = true; });
    status(context,
           std::format("Connecting to the support server {}...", toStdString(relay->host())));
    host.start();

    std::unique_ptr<StdinLineReader> reader;
    if (interactive) {
        reader = std::make_unique<StdinLineReader>();
    }
    bool inputEnded = false;
    const Status loop = runUntil(
        [&] {
            pumpHostInput(reader.get(), inputEnded, cliConsent, host);
            return ended || view.fatalError().has_value();
        },
        std::nullopt, hostContext.stop);
    if (!ended) {
        host.end();
    }
    const std::optional<Error> hostError = host.endError();
    QJsonObject endEvent;
    endEvent.insert(QStringLiteral("event"), QStringLiteral("ended"));
    endEvent.insert(QStringLiteral("error"),
                    hostError ? QJsonValue(toJson(*hostError)) : QJsonValue(QJsonValue::Null));
    event(context, endEvent);
    status(context, hostError ? "Sharing ended: " + hostError->message : "Sharing ended.");
    operations.reset();
    learning.reset(); // its queries leave the session before it closes
    session->close();
    view.printConsoleFile(session->consoleFile());
    view.printStopped();
    if (view.fatalError()) {
        return std::unexpected(*view.fatalError());
    }
    if (!loop) {
        if (loop.error().code == ErrorCode::Cancelled && context.mode == OutputMode::Human) {
            // Ctrl+C is the normal end of sharing: "Sharing ended." above is the whole human
            // output, the exit status stays cancelled. --json: the console's error envelope.
            return CommandResult{
                .data = QJsonValue(), .humanText = {}, .streamed = false, .failure = loop.error()};
        }
        return std::unexpected(loop.error()); // Ctrl+C: cancelled, as the console
    }
    if (hostError) {
        return std::unexpected(*hostError);
    }
    return CommandResult{.data = QJsonValue(), .humanText = {}, .streamed = true};
}

// ---- join ---------------------------------------------------------------------------------------

// ADR-0364: the interactive joiner completes from the host's command tree; a command's
// subcommands are asked from the host (tunnel `learn`). A web Terminal host sends none.
class RemoteTreeSource final : public device::CommandTreeSource {
public:
    explicit RemoteTreeSource(remote::RemoteConsoleSession& session) : m_session(&session) {}
    [[nodiscard]] std::shared_ptr<const device::CommandTree> tree() const override {
        return m_session->commandTree();
    }
    void requestTopLevel() override {} // the host sends its tree by itself
    void requestChildren(const std::vector<std::string>& path) override {
        m_session->requestLearn(path);
    }

private:
    remote::RemoteConsoleSession* m_session; // non-owning, outlives this object
};

std::optional<tunnel::OpKind> requestedOperation(const JoinCommandOptions& options) {
    if (!options.flash.empty()) {
        return tunnel::OpKind::Flash;
    }
    if (options.erase) {
        return tunnel::OpKind::Erase;
    }
    if (options.recover) {
        return tunnel::OpKind::Recover;
    }
    if (options.reset) {
        return tunnel::OpKind::Reset;
    }
    if (options.lteUpdate) {
        return tunnel::OpKind::LteUpdate;
    }
    return std::nullopt;
}

Result<QUrl> joinRelay(const Context& context, const JoinCommandOptions& options,
                       const remote::JoinTarget& target) {
    auto configured = relayOf(options.relay);
    if (!configured) {
        return std::unexpected(std::move(configured).error());
    }
    if (!target.relay || remote::sameRelay(*target.relay, *configured)) {
        return *configured;
    }
    // ADR-0330: a link to another server never redirects a joiner silently.
    const std::string other = toStdString(target.relay->toString());
    if (context.mode == OutputMode::Human && stdinIsTerminal() &&
        askYesNo(std::format("This link uses another server: {}. Join anyway?", other))) {
        return *target.relay;
    }
    Error error = makeError(ErrorCode::UsageInvalidArgument, other);
    error.message = std::format("The link uses another support server: {}.", other);
    error.suggestion = "Join with the code only, or pass --relay with this server if you trust it.";
    return std::unexpected(std::move(error));
}

Status waitAllowed(const Context& context, remote::RemoteConsoleSession& session) {
    status(context, "Waiting for the customer to allow you...");
    const Status allowed = runUntil(
        [&] {
            return session.joinState() == remote::JoinState::Allowed ||
                   session.joinState() == remote::JoinState::Ended;
        },
        allowWait, context.stop, ErrorCode::RemoteJoinNotAllowed);
    if (!allowed) {
        return allowed;
    }
    if (session.joinState() == remote::JoinState::Ended) {
        return std::unexpected(session.endError().value_or(makeError(ErrorCode::RemoteHostEnded)));
    }
    status(context, "The customer allowed you.");
    return {};
}

// ADR-0368: `remote join` answers the questions of a remote LTE update on stdin (y/N) when stdin
// is a terminal; the reader starts with the first question. Owner thread only.
class JoinQuestions {
public:
    JoinQuestions(const Context& context, remote::RemoteOperationClient& client, ProgressUi& ui)
        : m_context(&context), m_client(&client), m_ui(&ui) {
        if (!hostInteractive()) {
            return;
        }
        client.setAnswersQuestions(true);
        QObject::connect(&client, &remote::RemoteOperationClient::question, &client,
                         [this](tunnel::OpQuestionKind kind) { ask(kind); });
    }

    // Answers the open question with a line typed meanwhile (y / yes: continue).
    void poll() {
        if (!m_reader || !m_client->openQuestion()) {
            return;
        }
        const auto line = m_reader->poll();
        if (!line) {
            return; // nothing typed yet
        }
        std::string lower = line->value_or("");
        std::ranges::transform(lower, lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool yes = line->has_value() && (lower == "y" || lower == "yes");
        if (Status sent = m_client->answer(yes); !sent) {
            m_ui->warning(sent.error());
            return;
        }
        status(*m_context, yes ? "Answered: continue." : "Answered: cancel.");
    }

private:
    void ask(tunnel::OpQuestionKind kind) {
        m_ui->finish();
        const QString text = remote::operationQuestionKindText(kind);
        QJsonObject question;
        question.insert(QStringLiteral("event"), QStringLiteral("question"));
        question.insert(QStringLiteral("kind"), toQString(tunnel::questionId(kind)));
        question.insert(QStringLiteral("text"), text);
        event(*m_context, question);
        std::cerr << toStdString(text) << " [y/N] " << std::flush;
        if (!m_reader) {
            m_reader = std::make_unique<StdinLineReader>();
        }
    }

    // Non-owning; all outlive this object (locals of joinOperation()).
    const Context* m_context;
    remote::RemoteOperationClient* m_client;
    ProgressUi* m_ui;
    std::unique_ptr<StdinLineReader> m_reader;
};

Result<CommandResult> joinOperation(const Context& context, remote::RemoteConsoleSession& session,
                                    tunnel::OpKind kind, const JoinCommandOptions& options) {
    const auto target = tunnel::targetFromId(options.core);
    if (!target) {
        return std::unexpected(makeError(ErrorCode::UsageInvalidArgument, options.core));
    }
    // A Monitor host sends its hello right after the sync; a missing hello (also after a timeout
    // or Ctrl+C) is reported just below.
    [[maybe_unused]] const Status helloWaited =
        runUntil([&] { return session.hostHello().has_value(); }, helloWait, context.stop);
    if (!session.hostHello() || !tunnel::supports(*session.hostHello(), kind)) {
        return std::unexpected(makeError(
            ErrorCode::RemoteOperationUnsupportedPeer,
            session.hostHello() ? std::format("{} not offered by the host", tunnel::kindId(kind))
                                : "no hello"));
    }
    remote::RemoteOperationClient client(session);
    ProgressUi ui(context, std::cerr, stderrIsTerminal());
    std::optional<std::pair<bool, Error>> outcome;
    QObject::connect(&client, &remote::RemoteOperationClient::accepted, &client, [&] {
        ui.info("The customer allowed the operation; it runs on their side.");
    });
    QObject::connect(&client, &remote::RemoteOperationClient::progress, &client,
                     [&](const tunnel::OpProgress& p) {
                         ui.progress(p.phase.empty()
                                         ? std::string("working")
                                         : toStdString(remote::operationPhaseText(p.phase)),
                                     p.done, p.total, p.step, p.steps);
                     });
    QObject::connect(&client, &remote::RemoteOperationClient::finished, &client,
                     [&](bool ok, const Error& error) { outcome.emplace(ok, error); });
    // ADR-0368: with a terminal on stdin support answers the LTE update's questions here (y/N);
    // without one the customer does (the request does not offer it).
    JoinQuestions questions(context, client, ui);
    auto id = client.request(
        kind, *target, kind == tunnel::OpKind::Flash ? std::optional(options.flash) : std::nullopt);
    if (!id) {
        return std::unexpected(std::move(id).error());
    }
    status(context, "Waiting for the customer's confirmation...");
    const Status waited = runUntil(
        [&] {
            questions.poll();
            return outcome.has_value();
        },
        std::nullopt, context.stop);
    ui.finish();
    if (!waited) {
        return std::unexpected(waited.error());
    }
    if (!outcome) { // runUntil() returns success only once the outcome is set
        return std::unexpected(makeError(ErrorCode::RemoteHostEnded));
    }
    // ADR-0369: the result names the operation and the local time, as support's Flash card.
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (!outcome->first) {
        Error error = outcome->second;
        error.message = toStdString(
            error.code == ErrorCode::Cancelled && client.declined()
                ? remote::lteUpdateDeclinedText(time) // ADR-0368: support's own No
                : remote::operationFailedText(kind, *target, time, toQString(error.message)));
        return std::unexpected(std::move(error));
    }
    QJsonObject data;
    data.insert(QStringLiteral("operation"), toQString(tunnel::kindId(kind)));
    if (kind != tunnel::OpKind::LteUpdate) { // ADR-0363: no target
        data.insert(QStringLiteral("target"), toQString(tunnel::targetId(*target)));
    }
    if (kind == tunnel::OpKind::Flash) {
        data.insert(QStringLiteral("firmwareId"), toQString(options.flash));
    }
    data.insert(QStringLiteral("reportedBy"), QStringLiteral("host"));
    return CommandResult{
        .data = data,
        .humanText = toStdString(remote::operationResultText(
            kind, kind == tunnel::OpKind::LteUpdate ? tunnel::OpTarget::Lte : *target, time)),
        .streamed = false};
}

Result<CommandResult> join(const Context& context, const JoinCommandOptions& options) {
    auto target = remote::parseJoinInput(options.target);
    if (!target) {
        return std::unexpected(std::move(target).error());
    }
    if (!options.flash.empty() && !firmware::FirmwareId::looksLikeId(options.flash)) {
        return std::unexpected(makeError(ErrorCode::RemoteFirmwareNotInCatalog, options.flash));
    }
    auto relay = joinRelay(context, options, *target);
    if (!relay) {
        return std::unexpected(std::move(relay).error());
    }
    remote::RemoteConsoleSession session(
        remote::JoinOptions{.relay = *relay,
                            .code = target->code,
                            .appVersion = std::string(buildInfo().version),
                            .relayLimits = {},
                            .transcript = {},
                            .idleNoticeAfter = std::chrono::minutes(30),
                            .consoleLog = nullptr});
    QObject::connect(&session, &remote::RemoteConsoleSession::idleNotice, &session, [&context] {
        status(context, "No activity from the customer for 30 min. The session may have ended; "
                        "press Ctrl+C to leave.");
    });
    status(context, std::format("Joining session {} on {}...", target->code.grouped(),
                                toStdString(relay->host())));
    if (const Status joined =
            runUntil([&] { return session.joinState() != remote::JoinState::Connecting; },
                     relayConnectWait, context.stop, ErrorCode::RemoteRelayUnreachable);
        !joined) {
        return std::unexpected(joined.error());
    }
    if (session.joinState() == remote::JoinState::Ended) {
        return std::unexpected(
            session.endError().value_or(makeError(ErrorCode::RemoteRelayUnreachable)));
    }

    if (const auto kind = requestedOperation(options)) {
        if (const Status allowed = waitAllowed(context, session); !allowed) {
            return std::unexpected(allowed.error());
        }
        return joinOperation(context, session, *kind, options);
    }
    if (!options.command.empty()) {
        ResponseCollector collector(context, session);
        if (const Status allowed = waitAllowed(context, session); !allowed) {
            return std::unexpected(allowed.error());
        }
        const Status sent =
            runCommand(context, session, collector, splitLines(options.command),
                       CommandTiming{.writeTimeout = std::chrono::seconds(10),
                                     .collect = std::chrono::milliseconds(static_cast<std::int64_t>(
                                         options.timeoutSeconds * 1000.0)),
                                     .endAtPrompt = false,
                                     .writeTimeoutCode = ErrorCode::RemoteJoinNotAllowed});
        if (!sent) {
            return std::unexpected(sent.error());
        }
        return commandResult(context, options.command, collector.response());
    }

    // The console.
    const bool interactive =
        context.mode == OutputMode::Human && stdinIsTerminal() && stdoutIsTerminal();
    std::stop_source consoleStop;
    const std::stop_callback forwardStop(context.stop,
                                         [&consoleStop] { consoleStop.request_stop(); });
    Context consoleContext = context;
    consoleContext.stop = consoleStop.get_token();
    ConsoleView view(consoleContext, session, interactive && stdoutColors(), false);
    view.setOutputClosedHandler([&consoleStop] { consoleStop.request_stop(); });
    QObject::connect(&session, &remote::RemoteConsoleSession::joinStateChanged, &session, [&] {
        if (session.joinState() == remote::JoinState::Allowed) {
            view.printStatus(std::format("Joined: {}", toStdString(session.title())));
        }
    });
    // The wait for the relay above runs the event loop: the host's sync (the history) and live
    // lines may already have arrived before the view existed. They are in the session's log.
    const std::vector<device::ConsoleLine> earlier = session.log().linesSince(0);
    view.printLines(QList<device::ConsoleLine>(earlier.begin(), earlier.end()));
    if (session.joinState() == remote::JoinState::Allowed) {
        view.printStatus(std::format("Joined: {}", toStdString(session.title())));
    } else {
        view.printStatus("Waiting for the customer to allow you... (the live output may already "
                         "appear)");
    }
    ConsoleUiOptions ui;
    ui.interactive = interactive;
    ui.linger =
        std::chrono::milliseconds(static_cast<std::int64_t>(options.lingerSeconds * 1000.0));
    ui.prompt = "remote:~$ ";
    RemoteTreeSource treeSource(session);
    if (interactive) {
        ui.learner = &treeSource; // ADR-0364
    }
    const Status consoleStatus = runConsole(consoleContext, session, view, ui);
    session.close();
    view.printStopped();
    if (!consoleStatus) {
        Error error = consoleStatus.error();
        if (error.code == ErrorCode::Cancelled && view.outputClosed()) {
            error = outputClosedError("the standard output was closed");
        }
        return std::unexpected(std::move(error));
    }
    return CommandResult{.data = QJsonValue(), .humanText = {}, .streamed = true};
}

} // namespace

void registerRemoteCommands(CLI::App& app, CommandSlot& slot, const GlobalOptions& globals) {
    CLI::App* remote = app.add_subcommand(
        "remote", "Remote support session through the HARDWARIO Terminal server");
    remote->require_subcommand(1);

    // host
    auto hostOptions = std::make_shared<HostCommandOptions>();
    hostOptions->latencyMs =
        device::findTarget(*device::ProfileRegistry::find("chester"), "app")->defaultLatencyMs;
    auto group = std::make_shared<ProbeOptions>();
    CLI::App* hostCommand = remote->add_subcommand(
        "host", "Share the CHESTER console with HARDWARIO Support (prints a 6-digit code)");
    addProbeOptions(*hostCommand, hostOptions->probe);
    hostCommand->add_flag("--ble", hostOptions->ble, "Share a Bluetooth console instead of J-Link");
    hostCommand
        ->add_option("-a,--address", hostOptions->bleDevice.address,
                     "--ble: Bluetooth address (macOS: the device UUID shown by `hwio ble scan`)")
        ->option_text("ID");
    hostCommand
        ->add_option("--name", hostOptions->bleDevice.name,
                     "--ble: a part of the advertised name; exactly one device must match")
        ->option_text("N");
    hostCommand->add_option("--latency", hostOptions->latencyMs, "Latency for RTT readout in ms")
        ->capture_default_str()
        ->option_text("MS");
    CLI::Option* consoleFile = hostCommand
                                   ->add_option("--console-file", hostOptions->consoleFile,
                                                "Console log file ('' = none; default: one file "
                                                "per session in the application data)")
                                   ->option_text("PATH");
    hostCommand->add_flag("--auto-reconnect", hostOptions->autoReconnect,
                          "Keep reconnecting the console after the link drops");
    hostCommand
        ->add_option("--relay", hostOptions->relay,
                     "Support server (https; default: the setting remote/relayUrl or "
                     "https://terminal.hardwario.com)")
        ->option_text("URL");
    hostCommand->add_flag("--deny-operations", hostOptions->denyOperations,
                          "Refuse flash / erase / recover / reset requests without asking");
    hostCommand->callback([&slot, &globals, group, hostOptions, consoleFile] {
        hostOptions->consoleFileGiven = consoleFile->count() > 0;
        slot.command = [&globals, group,
                        hostOptions](const Context& context) -> Result<CommandResult> {
            if (hostOptions->ble &&
                hostOptions->bleDevice.address.empty() == hostOptions->bleDevice.name.empty()) {
                return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                                 "--ble needs exactly one of -a/--address and "
                                                 "--name"));
            }
            if (!hostOptions->ble &&
                (!hostOptions->bleDevice.address.empty() || !hostOptions->bleDevice.name.empty())) {
                return std::unexpected(makeError(ErrorCode::UsageInvalidArgument,
                                                 "-a/--address and --name need --ble"));
            }
            return host(context, globals, *group, *hostOptions);
        };
    });

    // join
    auto joinOptions = std::make_shared<JoinCommandOptions>();
    CLI::App* joinCommand =
        remote->add_subcommand("join", "Join a remote support session by its code or link");
    joinCommand->add_option("CODE_OR_LINK", joinOptions->target, "The session code or link")
        ->required();
    joinCommand
        ->add_option("--relay", joinOptions->relay,
                     "Support server (https; default: the setting remote/relayUrl or "
                     "https://terminal.hardwario.com)")
        ->option_text("URL");
    CLI::Option* commandOption = joinCommand
                                     ->add_option("--command", joinOptions->command,
                                                  "Send one command and print "
                                                  "the response")
                                     ->option_text("CMD");
    joinCommand
        ->add_option("-t,--timeout", joinOptions->timeoutSeconds,
                     "--command: seconds to collect the response")
        ->capture_default_str()
        ->check(CLI::Range(0.1, 600.0))
        ->option_text("S");
    CLI::Option* flash =
        joinCommand
            ->add_option("--flash", joinOptions->flash,
                         "Ask the customer to flash this catalog firmware ID (32 hex digits)")
            ->option_text("ID");
    CLI::Option* erase = joinCommand->add_flag("--erase", joinOptions->erase,
                                               "Ask the customer to erase the target");
    CLI::Option* recover = joinCommand->add_flag("--recover", joinOptions->recover,
                                                 "Ask the customer to recover the target");
    CLI::Option* reset = joinCommand->add_flag("--reset", joinOptions->reset,
                                               "Ask the customer to reset the target");
    CLI::Option* lteUpdate = joinCommand->add_flag("--lte-update", joinOptions->lteUpdate,
                                                   "Ask the customer to update the LTE firmware "
                                                   "(guided, their Monitor's recommended version)");
    CLI::Option* targetOption =
        joinCommand->add_option("--target", joinOptions->core, "Operation target: app or lte")
            ->capture_default_str()
            ->check(CLI::IsMember({"app", "lte"}))
            ->option_text("app|lte");
    joinCommand
        ->add_option("--linger", joinOptions->lingerSeconds,
                     "Non-interactive console: seconds to keep collecting output after the end "
                     "of input")
        ->capture_default_str()
        ->check(CLI::Range(0.0, 60.0))
        ->option_text("S");
    commandOption->excludes(flash)->excludes(erase)->excludes(recover)->excludes(reset)->excludes(
        lteUpdate);
    flash->excludes(erase)->excludes(recover)->excludes(reset)->excludes(lteUpdate);
    erase->excludes(recover)->excludes(reset)->excludes(lteUpdate);
    recover->excludes(reset)->excludes(lteUpdate);
    reset->excludes(lteUpdate);
    lteUpdate->excludes(targetOption);
    joinCommand->callback([&slot, joinOptions] {
        slot.command = [joinOptions](const Context& context) {
            return join(context, *joinOptions);
        };
    });
}

} // namespace hwio::cli
