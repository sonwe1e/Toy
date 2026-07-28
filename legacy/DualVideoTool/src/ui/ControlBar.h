#pragma once

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>

class ControlBar : public QWidget {
    Q_OBJECT

public:
    explicit ControlBar(QWidget* parent = nullptr);

    void setPlaying(bool playing);
    void setProxyReady(bool ready);
    void setProxyBusy(bool busy);

signals:
    void openVideoAClicked();
    void openVideoBClicked();
    void buildProxyClicked();
    void previousFrameClicked();
    void nextFrameClicked();
    void playPauseClicked();
    void markInClicked();
    void markOutClicked();
    void addClipToQueueClicked();
    void exportClipClicked();
    void batchExportQueueClicked();
    void speedChanged(double speed);

private:
    QPushButton* openABtn_;
    QPushButton* openBBtn_;
    QPushButton* buildProxyBtn_;
    QPushButton* previousFrameBtn_;
    QPushButton* nextFrameBtn_;
    QPushButton* playPauseBtn_;
    QPushButton* markInBtn_;
    QPushButton* markOutBtn_;
    QPushButton* addQueueBtn_;
    QPushButton* exportBtn_;
    QPushButton* batchExportBtn_;
    QComboBox* speedCombo_;
};
