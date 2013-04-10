#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>

class QScrollArea;
class QAction;
class QMenu;
class WaveView;
class WaveList;

class MainWindow : public QObject
{
    Q_OBJECT
public:
    MainWindow();
    void init(const WaveList & list);
private:
    void updateActions();
    void updateView();

    QMainWindow mainWindow;
    QScrollArea * scrollArea;
    WaveView * waveView; 
    QAction * zoomInAction;
    QAction * zoomOutAction;
    QMenu * viewMenu;
private slots:
    void zoomIn();
    void zoomOut();
};

#endif
