#pragma once

#include <QObject>
#include <QMainWindow>

class ShortcutManager : public QObject {
    Q_OBJECT

public:
    explicit ShortcutManager(QMainWindow* window, QObject* parent = nullptr);

signals:
    void playPause();
    void stepForward1();
    void stepBackward1();
    void stepForward10();
    void stepBackward10();
    void stepForward50();
    void stepBackward50();
    void goToFirst();
    void goToEnd();
    void markIn();
    void markOut();
    void jumpToIn();
    void jumpToOut();
    void clearIn();
    void clearOut();
    void exportClip();
    void setSpeed025();
    void setSpeed05();
    void setSpeed1();
    void setSpeed2();
    void setSpeed4();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};
