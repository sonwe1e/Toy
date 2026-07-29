#include "app/ShortcutManager.h"
#include <QKeyEvent>

ShortcutManager::ShortcutManager(QMainWindow* window, QObject* parent)
    : QObject(parent) {
    window->installEventFilter(this);
}

bool ShortcutManager::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(obj, event);
    }

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    int key = keyEvent->key();
    Qt::KeyboardModifiers mods = keyEvent->modifiers();

    if (mods == Qt::NoModifier) {
        switch (key) {
            case Qt::Key_Space:  emit playPause(); return true;
            case Qt::Key_Left:   emit stepBackward1(); return true;
            case Qt::Key_Right:  emit stepForward1(); return true;
            case Qt::Key_Up:     emit stepForward10(); return true;
            case Qt::Key_Down:   emit stepBackward10(); return true;
            case Qt::Key_Home:   emit goToFirst(); return true;
            case Qt::Key_End:    emit goToEnd(); return true;
            case Qt::Key_I:      emit markIn(); return true;
            case Qt::Key_O:      emit markOut(); return true;
            case Qt::Key_E:      emit exportClip(); return true;
            case Qt::Key_1:      emit setSpeed025(); return true;
            case Qt::Key_2:      emit setSpeed05(); return true;
            case Qt::Key_3:      emit setSpeed1(); return true;
            case Qt::Key_4:      emit setSpeed2(); return true;
            case Qt::Key_5:      emit setSpeed4(); return true;
        }
    }

    if (mods == Qt::ShiftModifier) {
        switch (key) {
            case Qt::Key_Left:  emit stepBackward50(); return true;
            case Qt::Key_Right: emit stepForward50(); return true;
            case Qt::Key_I:     emit jumpToIn(); return true;
            case Qt::Key_O:     emit jumpToOut(); return true;
        }
    }

    if (mods == Qt::AltModifier) {
        switch (key) {
            case Qt::Key_I: emit clearIn(); return true;
            case Qt::Key_O: emit clearOut(); return true;
        }
    }

    return QObject::eventFilter(obj, event);
}
