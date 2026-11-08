#include <cmath>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
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
// The sprite PNGs are cropped to the union of every visible pixel across all
// four animation frames (including the detached motion lines), so the window
// has no transparent padding that would keep the cat away from screen edges.
constexpr int kBaseOverlayWidth = 600;
constexpr int kBaseOverlayHeight = 345;
// Union of the visible cat body across all four animation frames, excluding the
// detached motion lines. This is the rectangle the selection frame should hug.
constexpr int kContentLeftPx = 9;
constexpr int kContentTopPx = 1;
constexpr int kContentWidthPx = 591;
constexpr int kContentHeightPx = 298;
constexpr double kContentLeft = double(kContentLeftPx) / kBaseOverlayWidth;
constexpr double kContentTop = double(kContentTopPx) / kBaseOverlayHeight;
constexpr double kContentWidth = double(kContentWidthPx) / kBaseOverlayWidth;
constexpr double kContentHeight = double(kContentHeightPx) / kBaseOverlayHeight;
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

QPoint positionForGrid(const QString &position, const QSize &overlaySize, const QSize &screenSize)
{
    const QMargins margins = marginsForPosition(position);
    int x = (screenSize.width() - overlaySize.width()) / 2;
    int y = (screenSize.height() - overlaySize.height()) / 2;

    if (position == QStringLiteral("up-left") || position == QStringLiteral("left")
        || position == QStringLiteral("down-left"))
        x = margins.left();
    else if (position == QStringLiteral("up-right") || position == QStringLiteral("right")
             || position == QStringLiteral("down-right"))
        x = screenSize.width() - overlaySize.width() - margins.right();

    if (position == QStringLiteral("up-left") || position == QStringLiteral("up")
        || position == QStringLiteral("up-right"))
        y = margins.top();
    else if (position == QStringLiteral("down-left") || position == QStringLiteral("down")
             || position == QStringLiteral("down-right"))
        y = screenSize.height() - overlaySize.height() - margins.bottom();

    return QPoint(x, y);
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

class PositionBridge final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int overlayWidth READ overlayWidth NOTIFY overlaySizeChanged)
    Q_PROPERTY(int overlayHeight READ overlayHeight NOTIFY overlaySizeChanged)
    Q_PROPERTY(qreal contentLeft READ contentLeft CONSTANT)
    Q_PROPERTY(qreal contentTop READ contentTop CONSTANT)
    Q_PROPERTY(qreal contentWidth READ contentWidth CONSTANT)
    Q_PROPERTY(qreal contentHeight READ contentHeight CONSTANT)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY mirroredChanged)
public:
    explicit PositionBridge(const QSize &overlaySize, QObject *parent = nullptr)
        : QObject(parent), m_overlaySize(overlaySize) {}

    int overlayWidth() const { return m_overlaySize.width(); }
    int overlayHeight() const { return m_overlaySize.height(); }
    qreal contentLeft() const { return kContentLeft; }
    qreal contentTop() const { return kContentTop; }
    qreal contentWidth() const { return kContentWidth; }
    qreal contentHeight() const { return kContentHeight; }

    bool mirrored() const { return m_mirrored; }
    void setMirrored(bool mirrored)
    {
        if (m_mirrored == mirrored) return;
        m_mirrored = mirrored;
        if (m_settings) {
            m_settings->setValue(QStringLiteral("Overlay/mirror"), m_mirrored);
            m_settings->sync();
        }
        emit mirroredChanged();
    }

    void attach(LayerShellQt::Window *layerWindow, QQuickWindow *window, QScreen *screen,
                const QPoint &position, QSettings *settings)
    {
        m_layerWindow = layerWindow;
        m_window = window;
        m_screen = screen;
        m_settings = settings;
        if (m_settings) {
            m_mirrored = m_settings->value(QStringLiteral("Overlay/mirror"), false).toBool();
            emit mirroredChanged();
        }
        m_position = clamped(position, m_overlaySize);
        applyGeometry();
    }

    Q_INVOKABLE QPoint dragBy(qreal dx, qreal dy)
    {
        if (!m_layerWindow || !m_window) return QPoint();
        const QPoint before = m_position;
        m_position = clamped(QPoint(qRound(m_position.x() + dx), qRound(m_position.y() + dy)),
                             m_overlaySize);
        if (m_position != before) applyGeometry();
        return m_position - before;
    }

    Q_INVOKABLE QPoint resizeBy(int corner, qreal dx, qreal dy)
    {
        if (!m_layerWindow || !m_window || !m_screen) return QPoint();
        if (m_overlaySize.width() <= 0 || m_overlaySize.height() <= 0) return QPoint();

        // corner: 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left.
        const bool movingLeft = corner == 0 || corner == 3;
        const bool movingTop = corner == 0 || corner == 1;
        const double widthDelta = movingLeft ? -dx : dx;
        const double heightDelta = movingTop ? -dy : dy;
        const double contentWidth = m_overlaySize.width() * kContentWidth;
        const double contentHeight = m_overlaySize.height() * kContentHeight;
        const double horizontalScale = (contentWidth + widthDelta) / contentWidth;
        const double verticalScale = (contentHeight + heightDelta) / contentHeight;
        double scale = qAbs(horizontalScale - 1.0) >= qAbs(verticalScale - 1.0)
            ? horizontalScale : verticalScale;

        const QSize screenSize = m_screen->geometry().size();
        const int minWidth = qRound(kBaseOverlayWidth * kMinOverlaySizePercent / 100.0);
        const int minHeight = qRound(kBaseOverlayHeight * kMinOverlaySizePercent / 100.0);
        const int freeWidth = movingLeft ? m_position.x() + m_overlaySize.width()
                                         : screenSize.width() - m_position.x();
        const int freeHeight = movingTop ? m_position.y() + m_overlaySize.height()
                                          : screenSize.height() - m_position.y();
        const int maxWidth = qMax(minWidth,
            qMin(qRound(kBaseOverlayWidth * kMaxOverlaySizePercent / 100.0), freeWidth));
        const int maxHeight = qMax(minHeight,
            qMin(qRound(kBaseOverlayHeight * kMaxOverlaySizePercent / 100.0), freeHeight));
        const double minScale = qMax(double(minWidth) / m_overlaySize.width(),
                                     double(minHeight) / m_overlaySize.height());
        double maxScale = qMin(double(maxWidth) / m_overlaySize.width(),
                               double(maxHeight) / m_overlaySize.height());
        if (maxScale < minScale) maxScale = minScale;
        scale = qBound(minScale, scale, maxScale);

        QSize newSize(qBound(minWidth, qRound(m_overlaySize.width() * scale), maxWidth),
                      qBound(minHeight, qRound(m_overlaySize.height() * scale), maxHeight));
        QPoint newPosition = m_position;
        if (movingLeft) {
            newPosition.setX(qRound(m_position.x()
                + (kContentLeft + kContentWidth)
                    * (m_overlaySize.width() - newSize.width())));
        } else {
            newPosition.setX(qRound(m_position.x()
                + kContentLeft * (m_overlaySize.width() - newSize.width())));
        }
        if (movingTop) {
            newPosition.setY(qRound(m_position.y()
                + (kContentTop + kContentHeight)
                    * (m_overlaySize.height() - newSize.height())));
        } else {
            newPosition.setY(qRound(m_position.y()
                + kContentTop * (m_overlaySize.height() - newSize.height())));
        }
        newPosition = clamped(newPosition, newSize);

        const QPoint applied = newPosition - m_position;
        if (newSize == m_overlaySize && applied.isNull()) return applied;
        m_position = newPosition;
        if (newSize != m_overlaySize) {
            m_overlaySize = newSize;
            emit overlaySizeChanged();
        }
        applyGeometry();
        return applied;
    }

    Q_INVOKABLE void finishDrag() { saveGeometry(); }
    Q_INVOKABLE void finishResize() { saveGeometry(); }
    Q_INVOKABLE void quit() { QCoreApplication::quit(); }

signals:
    void overlaySizeChanged();
    void mirroredChanged();

private:
    QPoint clamped(QPoint position, const QSize &size) const
    {
        if (!m_screen) return position;
        const QSize screenSize = m_screen->geometry().size();
        const int maxX = qMax(0, screenSize.width() - size.width());
        const int maxY = qMax(0, screenSize.height() - size.height());
        return QPoint(qBound(0, position.x(), maxX), qBound(0, position.y(), maxY));
    }

    void saveGeometry()
    {
        if (!m_settings) return;
        const double percent = 100.0 * m_overlaySize.width() / kBaseOverlayWidth;
        m_settings->setValue(QStringLiteral("Overlay/position"), QStringLiteral("custom"));
        m_settings->setValue(QStringLiteral("Overlay/x"), m_position.x());
        m_settings->setValue(QStringLiteral("Overlay/y"), m_position.y());
        m_settings->setValue(QStringLiteral("Overlay/size"),
                             QString::number(percent, 'f', 2) + QLatin1Char('%'));
        m_settings->sync();
    }

    void applyGeometry()
    {
        if (!m_layerWindow || !m_window) return;
        LayerShellQt::Window::Anchors anchors = LayerShellQt::Window::AnchorTop;
        anchors |= LayerShellQt::Window::AnchorLeft;
        m_layerWindow->setAnchors(anchors);
        QMargins margins;
        margins.setLeft(m_position.x());
        margins.setTop(m_position.y());
        m_layerWindow->setMargins(margins);
        m_layerWindow->setDesiredSize(m_overlaySize);
        // Layer shell state is only flushed with a surface commit.
        m_window->update();
        m_window->requestUpdate();
    }

    LayerShellQt::Window *m_layerWindow = nullptr;
    QQuickWindow *m_window = nullptr;
    QScreen *m_screen = nullptr;
    QSettings *m_settings = nullptr;
    QSize m_overlaySize;
    QPoint m_position;
    bool m_mirrored = false;
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

    const QString settingsPath = configPath();
    QDir().mkpath(QFileInfo(settingsPath).absolutePath());
    QSettings settings(settingsPath, QSettings::IniFormat);
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
    const QString configuredSize = parser.isSet(sizeOption)
        ? parser.value(sizeOption)
        : settings.value(QStringLiteral("Overlay/size"), QStringLiteral("100%")).toString();
    const double sizePercent = overlaySizePercent(configuredSize);
    const QSize overlaySize = overlaySizeForPercent(sizePercent);

    const QString configuredPosition = parser.isSet(positionOption)
        ? parser.value(positionOption)
        : settings.value(QStringLiteral("Overlay/position"), kDefaultPosition).toString();
    const bool savedPosition = !parser.isSet(positionOption)
        && configuredPosition.trimmed().compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0
        && settings.contains(QStringLiteral("Overlay/x"))
        && settings.contains(QStringLiteral("Overlay/y"));
    QString position = kDefaultPosition;
    QPoint initialPosition;
    if (savedPosition) {
        initialPosition = QPoint(settings.value(QStringLiteral("Overlay/x")).toInt(),
                                 settings.value(QStringLiteral("Overlay/y")).toInt());
    } else {
        position = normalizePosition(configuredPosition);
        if (configuredPosition.trimmed().toLower() != position)
            qWarning().noquote() << "Unknown cat position" << configuredPosition
                                 << "; using" << position;
        initialPosition = positionForGrid(position, overlaySize, targetScreen->geometry().size());
    }
    const bool simulation = parser.isSet(simulateLeftOption) || parser.isSet(simulateRightOption)
        || parser.isSet(simulateBothOption) || parser.isSet(simulateMouseOption)
        || parser.isSet(simulateInputOption);

    InputBridge input(!simulation, parser.isSet(debugInputOption));
    PositionBridge positionBridge(overlaySize);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("inputBridge"), &input);
    engine.rootContext()->setContextProperty(QStringLiteral("positionBridge"), &positionBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("debugInput"), parser.isSet(debugInputOption));
    const QUrl source(QStringLiteral("qrc:/qt/qml/BongaCatOverlay/Overlay.qml"));
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

    window->setFlag(Qt::FramelessWindowHint, true);
    window->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    positionBridge.attach(layerWindow, window, targetScreen, initialPosition, &settings);
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
