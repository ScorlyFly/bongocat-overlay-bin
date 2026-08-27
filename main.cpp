#include <cmath>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMargins>
#include <QProcess>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtMath>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

namespace {
constexpr int kBaseOverlayWidth = 864;
constexpr int kBaseOverlayHeight = 360;
constexpr double kDefaultOverlaySizePercent = 100.0;
constexpr double kMinOverlaySizePercent = 10.0;
constexpr double kMaxOverlaySizePercent = 200.0;
constexpr int kPositionMargin = 24;
const QString kDefaultPosition = QStringLiteral("down-right");

double overlaySizePercent(QString value)
{
    value = value.trimmed();
    if (value.endsWith(QLatin1Char('%'))) value.chop(1);

    bool ok = false;
    const double percent = value.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(percent) || percent < kMinOverlaySizePercent
        || percent > kMaxOverlaySizePercent)
        return kDefaultOverlaySizePercent;
    return percent;
}

QSize overlaySizeForPercent(double percent)
{
    return QSize(qRound(kBaseOverlayWidth * percent / 100.0),
                 qRound(kBaseOverlayHeight * percent / 100.0));
}

QString normalizePosition(QString position)
{
    position = position.trimmed().toLower();
    static const QStringList supported{
        QStringLiteral("up-left"), QStringLiteral("up"), QStringLiteral("up-right"),
        QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"),
        QStringLiteral("down-left"), QStringLiteral("down"), QStringLiteral("down-right")};
    return supported.contains(position) ? position : kDefaultPosition;
}

LayerShellQt::Window::Anchors anchorsForPosition(const QString &position)
{
    LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::AnchorNone;
    if (position == QStringLiteral("up-left") || position == QStringLiteral("up")
        || position == QStringLiteral("up-right"))
        anchors |= LayerShellQt::Window::AnchorTop;
    if (position == QStringLiteral("down-left") || position == QStringLiteral("down")
        || position == QStringLiteral("down-right"))
        anchors |= LayerShellQt::Window::AnchorBottom;
    if (position == QStringLiteral("up-left") || position == QStringLiteral("left")
        || position == QStringLiteral("down-left"))
        anchors |= LayerShellQt::Window::AnchorLeft;
    if (position == QStringLiteral("up-right") || position == QStringLiteral("right")
        || position == QStringLiteral("down-right"))
        anchors |= LayerShellQt::Window::AnchorRight;
    return anchors;
}

QMargins marginsForPosition(const QString &position)
{
    QMargins margins;
    if (position == QStringLiteral("up-left") || position == QStringLiteral("up")
        || position == QStringLiteral("up-right"))
        margins.setTop(kPositionMargin);
    if (position == QStringLiteral("down-left") || position == QStringLiteral("down")
        || position == QStringLiteral("down-right"))
        margins.setBottom(kPositionMargin);
    if (position == QStringLiteral("up-left") || position == QStringLiteral("left")
        || position == QStringLiteral("down-left"))
        margins.setLeft(kPositionMargin);
    if (position == QStringLiteral("up-right") || position == QStringLiteral("right")
        || position == QStringLiteral("down-right"))
        margins.setRight(kPositionMargin);
    return margins;
}

QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/bongocat-overlay/config.ini");
}

QString helperPathFor(const QString &name)
{
    const QString source = QStringLiteral(BONGO_SOURCE_DIR "/helper/") + name;
    const QString installed = QCoreApplication::applicationDirPath()
        + QStringLiteral("/../share/bongocat-overlay/helper/") + name;
    return QFileInfo::exists(source) ? source : installed;
}

QScreen *screenByName(const QString &requested)
{
    if (requested.isEmpty() || requested.compare(QStringLiteral("PRIMARY"), Qt::CaseInsensitive) == 0)
        return QGuiApplication::primaryScreen();
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->name() == requested) return screen;
    QStringList names;
    for (QScreen *screen : QGuiApplication::screens()) names << screen->name();
    qWarning().noquote() << "Unknown monitor" << requested << "; available:" << names.join(QStringLiteral(", "));
    return QGuiApplication::primaryScreen();
}

}

class InputBridge final : public QObject
{
    Q_OBJECT
public:
    explicit InputBridge(bool useEvdev, bool debugInput, QObject *parent = nullptr)
        : QObject(parent), m_useEvdev(useEvdev), m_debugInput(debugInput)
    {
        connect(&m_process, &QProcess::readyReadStandardOutput, this, &InputBridge::readOutput);
        connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
            const QString error = QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed();
            if (!error.isEmpty()) qWarning().noquote() << "input:" << error;
        });
        connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            qInfo() << "input helper stopped" << code << status;
            if (!m_stopping && m_useEvdev) QTimer::singleShot(1000, this, &InputBridge::start);
        });
    }

    Q_INVOKABLE void start()
    {
        if (!m_useEvdev || m_process.state() != QProcess::NotRunning) return;
        const QString helperSource = helperPathFor(QStringLiteral("bongo-input"));
        if (!QFileInfo::exists(helperSource)) {
            qWarning() << "Input helper not found:" << helperSource;
            return;
        }
        m_stopping = false;
        QStringList arguments{helperSource, QStringLiteral("--direct")};
        if (m_debugInput) arguments << QStringLiteral("--debug");
        m_process.start(QStringLiteral("python3"), arguments);
        if (!m_process.waitForStarted(1000))
            qWarning() << "Could not start input helper:" << m_process.errorString();
    }

    Q_INVOKABLE void stop()
    {
        m_stopping = true;
        if (m_process.state() != QProcess::NotRunning) {
            m_process.terminate();
            if (!m_process.waitForFinished(500)) m_process.kill();
        }
    }

    void simulateLeft() { qInfo() << "SIM -> leftTriggered"; emit leftTriggered(); }
    void simulateRight() { qInfo() << "SIM -> rightTriggered"; emit rightTriggered(); }
    void simulateBoth() { qInfo() << "SIM -> both"; emit leftTriggered(); emit rightTriggered(); }
    void simulateMouse(int dx, int dy) { qInfo() << "SIM -> mouseMoved" << dx << dy; emit mouseMoved(dx, dy); }

signals:
    void leftTriggered();
    void rightTriggered();
    void mouseMoved(int dx, int dy);
    void mouseButton(QString button);
    void status(QString value);

private slots:
    void readOutput()
    {
        while (m_process.canReadLine()) {
            const QString line = QString::fromLocal8Bit(m_process.readLine()).trimmed();
            const QStringList fields = line.split('\t');
            if (fields.isEmpty()) continue;
            if (fields[0] == QStringLiteral("L")) {
                if (m_debugInput) qInfo() << "BRIDGE -> leftTriggered";
                emit leftTriggered();
            } else if (fields[0] == QStringLiteral("R")) {
                if (m_debugInput) qInfo() << "BRIDGE -> rightTriggered";
                emit rightTriggered();
            } else if (fields[0] == QStringLiteral("M") && fields.size() >= 3) {
                const int dx = fields[1].toInt();
                const int dy = fields[2].toInt();
                if (m_debugInput) qInfo() << "INPUT mouse REL_X=" << dx << "REL_Y=" << dy;
                emit mouseMoved(dx, dy);
            } else if (fields[0] == QStringLiteral("B") && fields.size() >= 2) {
                if (m_debugInput) qInfo() << "INPUT mouse button=" << fields[1];
                emit mouseButton(fields[1]);
            } else if (fields[0] == QStringLiteral("STATUS") && fields.size() >= 2) {
                emit status(fields[1]);
            }
        }
    }

private:
    QProcess m_process;
    bool m_useEvdev = true;
    bool m_debugInput = false;
    bool m_stopping = false;
};

int main(int argc, char **argv)
{
    LayerShellQt::Shell::useLayerShell();
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("bongocat-overlay"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("BongoCat Overlay"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Standalone KDE Wayland Bongo Cat overlay"));
    parser.addHelpOption();
    QCommandLineOption monitorOption(QStringLiteral("monitor"), QStringLiteral("Output name or PRIMARY."), QStringLiteral("name"));
    QCommandLineOption positionOption(QStringLiteral("position"), QStringLiteral("Cat position: up-left, up, up-right, left, center, right, down-left, down, or down-right."), QStringLiteral("name"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Cat size as a percentage of the original sprite, for example 75%."), QStringLiteral("percent"));
    QCommandLineOption debugInputOption(QStringLiteral("debug-input"), QStringLiteral("Log privacy-minimized input protocol events."));
    QCommandLineOption simulateLeftOption(QStringLiteral("simulate-left"), QStringLiteral("Development: trigger left keyboard paw."));
    QCommandLineOption simulateRightOption(QStringLiteral("simulate-right"), QStringLiteral("Development: trigger right keyboard paw."));
    QCommandLineOption simulateBothOption(QStringLiteral("simulate-both"), QStringLiteral("Development: trigger both keyboard paws."));
    QCommandLineOption simulateMouseOption(QStringLiteral("simulate-mouse"), QStringLiteral("Development: move mouse hand by DX,DY."), QStringLiteral("dx,dy"));
    QCommandLineOption simulateInputOption(QStringLiteral("simulate-input"), QStringLiteral("Development: play keyboard and mouse state sequence."));
    parser.addOption(monitorOption);
    parser.addOption(positionOption);
    parser.addOption(sizeOption);
    parser.addOption(debugInputOption);
    parser.addOption(simulateLeftOption);
    parser.addOption(simulateRightOption);
    parser.addOption(simulateBothOption);
    parser.addOption(simulateMouseOption);
    parser.addOption(simulateInputOption);
    parser.process(app);

    QSettings settings(configPath(), QSettings::IniFormat);
    const QString requestedMonitor = parser.isSet(monitorOption)
        ? parser.value(monitorOption) : settings.value(QStringLiteral("Overlay/monitor"), QStringLiteral("PRIMARY")).toString();
    QScreen *targetScreen = screenByName(requestedMonitor);
    if (!targetScreen) {
        qCritical() << "No QScreen available";
        return 1;
    }
    qInfo().noquote() << "Primary screen:" << QGuiApplication::primaryScreen()->name();
    qInfo().noquote() << "Selected screen:" << targetScreen->name();
    QStringList availableScreens;
    for (QScreen *screen : QGuiApplication::screens()) availableScreens << screen->name();
    qInfo().noquote() << "Available screens:" << availableScreens;
    const QString configuredPosition = parser.isSet(positionOption)
        ? parser.value(positionOption)
        : settings.value(QStringLiteral("Overlay/position"), kDefaultPosition).toString();
    const QString position = normalizePosition(configuredPosition);
    if (configuredPosition.trimmed().toLower() != position)
        qWarning().noquote() << "Unknown cat position" << configuredPosition
                             << "; using" << position;
    const QString configuredSize = parser.isSet(sizeOption)
        ? parser.value(sizeOption)
        : settings.value(QStringLiteral("Overlay/size"), QStringLiteral("100%")).toString();
    const double sizePercent = overlaySizePercent(configuredSize);
    const QSize overlaySize = overlaySizeForPercent(sizePercent);
    const int overlayWidth = overlaySize.width();
    const int overlayHeight = overlaySize.height();
    const bool simulation = parser.isSet(simulateLeftOption) || parser.isSet(simulateRightOption)
        || parser.isSet(simulateBothOption) || parser.isSet(simulateMouseOption)
        || parser.isSet(simulateInputOption);

    InputBridge input(!simulation, parser.isSet(debugInputOption));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("inputBridge"), &input);
    engine.rootContext()->setContextProperty(QStringLiteral("debugInput"), parser.isSet(debugInputOption));
    engine.rootContext()->setContextProperty(QStringLiteral("overlayWidth"), overlaySize.width());
    engine.rootContext()->setContextProperty(QStringLiteral("overlayHeight"), overlaySize.height());
    const QUrl source(QStringLiteral("qrc:/BongaCatOverlay/Overlay.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [source] { qCritical() << "Could not load" << source; QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(source);
    if (engine.rootObjects().isEmpty()) return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        qCritical() << "Overlay root is not a QQuickWindow";
        return 1;
    }
    auto *layerWindow = LayerShellQt::Window::get(window);
    if (!layerWindow) {
        qCritical() << "LayerShellQt could not attach to the Qt Wayland window";
        return 1;
    }
    layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layerWindow->setExclusiveZone(0);
    layerWindow->setScope(QStringLiteral("bongocat-overlay"));
    layerWindow->setActivateOnShow(false);
    layerWindow->setWantsToBeOnActiveScreen(false);
    layerWindow->setScreen(targetScreen);
    layerWindow->setAnchors(anchorsForPosition(position));
    layerWindow->setMargins(marginsForPosition(position));
    layerWindow->setDesiredSize(QSize(overlayWidth, overlayHeight));

    window->setFlag(Qt::FramelessWindowHint, true);
    window->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    window->setFlag(Qt::WindowTransparentForInput, true);
    window->show();

    input.start();
    if (parser.isSet(simulateLeftOption)) QTimer::singleShot(250, &input, &InputBridge::simulateLeft);
    if (parser.isSet(simulateRightOption)) QTimer::singleShot(250, &input, &InputBridge::simulateRight);
    if (parser.isSet(simulateBothOption)) QTimer::singleShot(250, &input, &InputBridge::simulateBoth);
    if (parser.isSet(simulateMouseOption)) {
        const QStringList motion = parser.value(simulateMouseOption).split(',');
        if (motion.size() == 2) QTimer::singleShot(250, &input, [&input, motion] {
            input.simulateMouse(motion[0].toInt(), motion[1].toInt());
        });
    }
    if (parser.isSet(simulateInputOption)) {
        QTimer::singleShot(500, &input, &InputBridge::simulateLeft);
        QTimer::singleShot(1300, &input, &InputBridge::simulateRight);
        QTimer::singleShot(2100, &input, &InputBridge::simulateBoth);
        QTimer::singleShot(3000, &input, [&input] { input.simulateMouse(20, 0); });
        QTimer::singleShot(3150, &input, [&input] { input.simulateMouse(20, 0); });
        QTimer::singleShot(3300, &input, [&input] { input.simulateMouse(0, -15); });
        QTimer::singleShot(3450, &input, [&input] { input.simulateMouse(-30, 0); });
        QTimer::singleShot(3600, &input, [&input] { input.simulateMouse(0, 20); });
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &input, &InputBridge::stop);
    return app.exec();
}

#include "main.moc"
