#include "video_widget.h"
#include <QVideoWidget>
#include <QVideoSink>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>

namespace videoengine {

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    videoWidget_ = new QVideoWidget;
    playPauseBtn_ = new QPushButton("Play");
    stepBackBtn_ = new QPushButton("<|");
    stepFwdBtn_ = new QPushButton("|>");
    seekSlider_ = new QSlider(Qt::Horizontal);
    timeLabel_ = new QLabel("00:00 / 00:00");

    seekSlider_->setRange(0, 1000); // 0.1% granularity

    auto* controls = new QHBoxLayout;
    controls->addWidget(stepBackBtn_);
    controls->addWidget(playPauseBtn_);
    controls->addWidget(stepFwdBtn_);
    controls->addWidget(seekSlider_, 1);
    controls->addWidget(timeLabel_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(videoWidget_, 1);
    layout->addLayout(controls);

    // Play/Pause toggle
    connect(playPauseBtn_, &QPushButton::clicked, this, [this] {
        if (playing_) emit pauseClicked();
        else emit playClicked();
    });

    // Step buttons
    connect(stepFwdBtn_, &QPushButton::clicked,
            this, &VideoWidget::stepForwardClicked);
    connect(stepBackBtn_, &QPushButton::clicked,
            this, &VideoWidget::stepBackwardClicked);

    // Seek slider: emit seek on release to avoid flooding during drag
    connect(seekSlider_, &QSlider::sliderPressed, this, [this] {
        sliderDragging_ = true;
    });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this] {
        sliderDragging_ = false;
        if (durationUs_ > 0) {
            int64_t us = static_cast<int64_t>(seekSlider_->value())
                         * durationUs_ / 1000;
            emit seekRequested(us);
        }
    });

    setFocusPolicy(Qt::StrongFocus);
}

QVideoSink* VideoWidget::videoSink() const
{
    return videoWidget_->videoSink();
}

void VideoWidget::setPlaying(bool playing)
{
    playing_ = playing;
    playPauseBtn_->setText(playing ? "Pause" : "Play");
    stepBackBtn_->setEnabled(!playing);
    stepFwdBtn_->setEnabled(!playing);
}

void VideoWidget::setPositionUs(int64_t us)
{
    // Don't fight the user while they're dragging
    if (!sliderDragging_ && durationUs_ > 0) {
        int val = static_cast<int>(us * 1000 / durationUs_);
        seekSlider_->setValue(val);
    }

    int posSec = static_cast<int>(us / 1'000'000);
    int durSec = static_cast<int>(durationUs_ / 1'000'000);
    timeLabel_->setText(
        QString("%1:%2 / %3:%4")
            .arg(posSec / 60, 2, 10, QChar('0'))
            .arg(posSec % 60, 2, 10, QChar('0'))
            .arg(durSec / 60, 2, 10, QChar('0'))
            .arg(durSec % 60, 2, 10, QChar('0')));
}

void VideoWidget::setDurationUs(int64_t us)
{
    durationUs_ = us;
    setPositionUs(0);
}

void VideoWidget::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        if (playing_) emit pauseClicked();
        else emit playClicked();
        break;
    case Qt::Key_Right:
        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift+Right: seek +1 second
            emit seekRequested(
                std::min(durationUs_,
                         static_cast<int64_t>(seekSlider_->value())
                             * durationUs_ / 1000 + 1'000'000));
        } else {
            emit stepForwardClicked();
        }
        break;
    case Qt::Key_Left:
        if (event->modifiers() & Qt::ShiftModifier) {
            // Shift+Left: seek -1 second
            emit seekRequested(
                std::max(int64_t{0},
                         static_cast<int64_t>(seekSlider_->value())
                             * durationUs_ / 1000 - 1'000'000));
        } else {
            emit stepBackwardClicked();
        }
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace videoengine
