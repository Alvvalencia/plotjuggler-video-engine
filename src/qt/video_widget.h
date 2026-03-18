#ifndef VIDEOENGINE_QT_VIDEO_WIDGET_H
#define VIDEOENGINE_QT_VIDEO_WIDGET_H

#include <QWidget>

class QVideoWidget;
class QPushButton;
class QLabel;
class QSlider;
class QVideoSink;

namespace videoengine {

// Composite widget: QVideoWidget + play/pause + step buttons + seek slider + time label.
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);

    QVideoSink* videoSink() const;

public slots:
    void setPlaying(bool playing);
    void setPositionUs(int64_t us);
    void setDurationUs(int64_t us);

signals:
    void playClicked();
    void pauseClicked();
    void stepForwardClicked();
    void stepBackwardClicked();
    void seekRequested(int64_t us);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QVideoWidget* videoWidget_;
    QPushButton* playPauseBtn_;
    QPushButton* stepBackBtn_;
    QPushButton* stepFwdBtn_;
    QSlider* seekSlider_;
    QLabel* timeLabel_;
    int64_t durationUs_ = 0;
    bool playing_ = false;
    bool sliderDragging_ = false;
};

} // namespace videoengine

#endif // VIDEOENGINE_QT_VIDEO_WIDGET_H
