#include "ui/ControlBar.h"

#include <QPushButton>
#include <QComboBox>
#include <QHBoxLayout>

ControlBar::ControlBar(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);

    openABtn_ = new QPushButton("打开 A");
    openBBtn_ = new QPushButton("打开 B");
    buildProxyBtn_ = new QPushButton("生成代理");
    previousFrameBtn_ = new QPushButton("上一帧");
    nextFrameBtn_ = new QPushButton("下一帧");
    playPauseBtn_ = new QPushButton("播放");
    markInBtn_ = new QPushButton("入点");
    markOutBtn_ = new QPushButton("出点");
    addQueueBtn_ = new QPushButton("加入队列");
    exportBtn_ = new QPushButton("导出");
    batchExportBtn_ = new QPushButton("批量导出");

    speedCombo_ = new QComboBox();
    speedCombo_->addItem("0.25x", 0.25);
    speedCombo_->addItem("0.5x", 0.5);
    speedCombo_->addItem("1.0x", 1.0);
    speedCombo_->addItem("2.0x", 2.0);
    speedCombo_->addItem("4.0x", 4.0);
    speedCombo_->setCurrentIndex(2); // 1.0x

    layout->addWidget(openABtn_);
    layout->addWidget(openBBtn_);
    layout->addWidget(buildProxyBtn_);
    layout->addSpacing(12);
    layout->addWidget(previousFrameBtn_);
    layout->addWidget(playPauseBtn_);
    layout->addWidget(nextFrameBtn_);
    layout->addSpacing(12);
    layout->addWidget(markInBtn_);
    layout->addWidget(markOutBtn_);
    layout->addWidget(addQueueBtn_);
    layout->addWidget(exportBtn_);
    layout->addWidget(batchExportBtn_);
    layout->addStretch();
    layout->addWidget(speedCombo_);

    connect(openABtn_, &QPushButton::clicked, this, &ControlBar::openVideoAClicked);
    connect(openBBtn_, &QPushButton::clicked, this, &ControlBar::openVideoBClicked);
    connect(buildProxyBtn_, &QPushButton::clicked, this, &ControlBar::buildProxyClicked);
    connect(previousFrameBtn_, &QPushButton::clicked, this, &ControlBar::previousFrameClicked);
    connect(nextFrameBtn_, &QPushButton::clicked, this, &ControlBar::nextFrameClicked);
    connect(playPauseBtn_, &QPushButton::clicked, this, &ControlBar::playPauseClicked);
    connect(markInBtn_, &QPushButton::clicked, this, &ControlBar::markInClicked);
    connect(markOutBtn_, &QPushButton::clicked, this, &ControlBar::markOutClicked);
    connect(addQueueBtn_, &QPushButton::clicked, this, &ControlBar::addClipToQueueClicked);
    connect(exportBtn_, &QPushButton::clicked, this, &ControlBar::exportClipClicked);
    connect(batchExportBtn_, &QPushButton::clicked, this, &ControlBar::batchExportQueueClicked);

    connect(speedCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        double speed = speedCombo_->itemData(index).toDouble();
        emit speedChanged(speed);
    });
}

void ControlBar::setPlaying(bool playing) {
    playPauseBtn_->setText(playing ? "暂停" : "播放");
}

void ControlBar::setProxyReady(bool ready) {
    previousFrameBtn_->setEnabled(ready);
    nextFrameBtn_->setEnabled(ready);
    playPauseBtn_->setEnabled(ready);
    markInBtn_->setEnabled(ready);
    markOutBtn_->setEnabled(ready);
    addQueueBtn_->setEnabled(ready);
    exportBtn_->setEnabled(ready);
    batchExportBtn_->setEnabled(ready);
}

void ControlBar::setProxyBusy(bool busy) {
    buildProxyBtn_->setEnabled(!busy);
    buildProxyBtn_->setText(busy ? "生成中..." : "生成代理");
}
