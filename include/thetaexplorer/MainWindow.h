#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QPointer>
#include "thetaexplorer/CameraFileInfo.h"
#include "thetaexplorer/MediaAssetGroup.h"

class ThetaCameraService;
class ThumbnailGridWidget;
class PreviewPanel;
class FileMetadataPanel;
class QTimer;
class QStackedWidget;
class StatusDot;
class EmptyStateWidget;
class MessagePopover;
class LgaUpdateService;
class HelpDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onCameraConnected(const QString& name);
    void onCameraDisconnected();
    void onFileListReady(const QList<CameraFileInfo>& files);
    void onSelectionChanged(const QList<MediaAssetGroup>& selected);
    void onDownloadClicked();
    void onDeleteClicked();
    void onBrowseFolderClicked();
    void onRefreshClicked();
    void onDownloadProgress(const QString& fileName, int percent);
    void onDownloadFileCompleted(const QString& fileName, const QString& path);
    void onDownloadError(const QString& fileName, const QString& error);
    void onDeleteCompleted(const QStringList& deletedPaths);
    void onErrorOccurred(const QString& message);
    void onBatteryLevelChanged(int percent, bool available);
    void onHelpRequested();

private:
    void setupUI();
    void setupMenus();
    void setupConnections();
    void applyButtonIcons();
    void updateHelpButtonIcon();
    void updateRefreshButtonIcon();
    void setupUpdates();
    void loadSettings();
    void saveSettings() const;
    void updateFolderLabel();
    void refreshDownloadedStatus();
    void updateGroupLocalStatus(MediaAssetGroup& group) const;
    QString groupDownloadFolderName(const MediaAssetGroup& group) const;
    QString groupDownloadFolderPath(const MediaAssetGroup& group) const;
    QList<CameraFileInfo> selectedFilesFlattened() const;
    void updateButtonStates();
    void finishDownloadBatch();
    void updateGridPage();
    void snapBrowserWidth();
    // Barra de estado. `color` vacio = color secundario de la paleta.
    void setStatusMessage(const QString& msg, const QString& color = QString());
    // Aviso flotante ademas de la barra de estado. Los errores no se cierran solos.
    void notifyError(const QString& msg);
    void notifySuccess(const QString& msg);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    QString updateInstallBlockedReason() const;

    // Service
    ThetaCameraService*  m_service        = nullptr;
    LgaUpdateService*    m_updateService  = nullptr;
    QPointer<HelpDialog> m_helpDialog;
    bool                m_helpHovered = false;
    bool                m_refreshHovered = false;

    // Main layout widgets
    QWidget*             m_centralWidget  = nullptr;
    QSplitter*           m_mainSplitter   = nullptr;
    QWidget*             m_viewerContainer = nullptr;
    ThumbnailGridWidget* m_gridWidget     = nullptr;
    PreviewPanel*        m_previewPanel   = nullptr;
    QStackedWidget*      m_gridStack      = nullptr;
    EmptyStateWidget*    m_emptyState     = nullptr;
    FileMetadataPanel*   m_metaPanel      = nullptr;

    // Toolbar
    QWidget*             m_toolbar        = nullptr;
    StatusDot*           m_cameraDot      = nullptr;
    QLabel*              m_cameraLabel    = nullptr;
    QWidget*             m_batteryBox     = nullptr;
    QLabel*              m_batteryIcon    = nullptr;
    QLabel*              m_batteryLabel   = nullptr;
    QPushButton*         m_helpBtn        = nullptr;
    QPushButton*         m_folderBtn      = nullptr;
    QPushButton*         m_refreshBtn     = nullptr;
    QLabel*              m_folderLabel    = nullptr;
    QLabel*              m_folderIcon     = nullptr;
    QLabel*              m_selectionLabel = nullptr;
    QPushButton*         m_downloadBtn    = nullptr;
    QPushButton*         m_deleteBtn      = nullptr;
    QProgressBar*        m_progressBar    = nullptr;
    QLabel*              m_progressLabel  = nullptr;
    bool                 m_downloadHasDeterminateProgress = false;

    // Status bar label
    QLabel*              m_statusLabel    = nullptr;

    // State
    QString              m_downloadFolder;
    QList<CameraFileInfo> m_catalogFiles;
    QList<MediaAssetGroup> m_catalogGroups;
    QList<MediaAssetGroup> m_selectedGroups;
    QString              m_previewFileDevicePath;
    int                  m_downloadTotal  = 0;
    int                  m_downloadDone   = 0;
    bool                 m_downloadInProgress = false;
    bool                 m_catalogLoaded  = false;
    int                  m_downloadErrors = 0;
    QString              m_lastDownloadError;
    QPointer<MessagePopover> m_errorPopover;
};
