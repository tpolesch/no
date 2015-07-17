#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QtWidgets/QMainWindow>

class QScrollArea;
class QAction;
class QMenu;
class MainView;
class MainData;

class MainWindow : public QObject
{
    Q_OBJECT
public:
    MainWindow();
    void SetData(MainData & data);
    void SetHighlightSamples(bool isTrue);
    void OpenFile(const QString & file);
private:
    void UpdateActions();
    void UpdateView();

    QMainWindow mMainWindow;
    QScrollArea * mScrollAreaPtr;
    MainView * mMainViewPtr; 
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
