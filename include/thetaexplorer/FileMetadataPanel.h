#pragma once
#include <QWidget>
#include <QLabel>
#include "thetaexplorer/MediaAssetGroup.h"

class FileMetadataPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FileMetadataPanel(QWidget* parent = nullptr);

    void showMetadata(const MediaAssetGroup& group);
    void showMultipleSelection(int count);
    void clearMetadata();
    void setDimensions(int width, int height);

private:
    QLabel* makeValue(const QString& field);
    void    setRow(QLabel* val, const QString& text);

    QLabel* m_nameVal  = nullptr;
    QLabel* m_typeVal  = nullptr;
    QLabel* m_sizeVal  = nullptr;
    QLabel* m_dateVal  = nullptr;
    QLabel* m_dimVal   = nullptr;
    QLabel* m_pathVal  = nullptr;
    QLabel* m_downloadVal = nullptr;
};
