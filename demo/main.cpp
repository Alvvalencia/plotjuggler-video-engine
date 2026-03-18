#include <QApplication>
#include <QTimer>
#include <iostream>

#include "../src/FFmpegBackend.h"
#include "../src/qt/video_widget.h"
#include "VideoEngine/VideoController.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_file>\n";
        return 1;
    }

    auto backend = std::make_unique<videoengine::FFmpegBackend>();
    videoengine::VideoController ctrl(std::move(backend));

    videoengine::VideoWidget widget;
    widget.setWindowTitle("VideoEngine Demo");
    widget.resize(960, 600);
    ctrl.connectToSink(widget.videoSink());

    // Play / Pause
    QObject::connect(&widget, &videoengine::VideoWidget::playClicked, [&] {
        ctrl.play();
        widget.setPlaying(true);
    });
    QObject::connect(&widget, &videoengine::VideoWidget::pauseClicked, [&] {
        ctrl.pause();
        widget.setPlaying(false);
    });

    // Step
    QObject::connect(&widget, &videoengine::VideoWidget::stepForwardClicked, [&] {
        ctrl.pause();
        widget.setPlaying(false);
        ctrl.stepForward();
        widget.setPositionUs(ctrl.getPositionUs());
    });
    QObject::connect(&widget, &videoengine::VideoWidget::stepBackwardClicked, [&] {
        ctrl.pause();
        widget.setPlaying(false);
        ctrl.stepBackward();
        widget.setPositionUs(ctrl.getPositionUs());
    });

    // Seek
    QObject::connect(&widget, &videoengine::VideoWidget::seekRequested, [&](int64_t us) {
        ctrl.seek(us);
        widget.setPositionUs(ctrl.getPositionUs());
    });

    // Position update timer
    QTimer posTimer;
    posTimer.setInterval(100);
    QObject::connect(&posTimer, &QTimer::timeout, [&] {
        widget.setPositionUs(ctrl.getPositionUs());
    });

    if (!ctrl.open(QString::fromUtf8(argv[1]))) {
        std::cerr << "Failed to open: " << argv[1] << "\n";
        return 1;
    }
    widget.setDurationUs(ctrl.getDurationUs());
    std::cout << "Duration: " << ctrl.getDurationUs() / 1'000'000 << "s\n";

    widget.show();
    posTimer.start();

    return app.exec();
}
