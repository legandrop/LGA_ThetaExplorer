#include "thetaexplorer/MainWindow.h"
#include "thetaexplorer/ThetaCameraService.h"
#include "thetaexplorer/HdrGroupingService.h"
#include "thetaexplorer/ThumbnailGridWidget.h"
#include "thetaexplorer/PreviewPanel.h"
#include "thetaexplorer/FileMetadataPanel.h"
#include "thetaexplorer/DownloadManager.h"
#include "thetaexplorer/ConfirmDialog.h"
#include "thetaexplorer/HelpDialog.h"
#include "lgaupdate/LgaUpdateService.h"
#include "thetaexplorer/StatusWidgets.h"
#include "thetaexplorer/AppPaths.h"
#include "thetaexplorer/ColorUtils.h"
#include "thetaexplorer/UiIcons.h"
#include "thetaexplorer/Logger.h"
#include "thetaexplorer/popover/MessagePopover.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QSplitter>
#include <QStackedWidget>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QProgressBar>
#include <QProcess>
#include <QScopedPointer>
#include <QStatusBar>
#include <QStyle>
#include <QSettings>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QIcon>
#include <QImageReader>
#include <QPixmap>
#include <QDebug>
#include <QScrollBar>
#include <QResizeEvent>
#include <QTimer>

namespace {

QString chooseDirectoryNonNative(QWidget* parent,
                                 const QString& title,
                                 const QString& initialDir)
{
    QFileDialog dialog(parent, title, initialDir);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setModal(true);
    dialog.raise();
    dialog.activateWindow();

    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }

    const QStringList selected = dialog.selectedFiles();
    return selected.isEmpty() ? QString() : selected.first();
}

} // namespace

namespace {

QString findMatchingLocalFilePath(const QString& folderPath, const QString& expectedName)
{
    const QString exactPath = QDir(folderPath).filePath(expectedName);
    if (QFileInfo::exists(exactPath)) {
        return exactPath;
    }

    const QFileInfo expectedInfo(expectedName);
    const QString base = expectedInfo.completeBaseName();
    const QString suffix = expectedInfo.suffix();
    QDir dir(folderPath);
    const QStringList candidates = dir.entryList(
        QStringList()
            << QString("%1 *.%2").arg(base, suffix)
            << QString("%1.%2").arg(base, suffix),
        QDir::Files,
        QDir::Name
    );

    for (const QString& candidate : candidates) {
        const QFileInfo fi(candidate);
        const QString candidateBase = fi.completeBaseName();
        if (candidateBase == base || candidateBase.startsWith(base + " ")) {
            return dir.filePath(candidate);
        }
    }

    return QString();
}

} // namespace


namespace {

// Colores de los glyphs de los controles del Viewer.
constexpr auto kHelpIconHover = "#ffffff";
constexpr auto kIconNeutral         = "#b2b2b2";
constexpr auto kIconNeutralDisabled = "#4a4a4a";
constexpr auto kIconPrimary         = "#cccccc";
constexpr auto kIconPrimaryDisabled = "#3a3a50";
constexpr auto kIconNeutralAction   = "#999999";
constexpr int  kToolbarIconSize     = 15;
constexpr int  kViewerMinimumWidth  = 360;
constexpr int  kLowBatteryPercent   = 20;

QPushButton* toolbarButton(const QString& text, const char* objectName, QWidget* parent)
{
    auto* btn = new QPushButton(text, parent);
    btn->setObjectName(objectName);
    btn->setFixedHeight(30);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setIconSize(QSize(kToolbarIconSize, kToolbarIconSize));
    return btn;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Theta Explorer");
    setMinimumSize(900, 600);
    resize(1200, 720);

    // Default download folder
    m_downloadFolder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                       + "/ThetaDownloads";

    // Camera service
    m_service = new ThetaCameraService(this);

    setupUI();
    setupMenus();
    setupConnections();
    applyButtonIcons();
    loadSettings();
    setupUpdates();

    // Start scanning for the camera
    m_service->start();
    onCameraDisconnected();
    // La toolbar y la grilla ya dicen que se esta buscando la camara.
    setStatusMessage(QString());
}

MainWindow::~MainWindow()
{
    m_service->stop();
}

void MainWindow::setupUI()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    auto* rootLayout = new QVBoxLayout(m_centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setObjectName("mainSplitter");
    m_mainSplitter->setHandleWidth(5);
    m_mainSplitter->setChildrenCollapsible(false);

    m_gridStack = new QStackedWidget(this);
    m_gridWidget = new ThumbnailGridWidget(this);
    m_emptyState = new EmptyStateWidget(this);
    m_gridStack->addWidget(m_gridWidget);
    m_gridStack->addWidget(m_emptyState);
    m_gridStack->setMinimumWidth(m_gridWidget->minimumWidth());
    m_mainSplitter->addWidget(m_gridStack);

    m_viewerContainer = new QWidget(this);
    m_viewerContainer->setObjectName("viewerContainer");
    m_viewerContainer->setAttribute(Qt::WA_StyledBackground);
    m_viewerContainer->setMinimumWidth(kViewerMinimumWidth);
    auto* viewerLayout = new QVBoxLayout(m_viewerContainer);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);

    // Tres filas discretas dentro del Viewer; Browse conserva toda su altura.
    m_toolbar = new QWidget(m_viewerContainer);
    m_toolbar->setObjectName("toolbar");
    m_toolbar->setAttribute(Qt::WA_StyledBackground);
    m_toolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* tbLayout = new QVBoxLayout(m_toolbar);
    tbLayout->setContentsMargins(10, 9, 10, 9);
    tbLayout->setSpacing(6);
    auto* deviceRow = new QHBoxLayout;
    deviceRow->setSpacing(6);

    m_cameraDot = new StatusDot(m_toolbar);
    m_cameraLabel = new QLabel(m_toolbar);
    m_cameraLabel->setObjectName("cameraLabel");

    m_batteryBox = new QWidget(m_toolbar);
    m_batteryBox->setObjectName("batteryBox");
    auto* batteryLayout = new QHBoxLayout(m_batteryBox);
    batteryLayout->setContentsMargins(0, 0, 0, 0);
    batteryLayout->setSpacing(5);
    m_batteryIcon = new QLabel(m_batteryBox);
    m_batteryLabel = new QLabel(m_batteryBox);
    m_batteryLabel->setObjectName("batteryLabel");
    batteryLayout->addWidget(m_batteryIcon);
    batteryLayout->addWidget(m_batteryLabel);
    m_batteryBox->hide();

    m_folderBtn = toolbarButton(QString(), "destinationButton", m_toolbar);
    m_folderBtn->setFixedHeight(24);
    auto* folderLayout = new QHBoxLayout(m_folderBtn);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folderLayout->setSpacing(7);
    m_folderIcon = new QLabel(m_folderBtn);
    m_folderIcon->setFixedSize(kToolbarIconSize, kToolbarIconSize);
    auto* folderPrefix = new QLabel("Save to", m_folderBtn);
    folderPrefix->setObjectName("folderLabel");
    m_folderLabel = new QLabel(m_folderBtn);
    m_folderLabel->setObjectName("folderLabel");
    m_folderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    for (QLabel* label : {m_folderIcon, folderPrefix, m_folderLabel}) {
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    folderLayout->addWidget(m_folderIcon, 0, Qt::AlignVCenter);
    folderLayout->addWidget(folderPrefix, 0, Qt::AlignVCenter);
    folderLayout->addWidget(m_folderLabel, 1);
    m_folderBtn->installEventFilter(this);
    updateFolderLabel();

    m_refreshBtn = toolbarButton(QString(), "refreshBtn", m_toolbar);
    m_refreshBtn->setFixedWidth(30);
    m_refreshBtn->setAccessibleName("Refresh");
    m_refreshBtn->setIconSize(QSize(20, 20));
    m_refreshBtn->setAttribute(Qt::WA_Hover);
    m_refreshBtn->installEventFilter(this);

    m_selectionLabel = new QLabel(m_toolbar);
    m_selectionLabel->setObjectName("selectionLabel");
    m_selectionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_progressLabel = new QLabel(m_toolbar);
    m_progressLabel->setObjectName("progressLabel");
    m_progressLabel->setWordWrap(true);
    m_progressLabel->hide();
    m_progressBar = new QProgressBar(m_toolbar);
    m_progressBar->setRange(0, 100);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();

    m_downloadBtn = toolbarButton("Download", "downloadBtn", m_toolbar);
    m_downloadBtn->setEnabled(false);
    m_deleteBtn = toolbarButton("Delete", "deleteBtn", m_toolbar);
    m_deleteBtn->setEnabled(false);

    m_helpBtn = toolbarButton(QString(), "helpBtn", m_toolbar);
    m_helpBtn->setFixedWidth(30);
    m_helpBtn->setAccessibleName("Help");
    m_helpBtn->setAttribute(Qt::WA_Hover);
    m_helpBtn->installEventFilter(this);

    deviceRow->addWidget(m_cameraDot, 0, Qt::AlignVCenter);
    deviceRow->addWidget(m_cameraLabel, 1, Qt::AlignVCenter);
    deviceRow->addWidget(m_batteryBox, 0, Qt::AlignVCenter);
    deviceRow->addWidget(m_refreshBtn, 0, Qt::AlignVCenter);
    deviceRow->addWidget(m_helpBtn, 0, Qt::AlignVCenter);
    tbLayout->addLayout(deviceRow);
    tbLayout->addWidget(m_folderBtn);

    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    actionRow->addWidget(m_selectionLabel, 1);
    actionRow->addWidget(m_deleteBtn, 0, Qt::AlignVCenter);
    actionRow->addWidget(m_downloadBtn, 0, Qt::AlignVCenter);
    tbLayout->addLayout(actionRow);
    tbLayout->addWidget(m_progressLabel);
    tbLayout->addWidget(m_progressBar);

    viewerLayout->addWidget(m_toolbar);
    m_previewPanel = new PreviewPanel(m_viewerContainer);
    m_metaPanel = new FileMetadataPanel(m_viewerContainer);
    viewerLayout->addWidget(m_previewPanel, 1);
    viewerLayout->addWidget(m_metaPanel);

    m_mainSplitter->addWidget(m_viewerContainer);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setSizes({576, 624});
    connect(m_mainSplitter, &QSplitter::splitterMoved, this,
            [this](int, int) { snapBrowserWidth(); });

    rootLayout->addWidget(m_mainSplitter, 1);

    // Status bar
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("statusLabel");
    statusBar()->addPermanentWidget(m_statusLabel, 1);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::setupMenus()
{
    // En macOS la barra de menu es la nativa: el About con AboutRole lo mueve Qt al menu de
    // la app ("About Theta Explorer"), y el Help queda con Cmd+?.
    QMenu* helpMenu = menuBar()->addMenu("Help");

    auto* helpAction = new QAction("Theta Explorer Help", this);
    helpAction->setShortcut(QKeySequence::HelpContents);
    connect(helpAction, &QAction::triggered, this, &MainWindow::onHelpRequested);
    helpMenu->addAction(helpAction);

    auto* aboutAction = new QAction("About Theta Explorer", this);
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onHelpRequested);
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupConnections()
{
    connect(m_service, &ThetaCameraService::cameraConnected,
            this, &MainWindow::onCameraConnected);
    connect(m_service, &ThetaCameraService::cameraDisconnected,
            this, &MainWindow::onCameraDisconnected);
    connect(m_service, &ThetaCameraService::fileListReady,
            this, &MainWindow::onFileListReady);
    connect(m_service, &ThetaCameraService::thumbnailReady,
            m_gridWidget, &ThumbnailGridWidget::setThumbnail);
    connect(m_service, &ThetaCameraService::thumbnailReady,
            this, [this](const QString& path, const QPixmap& px) {
                // Una miniatura tardia no reemplaza el JPEG completo de la misma seleccion.
                if (m_selectedGroups.size() == 1 &&
                    m_selectedGroups.first().representativeDevicePath() == path &&
                    m_previewFileDevicePath != path) {
                    m_previewPanel->showGroup(m_selectedGroups.first(), px);
                }
            });
    connect(m_service, &ThetaCameraService::previewFileReady, this,
            [this](const QString& devicePath, const QString& filePath) {
                if (m_selectedGroups.size() != 1 ||
                    m_selectedGroups.first().representativeDevicePath() != devicePath) return;
                m_previewFileDevicePath = devicePath;
                m_previewPanel->showPreviewFile(m_selectedGroups.first(), filePath);
                const QSize dimensions = QImageReader(filePath).size();
                if (dimensions.isValid()) m_metaPanel->setDimensions(dimensions.width(), dimensions.height());
            });
    connect(m_service, &ThetaCameraService::previewError, this,
            [this](const QString& devicePath, const QString& message) {
                if (m_selectedGroups.size() != 1 ||
                    m_selectedGroups.first().representativeDevicePath() != devicePath) return;
                m_previewPanel->showPreviewError(devicePath, message);
                setStatusMessage("Preview unavailable.", ColorUtils::WARNING);
                notifyError(QString("Preview unavailable: %1").arg(message.toHtmlEscaped()));
            });
    connect(m_previewPanel, &PreviewPanel::previewFailed, this,
            [this](const QString& message) {
                setStatusMessage("Preview unavailable.", ColorUtils::WARNING);
                notifyError(QString("Preview unavailable: %1").arg(message.toHtmlEscaped()));
            });
    connect(m_service, &ThetaCameraService::previewActivityChanged, this,
            [this](bool) { updateButtonStates(); });
    connect(m_service, &ThetaCameraService::downloadProgress,
            this, &MainWindow::onDownloadProgress);
    connect(m_service, &ThetaCameraService::downloadFileCompleted,
            this, &MainWindow::onDownloadFileCompleted);
    connect(m_service, &ThetaCameraService::downloadError,
            this, &MainWindow::onDownloadError);
    connect(m_service, &ThetaCameraService::batteryLevelChanged,
            this, &MainWindow::onBatteryLevelChanged);
    connect(m_service, &ThetaCameraService::deleteCompleted,
            this, &MainWindow::onDeleteCompleted);
    connect(m_service, &ThetaCameraService::errorOccurred,
            this, &MainWindow::onErrorOccurred);

    connect(m_gridWidget, &ThumbnailGridWidget::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    connect(m_folderBtn,   &QPushButton::clicked, this, &MainWindow::onBrowseFolderClicked);
    connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onDownloadClicked);
    connect(m_deleteBtn,   &QPushButton::clicked, this, &MainWindow::onDeleteClicked);
    connect(m_refreshBtn,  &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(m_helpBtn,     &QPushButton::clicked, this, &MainWindow::onHelpRequested);
}

void MainWindow::applyButtonIcons()
{
    const qreal dpr = devicePixelRatioF();
    m_folderIcon->setPixmap(UiIcons::pixmap("folder", kToolbarIconSize,
        QColor(kIconNeutral), dpr));
    updateRefreshButtonIcon();
    m_downloadBtn->setIconSize(QSize(14, 14));
    m_deleteBtn->setIconSize(QSize(14, 14));
    m_downloadBtn->setIcon(UiIcons::buttonIcon("download", 14,
        QColor(kIconPrimary), QColor(kIconPrimaryDisabled), dpr));
    m_deleteBtn->setIcon(UiIcons::buttonIcon("trash", 14,
        QColor(kIconNeutralAction), QColor(kIconNeutralDisabled), dpr));
    updateHelpButtonIcon();
    m_batteryIcon->setPixmap(UiIcons::pixmap("battery", 18, QColor(ColorUtils::TXT_SECUNDARIO), dpr));
}

void MainWindow::updateRefreshButtonIcon()
{
    QIcon icon(m_refreshHovered && m_refreshBtn->isEnabled()
        ? ":/icons/ui/refresh_ps_white.svg" : ":/icons/ui/refresh_ps.svg");
    icon.addPixmap(UiIcons::pixmap("refresh_ps", 20, QColor(kIconNeutralDisabled),
                                 devicePixelRatioF()), QIcon::Disabled);
    m_refreshBtn->setIcon(icon);
}

void MainWindow::updateHelpButtonIcon()
{
    const QColor color(m_helpHovered ? kHelpIconHover : m_helpDialog
        ? ColorUtils::VIOLETA_OSCURO : ColorUtils::TXT_PRINCIPAL);
    m_helpBtn->setIcon(UiIcons::buttonIcon("help", 21, color,
        QColor(kIconNeutralDisabled), devicePixelRatioF()));
    m_helpBtn->setIconSize(QSize(21, 21));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_helpBtn && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        m_helpHovered = event->type() == QEvent::Enter;
        updateHelpButtonIcon();
    }
    if (watched == m_refreshBtn) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            m_refreshHovered = event->type() == QEvent::Enter;
            updateRefreshButtonIcon();
        } else if (event->type() == QEvent::EnabledChange) {
            updateRefreshButtonIcon();
        }
    }
    if (watched == m_folderBtn && event->type() == QEvent::Resize) {
        QTimer::singleShot(0, this, &MainWindow::updateFolderLabel);
    }
    return QMainWindow::eventFilter(watched, event);
}

QString MainWindow::updateInstallBlockedReason() const
{
    if (m_downloadInProgress || m_service->isDownloadActive() || m_service->isDeleteActive()) {
        return "Wait for the camera download or deletion to finish before updating.";
    }

#ifdef Q_OS_WIN
    // Enumerar por nombre nativo y excluir solamente el auxiliar hijo de esta instancia.
    const DWORD ownPid = DWORD(QApplication::applicationPid());
    const QString executable = QFileInfo(QApplication::applicationFilePath()).fileName();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return "The running copies could not be checked. Please try updating again.";
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool otherCopy = false;
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return "The running copies could not be checked. Please try updating again.";
    }
    {
        do {
            if (entry.th32ProcessID == ownPid) continue;
            if (QString::fromWCharArray(entry.szExeFile).compare(executable, Qt::CaseInsensitive) != 0) continue;
            // El worker corre el mismo exe. Un hijo con ruta distinta no queda exceptuado.
            bool ownWorker = false;
            if (entry.th32ParentProcessID == ownPid) {
                const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                wchar_t path[32768]; DWORD length = 32768;
                if (process) {
                    if (QueryFullProcessImageNameW(process, 0, path, &length))
                        ownWorker = QDir::fromNativeSeparators(QString::fromWCharArray(path, int(length)))
                            .compare(QDir::fromNativeSeparators(QApplication::applicationFilePath()), Qt::CaseInsensitive) == 0;
                    CloseHandle(process);
                }
            }
            if (!ownWorker) { otherCopy = true; break; }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (otherCopy) return "Close the other Theta Explorer windows before updating.";
    // Los helpers de instalacion son PowerShell; consultar solo sus comandos, sin cerrar nada.
    const QString helperPrefix = QDir(LgaUpdateService::resolveUpdateDir(AppPaths::updateCacheDirectory()))
        .filePath("LGA_ThetaExplorer_Update_Helper_");
    QScopedPointer<QProcess> reader(new QProcess);
    reader->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    const QString script = QStringLiteral("$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); "
        "@(Get-CimInstance Win32_Process -Filter \"Name='powershell.exe' OR Name='pwsh.exe'\" "
        "| ForEach-Object {$_.CommandLine}) | ConvertTo-Json -Compress");
    reader->start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", script});
    if (!reader->waitForFinished(5000)) { reader->kill(); reader->waitForFinished(1000); }
    if (reader->state() != QProcess::NotRunning) {
        // No destruir un QProcess vivo: esperar su final sin bloquear la ventana.
        QProcess* detached = reader.take();
        connect(detached, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), detached, &QObject::deleteLater);
        return "The update helpers could not be checked. Please try updating again.";
    }
    if (reader->exitStatus() != QProcess::NormalExit || reader->exitCode() != 0)
        return "The update helpers could not be checked. Please try updating again.";
    const QString rows = QString::fromUtf8(reader->readAllStandardOutput()).replace("\\\\", "/").replace('\\', '/');
    if (rows.contains(QDir::fromNativeSeparators(helperPrefix), Qt::CaseInsensitive))
        return "Wait for the current Theta Explorer update to finish before starting another.";
    return {};
#else
    // Las copias instaladas comparten cache; no barrerla mientras otra copia o helper la usa.
    const auto processRows = [](const QString& column, QHash<qint64, QString>& rows) {
        QProcess reader;
        reader.start("/bin/ps", {"-ww", "-axo", "pid=," + column + "="});
        if (!reader.waitForFinished(1000)) {
            reader.kill();
            reader.waitForFinished(1000);
            return false;
        }
        if (reader.exitStatus() != QProcess::NormalExit || reader.exitCode() != 0) return false;
        const QRegularExpression linePattern("^\\s*(\\d+)\\s+(.+)$");
        for (const QString& line : QString::fromUtf8(reader.readAllStandardOutput()).split('\n')) {
            const auto match = linePattern.match(line);
            if (match.hasMatch()) rows.insert(match.captured(1).toLongLong(), match.captured(2).trimmed());
        }
        return !rows.isEmpty();
    };
    QHash<qint64, QString> commands, arguments;
    if (!processRows("comm", commands) || !processRows("args", arguments)) {
        LOGW("ui") << "No se pudieron comprobar las otras instancias para actualizar.";
        return "The running copies could not be checked. Please try updating again.";
    }
    const QString executable = QFileInfo(QApplication::applicationFilePath()).fileName();
    const QString helperPrefix = QDir(LgaUpdateService::resolveUpdateDir(AppPaths::updateCacheDirectory()))
        .filePath("LGA_ThetaExplorer_Update_Helper_");
    const QRegularExpression helperPattern(QString("^/bin/bash\\s+[\"']?%1\\d+\\.sh[\"']?(?:\\s|$)")
        .arg(QRegularExpression::escape(helperPrefix)));
    for (auto it = commands.cbegin(); it != commands.cend(); ++it) {
        if (it.key() == QApplication::applicationPid()) continue;
        if (QFileInfo(it.value()).fileName() == executable) {
            return "Close the other Theta Explorer windows before updating.";
        }
        if (it.value() == "/bin/bash" && helperPattern.match(arguments.value(it.key())).hasMatch()) {
            return "Wait for the current Theta Explorer update to finish before starting another.";
        }
    }
    return {};
#endif
}

void MainWindow::setupUpdates()
{
    LgaUpdateConfig config;
    config.productId = "LGA_ThetaExplorer";
    config.displayName = "Theta Explorer";
    config.windowsExecutableName = "LGA_ThetaExplorer.exe";
    config.releaseRepoSlug = "legandrop/LGA_ThetaExplorer";
    config.settingsPath = AppPaths::settingsFile();
    config.updateCacheDir = AppPaths::updateCacheDirectory();
    config.automaticChecksEnabled = [] {
        return QSettings(AppPaths::settingsFile(), QSettings::IniFormat)
            .value("Updates/AutomaticChecks", true).toBool();
    };
    config.updateBlockedReason = [] {
        return AppPaths::runsFromDevelopmentTree(QApplication::applicationFilePath())
            ? QString("Updates are available for installed copies of Theta Explorer. "
                      "This copy is running from the development folder.") : QString();
    };
    config.installBlockedReason = [this] { return updateInstallBlockedReason(); };
    config.prepareForUpdatePrompt = [this] {
        if (m_helpDialog) m_helpDialog->reject();
    };
    config.quitApplication = [this] {
        if (close()) QApplication::quit();
    };
    m_updateService = new LgaUpdateService(config, this);
    m_updateService->scheduleAutomaticCheck();
}

void MainWindow::loadSettings()
{
    QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);

    const QByteArray geometry = settings.value("window/geometry").toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    const QByteArray mainState = settings.value("window/mainSplitter").toByteArray();
    if (!mainState.isEmpty()) {
        m_mainSplitter->restoreState(mainState);
    }

    const QString savedFolder = settings.value("downloads/folder").toString();
    if (!savedFolder.isEmpty()) {
        m_downloadFolder = savedFolder;
        updateFolderLabel();
    }
}

void MainWindow::saveSettings() const
{
    QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/mainSplitter", m_mainSplitter->saveState());
    settings.setValue("downloads/folder", m_downloadFolder);
}

void MainWindow::updateFolderLabel()
{
    // Recortada por el principio: lo que importa es la carpeta final.
    QString path = m_downloadFolder;
    const QString home = QDir::homePath();
    if (path.startsWith(home)) {
        path = "~" + path.mid(home.size());
    }
    m_folderLabel->ensurePolished();   // la fuente de 12 px sale del QSS
    const QFontMetrics fm(m_folderLabel->font());
    m_folderLabel->setText(fm.elidedText(path, Qt::ElideLeft, qMax(0, m_folderLabel->width())));
    m_folderBtn->setAccessibleName("Choose download folder");
    m_folderBtn->setAccessibleDescription(m_downloadFolder);
}

void MainWindow::updateGridPage()
{
    const bool hasCamera = m_service->isCameraConnected();
    // El minimo del cartel vacio no debe limitar la grilla mientras se navega.
    m_gridStack->setSizePolicy(hasCamera && !m_catalogGroups.isEmpty()
                                  ? QSizePolicy::Ignored : QSizePolicy::Preferred,
                              QSizePolicy::Preferred);
    if (!hasCamera) {
        m_emptyState->setContent("Connect your RICOH THETA",
            "Plug the camera in with a USB cable and turn it on. "
            "Its photos and videos will show up here.", true);
        m_gridStack->setCurrentIndex(1);
    } else if (!m_catalogLoaded) {
        m_emptyState->setContent("Loading camera files",
            "Reading photos and bracket metadata. This may take a moment.", false);
        m_gridStack->setCurrentIndex(1);
    } else if (m_catalogGroups.isEmpty()) {
        m_emptyState->setContent("The camera is empty",
            "Photos and videos you take will show up here.", false);
        m_gridStack->setCurrentIndex(1);
    } else {
        m_gridStack->setCurrentIndex(0);
    }
    snapBrowserWidth();
}

void MainWindow::onHelpRequested()
{
    HelpDialog dlg(m_downloadFolder, this, m_updateService);
    m_helpDialog = &dlg;
    updateHelpButtonIcon();
    dlg.exec();
    m_helpDialog.clear();
    updateHelpButtonIcon();
}

QString MainWindow::groupDownloadFolderName(const MediaAssetGroup& group) const
{
    const QString datePart = group.captureTime.isValid()
        ? group.captureTime.toString("yyMMdd")
        : QStringLiteral("000000");

    QString base = group.displayTitle;
    base.replace(QRegularExpression("[^A-Za-z0-9._-]+"), "_");
    base.remove(QRegularExpression("^_+|_+$"));
    if (base.isEmpty()) {
        base = "item";
    }

    if (group.isVideo) {
        return QStringLiteral("video_%1_%2_").arg(datePart, base);
    }
    if (group.isRaw) {
        return QStringLiteral("HDRI_%1_%2_dng").arg(datePart, base);
    }
    return QStringLiteral("HDRI_%1_%2_jpg").arg(datePart, base);
}

QString MainWindow::groupDownloadFolderPath(const MediaAssetGroup& group) const
{
    return QDir(m_downloadFolder).filePath(groupDownloadFolderName(group));
}

QList<CameraFileInfo> MainWindow::selectedFilesFlattened() const
{
    QList<CameraFileInfo> files;
    for (const MediaAssetGroup& group : m_selectedGroups) {
        files.append(group.files);
    }
    return files;
}

void MainWindow::refreshDownloadedStatus()
{
    for (MediaAssetGroup& group : m_catalogGroups) {
        updateGroupLocalStatus(group);
    }

    m_gridWidget->setGroups(m_catalogGroups);
}

void MainWindow::snapBrowserWidth()
{
    if (m_catalogGroups.isEmpty()) return;
    const QList<int> sizes = m_mainSplitter->sizes();
    const int total = sizes[0] + sizes[1];
    const int maximum = total - qMax(m_viewerContainer->minimumWidth(),
                                     m_viewerContainer->minimumSizeHint().width());
    const int snapped = m_gridWidget->snappedWidth(sizes[0], maximum);
    if (snapped != sizes[0]) m_mainSplitter->setSizes({snapped, total - snapped});
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Esperar la geometria de los hijos al restaurar o cambiar el tamano de ventana.
    QTimer::singleShot(0, this, &MainWindow::snapBrowserWidth);
}

void MainWindow::updateGroupLocalStatus(MediaAssetGroup& group) const
{
    int found = 0;
    int exactSizeMatches = 0;
    bool folderExists = false;

    for (const MediaAssetGroup& format : HdrGroupingService::downloadGroups(group)) {
        const QString folderPath = groupDownloadFolderPath(format);
        folderExists |= QFileInfo::exists(folderPath);
        for (const CameraFileInfo& file : format.files) {
            const QString matchedPath = findMatchingLocalFilePath(folderPath, file.name);
            if (matchedPath.isEmpty()) continue;
            ++found;

            const QFileInfo localInfo(matchedPath);
            if (file.sizeBytes > 0 && localInfo.exists() && localInfo.size() == file.sizeBytes) {
                ++exactSizeMatches;
            }
        }
    }

    group.downloadedFileCount = found;
    group.allFilesDownloaded = !group.files.isEmpty()
        && found == group.files.size()
        && exactSizeMatches == group.files.size();
    group.hasPartialLocalContent = !group.allFilesDownloaded
        && (found > 0 || folderExists);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::onCameraConnected(const QString& name)
{
    m_cameraDot->setMode(StatusDot::Mode::Connected);
    m_cameraLabel->setText(name);
    m_cameraLabel->setProperty("state", "connected");
    m_cameraLabel->style()->unpolish(m_cameraLabel);
    m_cameraLabel->style()->polish(m_cameraLabel);
    m_catalogLoaded = false;
    updateGridPage();
    setStatusMessage("Camera connected. Loading files...", ColorUtils::SUCCESS);
    updateButtonStates();
}

void MainWindow::onCameraDisconnected()
{
    m_cameraDot->setMode(StatusDot::Mode::Searching);
    m_cameraLabel->setText("Searching for camera…");
    m_cameraLabel->setProperty("state", "searching");
    m_cameraLabel->style()->unpolish(m_cameraLabel);
    m_cameraLabel->style()->polish(m_cameraLabel);
    m_batteryBox->hide();
    m_catalogLoaded = false;
    if (m_downloadInProgress) {
        LOGW("ui") << "Camera disconnected during a download:" << m_downloadDone
                   << "of" << m_downloadTotal << "files done";
    }
    m_downloadInProgress = false;
    m_downloadHasDeterminateProgress = false;
    m_progressBar->hide();
    m_progressLabel->hide();
    m_progressBar->setRange(0, 100);
    m_gridWidget->clearAll();
    m_previewPanel->clearPreview();
    m_metaPanel->clearMetadata();
    m_catalogFiles.clear();
    m_catalogGroups.clear();
    m_selectedGroups.clear();
    m_previewFileDevicePath.clear();
    updateGridPage();
    updateButtonStates();
    setStatusMessage("No camera connected.");
}

void MainWindow::onFileListReady(const QList<CameraFileInfo>& files)
{
    m_catalogFiles = files;
    m_catalogGroups = HdrGroupingService::buildGroups(files);
    refreshDownloadedStatus();
    m_previewPanel->clearPreview();
    m_metaPanel->clearMetadata();
    m_selectedGroups.clear();
    m_previewFileDevicePath.clear();
    m_catalogLoaded = true;
    updateGridPage();
    updateButtonStates();

    // Resumen por item (lo que se ve en la grilla), no por archivo.
    int photos = 0, raws = 0, vids = 0, hdrSets = 0;
    for (const auto& g : m_catalogGroups) {
        switch (g.kind) {
            case MediaAssetKind::SinglePhoto: photos++; break;
            case MediaAssetKind::SingleRaw:   raws++; break;
            case MediaAssetKind::Video:       vids++; break;
            case MediaAssetKind::HdrJpegSet:
            case MediaAssetKind::HdrRawSet:
            case MediaAssetKind::HdrCombinedSet: hdrSets++; break;
        }
    }
    QStringList parts;
    if (hdrSets) parts << QString("%1 HDR set%2").arg(hdrSets).arg(hdrSets != 1 ? "s" : "");
    if (photos)  parts << QString("%1 photo%2").arg(photos).arg(photos != 1 ? "s" : "");
    if (raws)    parts << QString("%1 RAW").arg(raws);
    if (vids)    parts << QString("%1 video%2").arg(vids).arg(vids != 1 ? "s" : "");
    QString summary = QString("%1 item%2 on camera")
        .arg(m_catalogGroups.size()).arg(m_catalogGroups.size() != 1 ? "s" : "");
    if (!parts.isEmpty()) {
        summary += ": " + parts.join(", ") + QString(" (%1 files)").arg(files.size());
    }
    setStatusMessage(summary);
}

void MainWindow::onSelectionChanged(const QList<MediaAssetGroup>& selected)
{
    m_service->cancelPendingPreview();
    m_selectedGroups = selected;
    m_previewFileDevicePath.clear();
    updateButtonStates();

    if (selected.isEmpty()) {
        m_previewPanel->clearPreview();
        m_metaPanel->clearMetadata();
    } else if (selected.size() == 1) {
        const MediaAssetGroup& group = selected.first();
        m_previewPanel->showGroup(group, QPixmap());
        m_metaPanel->showMetadata(group);
        m_service->requestThumbnail(group.representative);
        if (!group.representative.isVideo && !group.representative.isRaw) {
            m_service->requestPreview(group.representative);
        }
    } else {
        m_previewPanel->showMultipleSelection(selected.size());
        m_metaPanel->showMultipleSelection(selected.size());
    }
}

void MainWindow::onDownloadClicked()
{
    if (m_selectedGroups.isEmpty()) return;

    // Copia de la seleccion: mientras el cartel esta abierto una desconexion o un catalogo
    // nuevo vacian m_selectedGroups.
    const QList<MediaAssetGroup> selection = m_selectedGroups;

    LOGI("ui") << "Download requested. selectedGroups:" << selection.size()
               << "downloadRoot:" << m_downloadFolder;

    // Ensure download folder exists
    QDir().mkpath(m_downloadFolder);

    QList<MediaAssetGroup> groupsWithLocal;
    for (MediaAssetGroup group : selection) {
        updateGroupLocalStatus(group);
        if (group.allFilesDownloaded || group.hasPartialLocalContent) {
            groupsWithLocal.append(group);
        }
    }

    enum class OverwriteChoice { None, Replace, Skip };
    OverwriteChoice overwriteChoice = OverwriteChoice::None;
    if (!groupsWithLocal.isEmpty()) {
        const int existing = groupsWithLocal.size();
        const int total = selection.size();
        ConfirmDialog::Spec spec;
        spec.iconName = "folder_check";
        spec.title = (total == 1)
            ? QString("This item already has files in the download folder")
            : QString("%1 of %2 items already %3 files in the download folder")
                  .arg(existing).arg(total).arg(existing == 1 ? "has" : "have");
        spec.subtitle = (existing == 1)
            ? QString("Skip the files that are already there and download only what's missing, "
                      "or replace the local copy.")
            : QString("Skip the files that are already there and download only what's missing, "
                      "or replace the local copies.");
        spec.hintHtml = "Press <b>Enter</b> to skip, <b>Esc</b> to cancel.";
        spec.buttons = {
            {"Cancel (esc)", ConfirmDialog::ButtonStyle::Neutral},
            {"Replace", ConfirmDialog::ButtonStyle::Neutral},
            {"Skip Existing (enter)", ConfirmDialog::ButtonStyle::Action},
        };
        spec.defaultIndex = 2;
        spec.escapeIndex = 0;
        spec.noFocusIndexes = {1};   // Replace borra la carpeta local: solo con click

        const int choice = ConfirmDialog::ask(this, spec);
        if (choice == 1) {
            overwriteChoice = OverwriteChoice::Replace;
        } else if (choice == 2) {
            overwriteChoice = OverwriteChoice::Skip;
        } else {
            return;
        }
    }

    // La camara pudo desconectarse con el cartel abierto: no tocar nada local.
    if (!m_service->isCameraConnected()) {
        setStatusMessage("The camera was disconnected. Nothing was downloaded.", ColorUtils::WARNING);
        return;
    }

    // Que se encola por grupo. Con "Skip Existing" se saltean los ARCHIVOS que ya estan en
    // disco con el mismo tamano; un set a medio bajar se completa. (Antes de v0.994 se
    // calculaba la lista pero se encolaban todos los grupos igual.)
    QList<QPair<MediaAssetGroup, QList<CameraFileInfo>>> queue;
    QList<MediaAssetGroup> formats;
    for (const MediaAssetGroup& group : selection) {
        formats.append(HdrGroupingService::downloadGroups(group));
    }
    int filesToDownload = 0;
    for (const MediaAssetGroup& group : formats) {
        QList<CameraFileInfo> files = group.files;
        if (overwriteChoice == OverwriteChoice::Skip) {
            files.clear();
            const QString folder = groupDownloadFolderPath(group);
            for (const CameraFileInfo& file : group.files) {
                const QString local = findMatchingLocalFilePath(folder, file.name);
                const bool complete = !local.isEmpty()
                    && (file.sizeBytes <= 0 || QFileInfo(local).size() == file.sizeBytes);
                if (!complete) {
                    files.append(file);
                }
            }
        }
        if (!files.isEmpty()) {
            filesToDownload += files.size();
            queue.append({group, files});
        }
    }

    if (filesToDownload == 0) {
        setStatusMessage("Nothing to download. All selected files are already in the download folder.");
        return;
    }

    if (overwriteChoice == OverwriteChoice::Replace) {
        for (const MediaAssetGroup& group : groupsWithLocal) {
            for (const MediaAssetGroup& format : HdrGroupingService::downloadGroups(group)) {
                LOGI("ui") << "Reemplazando carpeta local antes de descargar:"
                           << groupDownloadFolderPath(format);
                QDir(groupDownloadFolderPath(format)).removeRecursively();
            }
        }
    }

    m_downloadTotal = filesToDownload;
    m_downloadDone  = 0;
    m_downloadErrors = 0;
    m_lastDownloadError.clear();
    m_downloadHasDeterminateProgress = false;
    m_downloadInProgress = true;

    m_progressBar->setRange(0, 0);
    m_progressLabel->setText(QString("Downloading %1 file%2...")
        .arg(m_downloadTotal)
        .arg(m_downloadTotal != 1 ? "s" : ""));
    m_progressBar->show();
    m_progressLabel->show();
    updateButtonStates();

    for (const auto& entry : queue) {
        const QString targetFolder = groupDownloadFolderPath(entry.first);
        QDir().mkpath(targetFolder);
        LOGI("ui") << "Queueing group download:"
                   << entry.first.displayTitle
                   << "files:" << entry.second.size()
                   << "targetFolder:" << targetFolder;
        m_service->downloadFiles(entry.second, targetFolder);
    }
    setStatusMessage(QString("Downloading %1 file%2 to %3...")
        .arg(m_downloadTotal)
        .arg(m_downloadTotal != 1 ? "s" : "")
        .arg(m_downloadFolder));
}

void MainWindow::onDeleteClicked()
{
    if (m_selectedGroups.isEmpty()) return;
    QList<CameraFileInfo> filesToDelete = selectedFilesFlattened();

    // Un set HDR son varios archivos: el cartel dice el total real que se borra.
    const int items = m_selectedGroups.size();
    const int files = filesToDelete.size();
    int hdrSets = 0;
    int hdrImages = 0;
    for (const MediaAssetGroup& group : m_selectedGroups) {
        if (group.isHdrSet) {
            ++hdrSets;
            hdrImages = group.bracketCount();
        }
    }
    QString detail;
    if (files != items) {
        detail = QString("That's %1 files").arg(files);
        if (hdrSets == 1) {
            detail += QString(", including a %1-bracket HDRI").arg(hdrImages);
        } else if (hdrSets > 1) {
            detail += QString(", including %1 HDR sets").arg(hdrSets);
        }
        detail += ". ";
    }

    ConfirmDialog::Spec spec;
    spec.iconName = "trash";
    spec.title = (items == 1) ? QString("Delete this item from the camera?")
                              : QString("Delete %1 items from the camera?").arg(items);
    spec.subtitle = detail + "This can't be undone.";
    spec.hintHtml = "Press <b>Enter</b> or <b>Esc</b> to cancel. Deleting takes a click.";
    spec.buttons = {
        {"Cancel (esc)", ConfirmDialog::ButtonStyle::Neutral},
        {"Delete", ConfirmDialog::ButtonStyle::Danger},
    };
    // Enter cancela: borrar de la camara no tiene vuelta atras.
    spec.defaultIndex = 0;
    spec.escapeIndex = 0;
    spec.noFocusIndexes = {1};   // con Tab + Espacio se borraria sin ver el foco
    if (ConfirmDialog::ask(this, spec) != 1) return;

    QStringList names;
    for (const CameraFileInfo& file : filesToDelete) {
        names << file.name;
    }
    LOGI("ui") << "Delete requested for" << filesToDelete.size()
               << "file(s):" << names;

    m_deleteBtn->setEnabled(false);
    m_downloadBtn->setEnabled(false);
    setStatusMessage(QString("Deleting %1 file%2 from camera...")
        .arg(filesToDelete.size())
        .arg(filesToDelete.size() != 1 ? "s" : ""),
        ColorUtils::WARNING);

    m_service->deleteFiles(filesToDelete);
}

void MainWindow::onBrowseFolderClicked()
{
    LOGD("ui") << "Browse folder clicked. currentFolder=" << m_downloadFolder;

    const QString chosen = chooseDirectoryNonNative(
        this,
        "Choose download folder",
        m_downloadFolder
    );

    if (!chosen.isEmpty()) {
        m_downloadFolder = chosen;
        updateFolderLabel();
        saveSettings();
        refreshDownloadedStatus();
        LOGI("ui") << "Browse folder selected:" << m_downloadFolder;
    } else {
        LOGD("ui") << "Browse folder canceled by user";
    }
}

void MainWindow::onRefreshClicked()
{
    if (m_downloadInProgress || m_service->isDownloadActive()) {
        setStatusMessage("Cannot refresh while a download is in progress.", ColorUtils::WARNING);
        return;
    }

    LOGI("ui") << "Manual camera refresh requested";
    setStatusMessage("Refreshing camera catalog...");
    m_service->refresh();
}

void MainWindow::onDownloadProgress(const QString& fileName, int percent)
{
    if (!m_downloadHasDeterminateProgress) {
        m_downloadHasDeterminateProgress = true;
        m_progressBar->setRange(0, m_downloadTotal * 100);
    }

    m_progressBar->setValue(m_downloadDone * 100 + percent);
    m_progressLabel->setText(
        QString("%1 / %2  %3%  %4")
            .arg(m_downloadDone)
            .arg(m_downloadTotal)
            .arg(percent)
            .arg(fileName)
    );
}

void MainWindow::onDownloadFileCompleted(const QString& fileName, const QString& path)
{
    m_downloadDone++;
    if (m_downloadHasDeterminateProgress) {
        m_progressBar->setValue(m_downloadDone * 100);
    }
    m_progressLabel->setText(
        QString("%1 / %2 done  %3").arg(m_downloadDone).arg(m_downloadTotal).arg(fileName)
    );
    LOGD("ui") << "Downloaded:" << fileName << "->" << path;

    if (m_downloadInProgress && m_downloadDone == m_downloadTotal) {
        finishDownloadBatch();
    }
}

void MainWindow::onDownloadError(const QString& fileName, const QString& error)
{
    m_downloadDone++;
    m_downloadErrors++;
    m_lastDownloadError = fileName + ": " + error;
    LOGW("ui") << "Download error surfaced to UI:"
               << fileName
               << "completed:" << m_downloadDone
               << "of" << m_downloadTotal
               << "message:" << error;
    setStatusMessage("Download error: " + fileName + " - " + error, ColorUtils::ERROR_COLOR);
    if (m_downloadInProgress && m_downloadDone == m_downloadTotal) {
        finishDownloadBatch();
    }
}

void MainWindow::finishDownloadBatch()
{
    m_downloadInProgress = false;
    m_progressBar->hide();
    m_progressLabel->hide();
    m_progressBar->setRange(0, 100);
    m_downloadHasDeterminateProgress = false;
    refreshDownloadedStatus();
    updateButtonStates();

    // Un solo aviso por lote, no uno por archivo.
    const int ok = m_downloadDone - m_downloadErrors;
    if (m_downloadErrors == 0) {
        const QString msg = QString("Downloaded %1 file%2 to %3")
            .arg(ok).arg(ok != 1 ? "s" : "").arg(m_downloadFolder);
        setStatusMessage(msg, ColorUtils::SUCCESS);
        notifySuccess(QString("Downloaded %1 file%2.").arg(ok).arg(ok != 1 ? "s" : ""));
    } else {
        notifyError(QString("<b>%1 of %2 files couldn't be downloaded.</b><br>%3")
            .arg(m_downloadErrors).arg(m_downloadTotal)
            .arg(m_lastDownloadError.toHtmlEscaped()));
    }
}

void MainWindow::onDeleteCompleted(const QStringList& deletedPaths)
{
    // Se saca lo borrado del catalogo y se reconstruyen los grupos: no se depende de que la
    // camara vuelva a enumerar (no siempre lo hace).
    QList<CameraFileInfo> remaining;
    for (const CameraFileInfo& file : m_catalogFiles) {
        if (!deletedPaths.contains(file.devicePath)) {
            remaining.append(file);
        }
    }
    m_catalogFiles = remaining;
    m_catalogGroups = HdrGroupingService::buildGroups(m_catalogFiles);
    refreshDownloadedStatus();
    m_previewPanel->clearPreview();
    m_metaPanel->clearMetadata();
    m_selectedGroups.clear();
    m_previewFileDevicePath.clear();
    updateGridPage();
    updateButtonStates();
    const QString msg = QString("Deleted %1 file%2 from the camera.")
        .arg(deletedPaths.size())
        .arg(deletedPaths.size() != 1 ? "s" : "");
    setStatusMessage(msg, ColorUtils::SUCCESS);
    notifySuccess(msg);
}

void MainWindow::onErrorOccurred(const QString& message)
{
    LOGW("ui") << "Error:" << message;
    setStatusMessage(message, ColorUtils::ERROR_COLOR);
    notifyError(message.toHtmlEscaped());
    // Los errores de descarga llegan por onDownloadError; estos (camara, borrar) no cortan
    // un lote en curso: si la camara se va, onCameraDisconnected resetea el estado.
    updateButtonStates();
}

void MainWindow::onBatteryLevelChanged(int percent, bool available)
{
    if (!available || percent < 0) {
        m_batteryBox->hide();
        return;
    }

    const bool low = percent <= kLowBatteryPercent;
    const QColor color(low ? ColorUtils::ERROR_COLOR : ColorUtils::TXT_SECUNDARIO);
    m_batteryIcon->setPixmap(UiIcons::pixmap("battery", 18, color, devicePixelRatioF()));
    m_batteryLabel->setText(QString("%1%").arg(percent));
    m_batteryLabel->setProperty("low", low);
    m_batteryLabel->style()->unpolish(m_batteryLabel);
    m_batteryLabel->style()->polish(m_batteryLabel);
    m_batteryBox->setVisible(m_service->isCameraConnected());
}

void MainWindow::updateButtonStates()
{
    bool hasCamera   = m_service->isCameraConnected();
    bool hasSelected = !m_selectedGroups.isEmpty();
    const bool busy = m_downloadInProgress || m_service->isDownloadActive() || m_service->isDeleteActive();
    m_downloadBtn->setEnabled(hasCamera && hasSelected && !busy);
    m_deleteBtn->setEnabled(hasCamera && hasSelected && !busy);
    m_folderBtn->setEnabled(!busy);
#ifdef Q_OS_WIN
    // Permitir reiniciar la busqueda aunque el auxiliar se haya detenido o no haya camara.
    m_refreshBtn->setEnabled(!busy);
#else
    m_refreshBtn->setEnabled(hasCamera && !busy);
#endif
    m_downloadBtn->setText(hasSelected ? QString("Download %1").arg(m_selectedGroups.size())
                                       : QString("Download"));
    m_selectionLabel->setText(hasSelected
        ? QString("%1 item%2 selected").arg(m_selectedGroups.size())
            .arg(m_selectedGroups.size() == 1 ? "" : "s")
        : QString("No selection"));
    m_folderIcon->setPixmap(UiIcons::pixmap("folder", kToolbarIconSize,
        QColor(busy ? kIconNeutralDisabled : kIconNeutral), devicePixelRatioF()));
}

void MainWindow::setStatusMessage(const QString& msg, const QString& color)
{
    const QString c = color.isEmpty() ? QString(ColorUtils::TXT_SECUNDARIO) : color;
    m_statusLabel->setStyleSheet(QString("QLabel#statusLabel { color: %1; }").arg(c));
    m_statusLabel->setText(msg);
}

void MainWindow::notifyError(const QString& html)
{
    // Un error nuevo reemplaza al que este abierto, para no apilar avisos.
    if (m_errorPopover) {
        m_errorPopover->closePopover();
    }
    m_errorPopover = new MessagePopover(this);
    m_errorPopover->setMessage(html);
    m_errorPopover->showPopover();   // los errores no se cierran solos
}

void MainWindow::notifySuccess(const QString& msg)
{
    auto* popover = new MessagePopover(this);
    popover->setMessage(msg.toHtmlEscaped());
    popover->showPopover(4000);
}
