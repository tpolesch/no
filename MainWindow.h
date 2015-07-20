#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QtWidgets/QMainWindow>

class QScrollArea;
class QAction;
class QMenu;
class MainView;
class MainData;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();
    void SetData(MainData & data);
    void SetHighlightSamples(bool isTrue);
    void OpenFile(const QString & file);
private:
    QString mInfoFile;
    QScrollArea * mScrollAreaPtr;
    MainView * mMainViewPtr; 
    QAction * mZoomInActionPtr;
    QAction * mZoomOutActionPtr;
    QAction * mXZoomInActionPtr;
    QAction * mXZoomOutActionPtr;
    QAction * mYZoomInActionPtr;
    QAction * mYZoomOutActionPtr;
    QAction * mUnzoomActionPtr;
    QAction * mHighlightSamplesActionPtr;
    QAction * mOpenActionPtr;
    QAction * mReloadActionPtr;
    QAction * mExitActionPtr;
    QMenu * mFileMenuPtr;
    QMenu * mViewMenuPtr;
private slots:
    void ZoomIn();
    void ZoomOut();
    void XZoomIn();
    void XZoomOut();
    void YZoomIn();
    void YZoomOut();
    void Unzoom();
    void HighlightSamples();
    void OpenFile();
    void ReloadFile();
    void ExitApplication();
};

#endif
