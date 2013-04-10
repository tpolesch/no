
#include "MainWindow.h"
#include <QtGui>

////////////////////////////////////////////////////////////////////////////////
// class declarations
////////////////////////////////////////////////////////////////////////////////

class SingleWave
{
public:
    SingleWave();
    void BuildTestSignal();
    void BuildFromFile(const QString & name);

    typedef QList<unsigned int> SampleList;
    const SampleList & Samples() const {return mSamples;}
    unsigned int SampleMask() const {return 0x3fffu;}
    int SampleOffset() const {return 0x2000;}
    int SamplesPerSecond() const {return 500;}
    int MilliMeterPerSecond() const {return 25;} // mm/s
    int MilliMeterPerMilliVolt() const {return 10;} // mm/mV
    int LsbPerMilliVolt() const {return 200;} // LSB/mV
    bool IsValid() const {return (mErrorCount == 0);}
private:
    SampleList mSamples;
    int mErrorCount;
};

////////////////////////////////////////////////////////////////////////////////

class ActiveWaves
{
public:
    ActiveWaves();
    void AddFileList(const QStringList & args);
    void AddFile(const QString & fileName);
    void AddTestSignal();

    typedef QList<SingleWave> WaveList;
    const WaveList & Waves() const {return mWaves;}
private:
    WaveList mWaves;
};

////////////////////////////////////////////////////////////////////////////////

class PixelScaling
{
public:
    explicit PixelScaling(int xzoom, int yzoom);
    int MilliMeterAsXPixel(int numerator, int denominator) const;
    int MilliMeterAsYPixel(int numerator, int denominator) const;
private:
    const int mXZoom;
    const int mYZoom;
    int mXmm;
    int mXpx;
    int mYmm;
    int mYpx;
};

////////////////////////////////////////////////////////////////////////////////

class DrawGrid
{
public:
    explicit DrawGrid(const PixelScaling & scaling);
    void Draw(QWidget & parent);
private:
    void DrawTimeGrid(QWidget & parent);
    void DrawValueGrid(QWidget & parent);

    const PixelScaling * mScalingPtr;
};

////////////////////////////////////////////////////////////////////////////////

class DrawChannel
{
public:
    explicit DrawChannel(const SingleWave & wave, const PixelScaling & scaling);
    void SetMarkSamples(bool isMark) {mIsMarkSamples = isMark;}
    void Draw(QWidget & parent, int offset);
    int MinimumWidth() const;
private:
    const SingleWave & Wave() const {return *mWavePtr;}
    int YPixel(int pos) const;
    int XPixel(int pos) const;
    bool IsReading() const;
    QPoint CurrentPoint();

    const PixelScaling * mScalingPtr;
    const SingleWave * mWavePtr;
    int mSamplePosition;
    int mChannelOffset;
    bool mIsMarkSamples;
};

////////////////////////////////////////////////////////////////////////////////

class WaveView : public QWidget
{
public:
    WaveView();
    void XZoomIn();
    void XZoomOut();
    void YZoomIn();
    void YZoomOut();
    void ResetZoom();
    void SetMarkSamples(bool isMarked);
    void SetActiveWaves(ActiveWaves & arg);
    void OpenFile(const QString & fileName);
protected:
    void paintEvent(QPaintEvent *);
private:
    const ActiveWaves::WaveList & Waves() const;
    void InitSize();
    void RebuildView();
    PixelScaling ZoomScaling() const;
    PixelScaling StandardScaling() const;

    ActiveWaves * mActiveWavesPtr;
    int mXZoomValue;
    int mYZoomValue;
    bool mIsMarkSamples;
};

class ArgumentParser
{
public:
    ArgumentParser();
    void ParseList(QStringList list);
    void PrintUsage();
    bool IsInvalid() const {return mIsInvalid;}
    bool IsTestSignal() const {return mIsTestSignal;}
    bool IsMarkSamples() const {return mIsMarkSamples;}
    bool IsShowHelp() const {return mIsShowHelp;}
    const QStringList & Files() const {return mFiles;}
private:
    void ParseLine(const QString & line);
    bool mIsInvalid;
    bool mIsTestSignal;
    bool mIsMarkSamples;
    bool mIsShowHelp;
    QString mApplication;
    QStringList mFiles;
};
    
////////////////////////////////////////////////////////////////////////////////
// class SingleWave
////////////////////////////////////////////////////////////////////////////////

SingleWave::SingleWave():
    mSamples(),
    mErrorCount(0)
{
}

void SingleWave::BuildTestSignal()
{
    qDebug("SingleWave::BuildTestSignal()");
    const int sampleCount = 10 * SamplesPerSecond();
    int value = LsbPerMilliVolt();

    for (int index = 0; index < sampleCount; ++index)
    {
        if ((index % SamplesPerSecond()) == 0)
        {
            value -= LsbPerMilliVolt();
        }

        mSamples.append(static_cast<unsigned int>(SampleOffset() + value));
    }
}

void SingleWave::BuildFromFile(const QString & name)
{
    qDebug("SingleWave::BuildFromFile(%s)", qPrintable(name));
    QFile file(name);

    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug("... failed");
        ++mErrorCount;
        return;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);
    int count = 0;

    while (!stream.atEnd())
    {
        ++count;
        qint16 sample;
        stream >> sample;
        mSamples.append(static_cast<unsigned int>(sample));
    }

    qDebug("... done with %d samples", count);
}

////////////////////////////////////////////////////////////////////////////////
// class ActiveWaves
////////////////////////////////////////////////////////////////////////////////

ActiveWaves::ActiveWaves():
    mWaves()
{
}

void ActiveWaves::AddTestSignal()
{
    SingleWave wave;
    wave.BuildTestSignal();
    mWaves.append(wave);
}

void ActiveWaves::AddFile(const QString & fileName)
{
    SingleWave wave;
    wave.BuildFromFile(fileName);

    if (wave.IsValid())
    {
        mWaves.append(wave);
    }
}

void ActiveWaves::AddFileList(const QStringList & list)
{
    for (int index = 0; index < list.count(); ++index)
    {
        AddFile(list[index]);
    }
}

////////////////////////////////////////////////////////////////////////////////
// class PixelScaling
////////////////////////////////////////////////////////////////////////////////

PixelScaling::PixelScaling(int xzoom, int yzoom):
    mXZoom(xzoom),
    mYZoom(yzoom)
{
    const QDesktopWidget desk;
    mXmm = desk.widthMM();
    mYmm = desk.heightMM();
    mXpx = desk.width();
    mYpx = desk.height();
}

int PixelScaling::MilliMeterAsXPixel(int numerator, int denominator) const
{
    qint64 result = numerator;
    result *= mXpx;

    if (mXZoom > 0) {result <<= mXZoom;}
    if (mXZoom < 0) {result >>= -mXZoom;}

    result /= mXmm;
    result /= denominator;
    return static_cast<int>(result);
}

int PixelScaling::MilliMeterAsYPixel(int numerator, int denominator) const
{
    qint64 result = numerator;
    result *= mYpx;

    if (mYZoom > 0) {result <<= mYZoom;}
    if (mYZoom < 0) {result >>= -mYZoom;}

    result /= mYmm;
    result /= denominator;
    return static_cast<int>(result);
}

////////////////////////////////////////////////////////////////////////////////
// class DrawGrid
////////////////////////////////////////////////////////////////////////////////

DrawGrid::DrawGrid(const PixelScaling & scaling):
    mScalingPtr(&scaling)
{
}

void DrawGrid::Draw(QWidget & parent)
{
    DrawTimeGrid(parent);
    DrawValueGrid(parent);
}

void DrawGrid::DrawTimeGrid(QWidget & parent)
{
    const int max = parent.widthMM();
    const int height = parent.height();
    QPen pen(Qt::red, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int mm = 0; mm < max; mm += 5)
    {
        const int xpx = mScalingPtr->MilliMeterAsXPixel(mm, 1);
        pen.setStyle(((mm % 25) == 0) ? Qt::SolidLine : Qt::DotLine);
        painter.setPen(pen);
        painter.drawLine(xpx, 0, xpx, height);
    }
}

void DrawGrid::DrawValueGrid(QWidget & parent)
{
    const int max = parent.heightMM();
    const int width = parent.width();
    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::red, 1, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin));

    for (int mm = 0; mm < max; mm += 5)
    {
        const int ypx = mScalingPtr->MilliMeterAsYPixel(mm, 1);
        painter.drawLine(0, ypx, width, ypx);
    }
}

////////////////////////////////////////////////////////////////////////////////
// class DrawChannel
////////////////////////////////////////////////////////////////////////////////

DrawChannel::DrawChannel(const SingleWave & wave, const PixelScaling & scaling):
    mScalingPtr(&scaling),
    mWavePtr(&wave),
    mSamplePosition(0),
    mChannelOffset(0),
    mIsMarkSamples(false)
{
}

int DrawChannel::MinimumWidth() const
{
    return XPixel(Wave().Samples().count());
}

void DrawChannel::Draw(QWidget & parent, int offset)
{
    QPen linePen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen pointPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    if (mIsMarkSamples)
    {
        pointPen.setWidth(3);
        linePen.setColor(Qt::gray);
    }

    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);

    mSamplePosition = 0;
    mChannelOffset = offset;
    QPoint from = CurrentPoint();

    while (IsReading())
    {
        const QPoint to = CurrentPoint();
        painter.setPen(linePen);
        painter.drawLine(from, to);
        painter.setPen(pointPen);
        painter.drawPoint(from);
        from = to;
    }
}

bool DrawChannel::IsReading() const
{
    return (mSamplePosition < Wave().Samples().count());
}

int DrawChannel::YPixel(int pos) const
{
    const unsigned int rawValue = ((Wave().Samples()[pos]) & Wave().SampleMask());
    const int value = rawValue - Wave().SampleOffset();
    // Example: (100LSB * 10mm/mV) / 200LSB/mV = 5mm
    return mScalingPtr->MilliMeterAsYPixel((value * Wave().MilliMeterPerMilliVolt()), Wave().LsbPerMilliVolt());
}

int DrawChannel::XPixel(int pos) const
{
    // Example: (200 * 25mm/sec) / 500samples/sec = 10mm
    return mScalingPtr->MilliMeterAsXPixel((pos * Wave().MilliMeterPerSecond()), Wave().SamplesPerSecond());
}

QPoint DrawChannel::CurrentPoint()
{
    const int xpx = XPixel(mSamplePosition);
    const int ypx = YPixel(mSamplePosition);
    ++mSamplePosition;
    return QPoint(xpx, (mChannelOffset - ypx));
}

////////////////////////////////////////////////////////////////////////////////
// class WaveView
////////////////////////////////////////////////////////////////////////////////

WaveView::WaveView():
    QWidget(),
    mActiveWavesPtr(NULL),
    mXZoomValue(0),
    mYZoomValue(0),
    mIsMarkSamples(false)
{
}

void WaveView::XZoomIn()
{
    ++mXZoomValue;
    RebuildView();
}

void WaveView::XZoomOut()
{
    --mXZoomValue;
    RebuildView();
}

void WaveView::YZoomIn()
{
    ++mYZoomValue;
    RebuildView();
}

void WaveView::YZoomOut()
{
    --mYZoomValue;
    RebuildView();
}

void WaveView::ResetZoom()
{
    mXZoomValue = 0;
    mYZoomValue = 0;
    RebuildView();
}

void WaveView::SetMarkSamples(bool isMarked)
{
    mIsMarkSamples = isMarked;
    update();
}

void WaveView::OpenFile(const QString & fileName)
{
    mActiveWavesPtr->AddFile(fileName);
    RebuildView();
}

void WaveView::SetActiveWaves(ActiveWaves & arg)
{
    mActiveWavesPtr = &arg;
    InitSize();
}

const ActiveWaves::WaveList & WaveView::Waves() const
{
    return mActiveWavesPtr->Waves();
}

void WaveView::InitSize()
{
    int minimumWidth = 0;

    for (int index = 0; index < Waves().count(); ++index)
    {
        DrawChannel channel(Waves()[index], ZoomScaling());
        const int channelWidth = channel.MinimumWidth();

        if (minimumWidth < channelWidth)
        {
            minimumWidth = channelWidth;
        }

    }

    const int width = minimumWidth + 10;
    setMinimumWidth(width);
    resize(width, height());

    qDebug("WaveView::InitSize() width = %d", width);
}

void WaveView::RebuildView()
{
    InitSize();
    update();
}

void WaveView::paintEvent(QPaintEvent *)
{
    DrawGrid grid(StandardScaling());
    grid.Draw(*this);

    const int count = Waves().count();
    const int channelHeight = height() / count;

    for (int index = 0; index < count; ++index)
    {
        DrawChannel channel(Waves()[index], ZoomScaling());
        const int channelOffset = (index * channelHeight) + (channelHeight / 2);
        channel.SetMarkSamples(mIsMarkSamples);
        channel.Draw(*this, channelOffset);
    }
}
    
PixelScaling WaveView::ZoomScaling() const
{
    return PixelScaling(mXZoomValue, mYZoomValue);
}

PixelScaling WaveView::StandardScaling() const
{
    return PixelScaling(0, 0);
}

////////////////////////////////////////////////////////////////////////////////
// class MainWindow
////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
{
    mWaveViewPtr = new WaveView();
    mWaveViewPtr->setBackgroundRole(QPalette::Base);
    mWaveViewPtr->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    mScrollAreaPtr = new QScrollArea();
    mScrollAreaPtr->setBackgroundRole(QPalette::Dark);
    mScrollAreaPtr->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mScrollAreaPtr->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mScrollAreaPtr->setWidgetResizable(true);
    mScrollAreaPtr->setWidget(mWaveViewPtr);

    mMainWindow.setCentralWidget(mScrollAreaPtr);
    mMainWindow.setWindowTitle(QString("no"));
    mMainWindow.resize(600, 300);

    mXZoomInActionPtr = new QAction(QString("X-Zoom In"), this);
    mXZoomInActionPtr->setShortcut(QKeySequence(Qt::Key_X));
    mXZoomInActionPtr->setEnabled(false);
    connect(mXZoomInActionPtr, SIGNAL(triggered()), this, SLOT(XZoomIn()));

    mXZoomOutActionPtr = new QAction(QString("X-Zoom Out"), this);
    mXZoomOutActionPtr->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_X));
    mXZoomOutActionPtr->setEnabled(false);
    connect(mXZoomOutActionPtr, SIGNAL(triggered()), this, SLOT(XZoomOut()));

    mYZoomInActionPtr = new QAction(QString("Y-Zoom In"), this);
    mYZoomInActionPtr->setShortcut(QKeySequence(Qt::Key_Y));
    mYZoomInActionPtr->setEnabled(false);
    connect(mYZoomInActionPtr, SIGNAL(triggered()), this, SLOT(YZoomIn()));

    mYZoomOutActionPtr = new QAction(QString("Y-Zoom Out"), this);
    mYZoomOutActionPtr->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Y));
    mYZoomOutActionPtr->setEnabled(false);
    connect(mYZoomOutActionPtr, SIGNAL(triggered()), this, SLOT(YZoomOut()));

    mZoomResetActionPtr = new QAction(QString("Reset Zoom"), this);
    mZoomResetActionPtr->setShortcut(QKeySequence(Qt::Key_R));
    mZoomResetActionPtr->setEnabled(false);
    connect(mZoomResetActionPtr, SIGNAL(triggered()), this, SLOT(ResetZoom()));

    mMarkSamplesActionPtr = new QAction(QString("Mark &Samples"), this);
    mMarkSamplesActionPtr->setShortcut(QKeySequence(Qt::Key_S));
    mMarkSamplesActionPtr->setEnabled(true);
    mMarkSamplesActionPtr->setChecked(false);
    mMarkSamplesActionPtr->setCheckable(true);
    connect(mMarkSamplesActionPtr, SIGNAL(triggered()), this, SLOT(MarkSamples()));

    mOpenActionPtr = new QAction(tr("&Open..."), this);
    mOpenActionPtr->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_O));
    mOpenActionPtr->setEnabled(true);
    connect(mOpenActionPtr, SIGNAL(triggered()), this, SLOT(OpenFile()));

    mExitActionPtr = new QAction(tr("E&xit"), this);
    mExitActionPtr->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    mExitActionPtr->setEnabled(true);
    connect(mExitActionPtr, SIGNAL(triggered()), this, SLOT(ExitApplication()));

    mFileMenuPtr = new QMenu(QString("&File"), &mMainWindow);
    mFileMenuPtr->addAction(mOpenActionPtr);
    mFileMenuPtr->addAction(mExitActionPtr);

    mViewMenuPtr = new QMenu(QString("&View"), &mMainWindow);
    mViewMenuPtr->addAction(mXZoomInActionPtr);
    mViewMenuPtr->addAction(mXZoomOutActionPtr);
    mViewMenuPtr->addAction(mYZoomInActionPtr);
    mViewMenuPtr->addAction(mYZoomOutActionPtr);
    mViewMenuPtr->addAction(mZoomResetActionPtr);
    mViewMenuPtr->addSeparator();
    mViewMenuPtr->addAction(mMarkSamplesActionPtr);

    mMainWindow.menuBar()->addMenu(mFileMenuPtr);
    mMainWindow.menuBar()->addMenu(mViewMenuPtr);
}

void MainWindow::SetActiveWaves(ActiveWaves & list)
{
    mWaveViewPtr->SetActiveWaves(list);
    UpdateActions();
    mMainWindow.show();
}

void MainWindow::XZoomIn()
{
    mWaveViewPtr->XZoomIn();
    UpdateActions();
}

void MainWindow::XZoomOut()
{
    mWaveViewPtr->XZoomOut();
    UpdateActions();
}

void MainWindow::YZoomIn()
{
    mWaveViewPtr->YZoomIn();
    UpdateActions();
}

void MainWindow::YZoomOut()
{
    mWaveViewPtr->YZoomOut();
    UpdateActions();
}

void MainWindow::SetMarkSamples(bool isMark)
{
    mWaveViewPtr->SetMarkSamples(isMark);
}

void MainWindow::MarkSamples()
{
    SetMarkSamples(mMarkSamplesActionPtr->isChecked());
    UpdateActions();
}

void MainWindow::ResetZoom()
{
    mWaveViewPtr->ResetZoom();
    UpdateActions();
}

void MainWindow::ExitApplication()
{
    mMainWindow.close();
}

void MainWindow::OpenFile()
{
    QString fileName = QFileDialog::getOpenFileName(&mMainWindow, QString("Open File"), QDir::currentPath());

    if (!fileName.isEmpty())
    {
        mWaveViewPtr->OpenFile(fileName);
    }
}

void MainWindow::UpdateView()
{
}

void MainWindow::UpdateActions()
{
    mXZoomInActionPtr->setEnabled(true);
    mXZoomOutActionPtr->setEnabled(true);
    mYZoomInActionPtr->setEnabled(true);
    mYZoomOutActionPtr->setEnabled(true);
    mZoomResetActionPtr->setEnabled(true);
}

////////////////////////////////////////////////////////////////////////////////
// class ArgumentParser
////////////////////////////////////////////////////////////////////////////////

ArgumentParser::ArgumentParser():
    mIsInvalid(false),
    mIsTestSignal(false),
    mIsMarkSamples(false),
    mIsShowHelp(false),
    mFiles()
{
}

void ArgumentParser::ParseList(QStringList list)
{
    if (list.count() > 0)
    {
        mApplication = list[0];
    }

    for (int index = 1; index < list.count(); ++index)
    {
        ParseLine(list[index]);
    }
}

void ArgumentParser::ParseLine(const QString & line)
{
    if ((line == QString("-t")) || (line == QString("--test")))
    {
        mIsTestSignal = true;
        return;
    }

    if ((line == QString("-m")) || (line == QString("--mark")))
    {
        mIsMarkSamples = true;
        return;
    }

    if ((line == QString("-h")) || (line == QString("--help")))
    {
        mIsShowHelp = true;
        return;
    }

    const QRegExp longOption("--(\\w+)");

    if (longOption.exactMatch(line))
    {
        qDebug() << "Unknown option: " << longOption.cap(0);
        mIsInvalid = true;
        return;
    }

    const QRegExp shortOption("-(\\w)");

    if (shortOption.exactMatch(line))
    {
        qDebug() << "Unknown option: " << shortOption.cap(0);
        mIsInvalid = true;
        return;
    }

    // assume filename argument
    mFiles.append(line);
}

void ArgumentParser::PrintUsage()
{
    qDebug("Usage:");
    qDebug("  %s [options] [file1 file2 ...]", qPrintable(mApplication));
    qDebug("Options:");
    qDebug("  -t --test ... add test signal");
    qDebug("  -m --mark ... mark samples");
    qDebug("  -h --help ... show help");
}

////////////////////////////////////////////////////////////////////////////////
// main()
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char * argv[])
{
    QApplication app(argc, argv);
    ArgumentParser arguments;
    arguments.ParseList(app.arguments());

    if (arguments.IsShowHelp() || arguments.IsInvalid())
    {
        arguments.PrintUsage();
        return 0;
    }

    ActiveWaves activeWaves;

    if (arguments.IsTestSignal())
    {
        activeWaves.AddTestSignal();
    }

    activeWaves.AddFileList(arguments.Files());

    MainWindow window;
    window.SetActiveWaves(activeWaves);
    window.SetMarkSamples(arguments.IsMarkSamples());
    return app.exec();
}

