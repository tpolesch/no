#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>

class QScrollArea;
class QAction;
class QMenu;
class WaveView;
class ActiveWaves;

class MainWindow : public QObject
{
    Q_OBJECT
public:
    MainWindow();
    void SetActiveWaves(ActiveWaves & list);
    void SetHighlightSamples(bool isTrue);
private:
    void UpdateActions();
    void UpdateView();

    QMainWindow mMainWindow;
    QScrollArea * mScrollAreaPtr;
    WaveView * mWaveViewPtr; 
    QAction * mXZoomInActionPtr;
    QAction * mXZoomOutActionPtr;
    QAction * mYZoomInActionPtr;
    QAction * mYZoomOutActionPtr;
    QAction * mZoomResetActionPtr;
    QAction * mHighlightSamplesActionPtr;
    QAction * mOpenActionPtr;
    QAction * mExitActionPtr;
    QMenu * mFileMenuPtr;
    QMenu * mViewMenuPtr;
private slots:
    void XZoomIn();
    void XZoomOut();
    void YZoomIn();
    void YZoomOut();
    void ResetZoom();
    void HighlightSamples();
    void OpenFile();
    void ExitApplication();
};

#endif
