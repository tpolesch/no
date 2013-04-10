
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

    typedef QList<unsigned int> SampleType;
    const SampleType & Samples() const {return mSamples;}
    unsigned int SampleMask() const {return 0x3fffu;}
    int SampleOffset() const {return 0x2000;}
    int SamplesPerSecond() const {return 500;}
    int MilliMeterPerSecond() const {return 25;} // mm/s
    int MilliMeterPerMilliVolt() const {return 10;} // mm/mV
    int LsbPerMilliVolt() const {return 200;} // LSB/mV
    bool IsValid() const {return (error == 0);}
private:
    SampleType mSamples;
    int error;
};

////////////////////////////////////////////////////////////////////////////////

class ActiveWaves
{
public:
    ActiveWaves();
    void AddTestSignal();
    void Init(const QStringList & args);

    typedef QList<SingleWave> ListType;
    const ListType & List() const {return mList;}
private:
    ListType mList;
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
    void Init(const ActiveWaves & arg);
protected:
    void paintEvent(QPaintEvent *);
private:
    const ActiveWaves::ListType & Waves() const;
    void InitSize();
    void BuildZoom();
    PixelScaling ZoomScaling() const;
    PixelScaling StandardScaling() const;

    const ActiveWaves * mActiveWavesPtr;
    int mXZoomValue;
    int mYZoomValue;
};

////////////////////////////////////////////////////////////////////////////////
// class SingleWave
////////////////////////////////////////////////////////////////////////////////

SingleWave::SingleWave():
    mSamples(),
    error(0)
{
}

void SingleWave::BuildTestSignal()
{
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
        ++error;
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
    mList()
{
}

void ActiveWaves::AddTestSignal()
{
    SingleWave wave;
    wave.BuildTestSignal();
    mList.append(wave);
}

void ActiveWaves::Init(const QStringList & args)
{
    for (int index = 1; index < args.count(); ++index)
    {
        SingleWave wave;
        wave.BuildFromFile(args[index]);

        if (wave.IsValid())
        {
            mList.append(wave);
        }
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
    mChannelOffset(0)
{
}

int DrawChannel::MinimumWidth() const
{
    return XPixel(Wave().Samples().count());
}

void DrawChannel::Draw(QWidget & parent, int offset)
{
    QPainter samplePainter(&parent);
    samplePainter.setRenderHint(QPainter::Antialiasing, true);
    samplePainter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    mSamplePosition = 0;
    mChannelOffset = offset;
    QPoint from = CurrentPoint();

    while (IsReading())
    {
        const QPoint to = CurrentPoint();
        samplePainter.drawLine(from, to);
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
    mYZoomValue(0)
{
}

void WaveView::XZoomIn()
{
    ++mXZoomValue;
    BuildZoom();
}

void WaveView::XZoomOut()
{
    --mXZoomValue;
    BuildZoom();
}

void WaveView::YZoomIn()
{
    ++mYZoomValue;
    BuildZoom();
}

void WaveView::YZoomOut()
{
    --mYZoomValue;
    BuildZoom();
}

void WaveView::ResetZoom()
{
    mXZoomValue = 0;
    mYZoomValue = 0;
    BuildZoom();
}

void WaveView::Init(const ActiveWaves & arg)
{
    mActiveWavesPtr = &arg;
    InitSize();
}

const ActiveWaves::ListType & WaveView::Waves() const
{
    return mActiveWavesPtr->List();
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

void WaveView::BuildZoom()
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

    mViewMenuPtr = new QMenu(QString("&View"), &mMainWindow);
    mViewMenuPtr->addAction(mXZoomInActionPtr);
    mViewMenuPtr->addAction(mXZoomOutActionPtr);
    mViewMenuPtr->addAction(mYZoomInActionPtr);
    mViewMenuPtr->addAction(mYZoomOutActionPtr);
    mViewMenuPtr->addAction(mZoomResetActionPtr);
    mViewMenuPtr->addSeparator();

    mMainWindow.menuBar()->addMenu(mViewMenuPtr);
}

void MainWindow::Init(const ActiveWaves & list)
{
    mWaveViewPtr->Init(list);
    UpdateActions();
    mMainWindow.show();
}

void MainWindow::XZoomIn()
{
    qDebug("MainWindow::XZoomIn()");
    mWaveViewPtr->XZoomIn();
    UpdateActions();
}

void MainWindow::XZoomOut()
{
    qDebug("MainWindow::XZoomOut()");
    mWaveViewPtr->XZoomOut();
    UpdateActions();
}

void MainWindow::YZoomIn()
{
    qDebug("MainWindow::YZoomIn()");
    mWaveViewPtr->YZoomIn();
    UpdateActions();
}

void MainWindow::YZoomOut()
{
    qDebug("MainWindow::YZoomOut()");
    mWaveViewPtr->YZoomOut();
    UpdateActions();
}

void MainWindow::ResetZoom()
{
    qDebug("MainWindow::ResetZoom()");
    mWaveViewPtr->ResetZoom();
    UpdateActions();
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
// main()
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char * argv[])
{
    QApplication app(argc, argv);
    QStringList args = app.arguments();

    if (args.count() < 2)
    {
        qDebug("Usage: %s file1 [file2 ...]", qPrintable(args[0]));
        return 0;
    }

    ActiveWaves activeWaves;
    // activeWaves.AddTestSignal();
    activeWaves.Init(args);

    MainWindow window;
    window.Init(activeWaves);
    return app.exec();
}

