#pragma once
#include <QWidget>
#include <QLabel>
#include "thetaexplorer/MediaAssetGroup.h"

class ThumbnailTileWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected)
public:
    static constexpr int WIDTH = 180;
    static constexpr int HEIGHT = 160;
    static constexpr int THUMBNAIL_HEIGHT = 100;
    static constexpr int FOOTER_TOP = 112;

    explicit ThumbnailTileWidget(const MediaAssetGroup& group, QWidget* parent = nullptr);

    const MediaAssetGroup& group() const { return m_group; }
    bool  isSelected() const { return m_selected; }
    void  setSelected(bool s);
    void  setThumbnail(const QPixmap& pixmap);

signals:
    void clicked(ThumbnailTileWidget* self, Qt::KeyboardModifiers mods);
    void doubleClicked(ThumbnailTileWidget* self);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void updateStyleState();
    QString buildDateHtml() const;
    QString ageColor(const QDateTime& timestamp) const;

    MediaAssetGroup m_group;
    QLabel*        m_thumb     = nullptr;
    QLabel*        m_nameLabel = nullptr;
    QLabel*        m_datesLabel = nullptr;
    QLabel*        m_downloadedBadge = nullptr;
    QLabel*        m_formatBadge = nullptr;
    bool           m_selected  = false;
    bool           m_hovered   = false;
};
