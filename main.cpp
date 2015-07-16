
#include "MainWindow.h"
#include <QtWidgets>
#include <QDebug>
#include <util/LightTestImplementation.h>

////////////////////////////////////////////////////////////////////////////////
// class InfoLine
////////////////////////////////////////////////////////////////////////////////

struct InfoLine
{
    QString file;
    QString label;
    int sps;
    int mask;
    int offset;
    int delay; // ms
    double gain;

    InfoLine():
        file(""),
        label(""),
        sps(500),
        mask(0xffff),
        offset(0),
        delay(0),
        gain(1)
    {
    }
};

////////////////////////////////////////////////////////////////////////////////
// class declarations
////////////////////////////////////////////////////////////////////////////////

class EcgSample
{
public:
    explicit EcgSample(unsigned int raw, const InfoLine & info)
    {
        const unsigned int lsb = raw & info.mask;
        const int value = static_cast<int>(lsb) - info.offset;
        milliVolt = info.gain * value;
    }

    double MilliVolt() const {return milliVolt;}
private:
    double milliVolt;
};

class AnyWave
{
public:
    explicit AnyWave();
    void SetInfo(const InfoLine & info);
    EcgSample Sample(int index) const {return EcgSample(mSamples[index], mInfo);}
    int SampleCount() const {return mSamples.count();}
    bool IsValid() const {return (mErrorCount == 0);}
    const InfoLine & Info() const {return mInfo;}
    double MilliMeterPerSecond() const {return 25.0;}
    double MilliMeterPerMilliVolt() const {return 10.0;}
private:
    InfoLine mInfo;
    QList<unsigned int> mSamples;
    int mErrorCount;
};

////////////////////////////////////////////////////////////////////////////////

class ActiveWaves
{
public:
    ActiveWaves();
    void AddFile(const InfoLine & info);

    int WaveCount() const {return mWaves.count();}
    const AnyWave & Wave(int index) const {return mWaves[index];}
private:
    QList<AnyWave> mWaves;
};

////////////////////////////////////////////////////////////////////////////////

class PixelScaling
{
public:
    explicit PixelScaling(int xzoom, int yzoom);
    int MilliMeterAsXPixel(double arg, double factor = 1.0) const;
    int MilliMeterAsYPixel(double arg, double factor = 1.0) const;
    double XPixelAsMilliMeter(int xpx) const;
    double YPixelAsMilliMeter(int ypx) const;
private:
    const int mXZoom;
    const int mYZoom;
    double mXmm;
    double mXpx;
    double mYmm;
    double mYpx;
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
    explicit DrawChannel(const AnyWave & wave, const PixelScaling & scaling);
    void SetHighlightSamples(bool isTrue) {mIsHighlightSamples = isTrue;}
    void Draw(QWidget & parent, int offset);
    int MinimumWidth() const;
private:
    void StartSampleIndex() {mSampleIndex = 0;}
    void MoveSampleIndex() {++mSampleIndex;}
    void DrawSamples(QPainter & painter);
    void DrawSpecialSamples(QPainter & painter);
    const AnyWave & Wave() const {return *mWavePtr;}
    int YPixel() const;
    int XPixel() const {return XPixel(mSampleIndex);}
    int XPixel(int sampleIndex) const;
    bool NotFinished() const;
    QPoint Point() const;
    EcgSample Sample() const;

    const PixelScaling * mScalingPtr;
    const AnyWave * mWavePtr;
    int mChannelOffset;
    int mSampleIndex;
    bool mIsHighlightSamples;
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
    void SetHighlightSamples(bool isTrue);
    void SetActiveWaves(ActiveWaves & arg);
    void OpenFile(const QString & fileName);
protected:
    void paintEvent(QPaintEvent *);
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
private:
    int WaveCount() const {return mActiveWavesPtr->WaveCount();}
    const AnyWave & Wave(int index) const {return mActiveWavesPtr->Wave(index);}
    void InitSize();
    void RebuildView();
    PixelScaling ZoomScaling() const;
    PixelScaling StandardScaling() const;

    ActiveWaves * mActiveWavesPtr;
    QRubberBand * mRubberBandPtr;
    QPoint mOrigin;
    int mXZoomValue;
    int mYZoomValue;
    bool mIsHighlightSamples;
};

class ArgumentParser
{
public:
    ArgumentParser();
    void ParseList(QStringList list);
    void PrintUsage();
    bool IsInvalid() const {return mIsInvalid;}
    bool IsTestSignal() const {return mIsTestSignal;}
    bool IsHighlightSamples() const {return mIsHighlightSamples;}
    bool IsShowHelp() const {return mIsShowHelp;}
    const QStringList & Files() const {return mFiles;}
private:
    void ParseLine(const QString & line);
    bool mIsInvalid;
    bool mIsTestSignal;
    bool mIsHighlightSamples;
    bool mIsShowHelp;
    QString mApplication;
    QStringList mFiles;
};
    
////////////////////////////////////////////////////////////////////////////////
// class AnyWave
////////////////////////////////////////////////////////////////////////////////

AnyWave::AnyWave():
    mInfo(),
    mSamples(),
    mErrorCount(0)
{
}

void AnyWave::SetInfo(const InfoLine & info)
{
    mInfo = info;
    const QString name = info.file;
    qDebug("AnyWave::BuildFromFile(%s)", qPrintable(name));
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
        mSamples.append(sample);
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

void ActiveWaves::AddFile(const InfoLine & info)
{
    AnyWave wave;
    wave.SetInfo(info);

    if (wave.IsValid())
    {
        mWaves.append(wave);
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
    mXmm = static_cast<double>(desk.widthMM());
    mYmm = static_cast<double>(desk.heightMM());
    mXpx = static_cast<double>(desk.width());
    mYpx = static_cast<double>(desk.height());
}

inline static double AddZoomHelper(double value, int zoom)
{
    if (zoom < 0)
    {
        return value / static_cast<double>(1 << (-zoom));
    }

    return value * static_cast<double>(1 << zoom);
}

inline static int AddZoom(double value, int zoom)
{
    return static_cast<int>(AddZoomHelper(value, zoom));
}

inline static double RemoveZoom(double value, int zoom)
{
    return AddZoomHelper(value, -zoom);
}

int PixelScaling::MilliMeterAsXPixel(double arg, double factor) const
{
    const double xpx = arg * factor * mXpx / mXmm;
    return AddZoom(xpx, mXZoom);
}

int PixelScaling::MilliMeterAsYPixel(double arg, double factor) const
{
    const double ypx = arg * factor * mYpx / mYmm;
    return AddZoom(ypx, mYZoom);
}

double PixelScaling::XPixelAsMilliMeter(int xpx) const
{
    const double xmm = (static_cast<double>(xpx) * mXmm) / mXpx;
    return RemoveZoom(xmm, mXZoom);
}

double PixelScaling::YPixelAsMilliMeter(int ypx) const
{
    const double ymm = (static_cast<double>(ypx) * mYmm) / mYpx;
    return RemoveZoom(ymm, mYZoom);
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
        const int xpx = mScalingPtr->MilliMeterAsXPixel(mm);
        pen.setStyle(((mm % 25) == 0) ? Qt::SolidLine : Qt::DotLine);
        painter.setPen(pen);
        painter.drawLine(xpx, 0, xpx, height);
    }

    if (max < 1)
    {
        qDebug() << "DrawGrid::DrawTimeGrid() width = " << max << "mm";
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
        const int ypx = mScalingPtr->MilliMeterAsYPixel(mm);
        painter.drawLine(0, ypx, width, ypx);
    }
    
    if (max < 1)
    {
        qDebug() << "DrawGrid::DrawValueGrid() height = " << max << "mm";
    }
}

////////////////////////////////////////////////////////////////////////////////
// class DrawChannel
////////////////////////////////////////////////////////////////////////////////

DrawChannel::DrawChannel(const AnyWave & wave, const PixelScaling & scaling):
    mScalingPtr(&scaling),
    mWavePtr(&wave),
    mChannelOffset(0),
    mSampleIndex(0),
    mIsHighlightSamples(false)
{
}

int DrawChannel::MinimumWidth() const
{
    return XPixel(Wave().SampleCount());
}

void DrawChannel::Draw(QWidget & parent, int offset)
{
    mChannelOffset = offset;
    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);
    DrawSamples(painter);
}

void DrawChannel::DrawSamples(QPainter & painter)
{
    QPen defaultPen(Qt::gray, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen highlightPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    if (!mIsHighlightSamples)
    {
        defaultPen.setColor(Qt::black);
    }

    StartSampleIndex();
    QPoint fromPoint = Point();
    DrawSpecialSamples(painter);
    MoveSampleIndex();

    while (NotFinished())
    {
        const QPoint toPoint = Point();
        DrawSpecialSamples(painter);
        painter.drawLine(fromPoint, toPoint);

        if (mIsHighlightSamples)
        {
            painter.setPen(highlightPen);
            painter.drawPoint(fromPoint);
            painter.setPen(defaultPen);
        }

        fromPoint = toPoint;
        MoveSampleIndex();
    }
}

void DrawChannel::DrawSpecialSamples(QPainter & )
{
}

bool DrawChannel::NotFinished() const
{
    return (mSampleIndex < Wave().SampleCount());
}

int DrawChannel::YPixel() const
{
    return mChannelOffset -
        mScalingPtr->MilliMeterAsYPixel(Sample().MilliVolt(), Wave().MilliMeterPerMilliVolt());
}

int DrawChannel::XPixel(int sampleIndex) const
{
    double second = sampleIndex;
    second /= Wave().Info().sps;
    return mScalingPtr->MilliMeterAsXPixel(second, Wave().MilliMeterPerSecond());
}

QPoint DrawChannel::Point() const
{
    return QPoint(XPixel(), YPixel());
}
    
EcgSample DrawChannel::Sample() const
{
    return EcgSample(Wave().Sample(mSampleIndex));
}

////////////////////////////////////////////////////////////////////////////////
// class TextFileReader
////////////////////////////////////////////////////////////////////////////////

class TextFileReader
{
    QStringList mLines;
public:
    explicit TextFileReader(const QString & fileName)
    {
        QFile file(fileName);

        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::information(0, "error", file.errorString());
            return;
        }

        QTextStream in(&file);
        while (!in.atEnd()) {mLines.append(in.readLine());}    
        file.close();
    }
    
    const QStringList & Lines() const {return mLines;}
};

////////////////////////////////////////////////////////////////////////////////
// class InfoLineParser
////////////////////////////////////////////////////////////////////////////////

class InfoLineParser
{
    QList<InfoLine> mList;
public:
    explicit InfoLineParser(const QString & line)
    {
        Parse(line);
    }

    explicit InfoLineParser(const QStringList & lines)
    {
        for (auto & line:lines)
        {
            Parse(line);
        }
    }
    
    const QList<InfoLine> & List() const {return mList;}
private:
    void Parse(const QString & line)
    {
        const QStringList list = line.split(QRegExp("\\s+|="));
        bool done = true;
        if (list.count() < 5) {return;}

        // mandatory
        InfoLine info;
        info.file = list[0];
        info.label = list[4];
        info.sps = list[1].toInt(&done, 0);

        // optional
        QString value;
        if (done && FindKey(list, "gain",   value)) {info.gain   = value.toDouble(&done);}
        if (done && FindKey(list, "s-mask", value)) {info.mask   = value.toInt(&done, 0);}
        if (done && FindKey(list, "offset", value)) {info.offset = value.toInt(&done, 0);}
        if (done && FindKey(list, "delay",  value)) {info.delay  = value.toInt(&done, 0);}
        if (done) {mList.append(info);}
    }

    bool FindKey(const QStringList & list, const QString & key, QString & dst) const
    {
        for (int index = 5; index < list.count() - 1; ++index)
        {
            if (key == list[index])
            {
                dst = list[index + 1];
                return true;
            }
        }

        dst = "unknown";
        return false;
    }
};

TEST(InfoLineParser, Examples)
{
    InfoLineParser obj("ecg.dat 500 1 mV \"E_01_16\" gain=0.005 s-mask=0x3fff offset=0x1fff delay=200");
    const InfoLine & got = obj.List()[0];
    EXPECT_EQ("ecg.dat", got.file);
    EXPECT_EQ(500, got.sps);
    EXPECT_EQ(200, static_cast<int>(1.0 / got.gain));
    EXPECT_EQ(0x3fffu, got.mask);
    EXPECT_EQ(0x1fffu, got.offset);
    EXPECT_EQ(200, got.delay);
}

////////////////////////////////////////////////////////////////////////////////
// class WaveView
////////////////////////////////////////////////////////////////////////////////

WaveView::WaveView():
    QWidget(),
    mActiveWavesPtr(NULL),
    mRubberBandPtr(NULL),
    mXZoomValue(0),
    mYZoomValue(0),
    mIsHighlightSamples(false)
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

void WaveView::SetHighlightSamples(bool isTrue)
{
    mIsHighlightSamples = isTrue;
    update();
}

void WaveView::OpenFile(const QString & fileName)
{
    if (QFileInfo(fileName).suffix() == "info")
    {
        TextFileReader read(fileName);
        InfoLineParser parse(read.Lines());

        for (auto & info:parse.List())
        {
            mActiveWavesPtr->AddFile(info);
        }
    }
    else
    {
        InfoLine info;
        info.file = fileName;
        mActiveWavesPtr->AddFile(info);
    }

    RebuildView();
}

void WaveView::SetActiveWaves(ActiveWaves & arg)
{
    mActiveWavesPtr = &arg;
    InitSize();
}

void WaveView::InitSize()
{
    int minimumWidth = 0;

    for (int index = 0; index < WaveCount(); ++index)
    {
        DrawChannel channel(Wave(index), ZoomScaling());
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

void WaveView::mousePressEvent(QMouseEvent * eventPtr)
{
    if (!mRubberBandPtr)
    {
        mRubberBandPtr = new QRubberBand(QRubberBand::Rectangle, this);
    }

    mOrigin = eventPtr->pos();
    mRubberBandPtr->setGeometry(QRect(mOrigin, QSize()));
    mRubberBandPtr->show();
}

void WaveView::mouseMoveEvent(QMouseEvent * eventPtr)
{
    mRubberBandPtr->setGeometry(QRect(mOrigin, eventPtr->pos()).normalized());
}

void WaveView::mouseReleaseEvent(QMouseEvent * eventPtr)
{
    mRubberBandPtr->setGeometry(QRect(mOrigin, eventPtr->pos()).normalized());
    const QRect rect = mRubberBandPtr->geometry();
    mRubberBandPtr->hide();
    PixelScaling scaling = ZoomScaling();
    qDebug()
        << "Selection: " << rect << "\n"
        << "- width " << scaling.XPixelAsMilliMeter(rect.width()) << "mm\n"
        << "- height " << scaling.YPixelAsMilliMeter(rect.height()) << "mm";

    const double xmm = scaling.XPixelAsMilliMeter(rect.x());

    for (int channelIndex = 0; channelIndex < WaveCount(); ++channelIndex)
    {
        const AnyWave & wv = Wave(channelIndex);
        const double samplePos =  xmm * static_cast<double>(wv.Info().sps) /
            static_cast<double>(wv.MilliMeterPerSecond());
        const int sampleIndex = static_cast<int>(samplePos);
        qDebug("channel %d: mv[%d] = %f",
                channelIndex, sampleIndex, wv.Sample(sampleIndex).MilliVolt());
    }
}

void WaveView::paintEvent(QPaintEvent *)
{
    DrawGrid grid(StandardScaling());
    grid.Draw(*this);

    const int count = WaveCount();
    const int widgetHeight = height();

    for (int index = 0; index < count; ++index)
    {
        const int channelHeight = widgetHeight / count;
        const int channelOffset = (index * channelHeight) + (channelHeight / 2);
        DrawChannel channel(Wave(index), ZoomScaling());
        channel.SetHighlightSamples(mIsHighlightSamples);
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

    mHighlightSamplesActionPtr = new QAction(QString("Highlight &Samples"), this);
    mHighlightSamplesActionPtr->setShortcut(QKeySequence(Qt::Key_H));
    mHighlightSamplesActionPtr->setEnabled(true);
    mHighlightSamplesActionPtr->setChecked(false);
    mHighlightSamplesActionPtr->setCheckable(true);
    connect(mHighlightSamplesActionPtr, SIGNAL(triggered()), this, SLOT(HighlightSamples()));

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
    mViewMenuPtr->addAction(mHighlightSamplesActionPtr);

    mMainWindow.menuBar()->addMenu(mFileMenuPtr);
    mMainWindow.menuBar()->addMenu(mViewMenuPtr);
}

void MainWindow::Open(const QString & file)
{
    mWaveViewPtr->OpenFile(file);
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

void MainWindow::SetHighlightSamples(bool isTrue)
{
    qDebug() << "SetHighlightSamples " << isTrue;
    mWaveViewPtr->SetHighlightSamples(isTrue);
}

void MainWindow::HighlightSamples()
{
    SetHighlightSamples(mHighlightSamplesActionPtr->isChecked());
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
    mIsHighlightSamples(false),
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

    if ((line == QString("-l")) || (line == QString("--highlight")))
    {
        mIsHighlightSamples = true;
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
    qDebug("  -l --highlight ... highlight samples");
    qDebug("  -h --help ... show help");
}

////////////////////////////////////////////////////////////////////////////////
// main()
////////////////////////////////////////////////////////////////////////////////

int main(int argc, char * argv[])
{
    const int testResult = LightTest::RunTests(argc, argv);
    if (testResult != 0) {return testResult;}

    QApplication app(argc, argv);
    ArgumentParser arguments;
    arguments.ParseList(app.arguments());

    if (arguments.IsShowHelp() || arguments.IsInvalid())
    {
        arguments.PrintUsage();
        return 0;
    }

    ActiveWaves activeWaves;
    MainWindow window;
    window.SetActiveWaves(activeWaves);
    window.SetHighlightSamples(arguments.IsHighlightSamples());

    if (arguments.Files().count() > 0)
    {
        window.Open(arguments.Files()[0]);
    }

    return app.exec();
}


