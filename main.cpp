
#include "MainWindow.h"
#include <QtWidgets>
#include <QDebug>
#include <util/LightTestImplementation.h>

////////////////////////////////////////////////////////////////////////////////

typedef int64_t IntType;
typedef IntType MicroSecond;
typedef IntType MilliSecond;
typedef double FloatType;
typedef FloatType MilliVolt;

inline static MicroSecond FromMilliSec(IntType ms)
{
    return ms * 1000ll;
}

////////////////////////////////////////////////////////////////////////////////
// InfoLine
////////////////////////////////////////////////////////////////////////////////

class InfoLine
{
private:
    unsigned int mErrors;
    unsigned int mSampleMask;
    int mSampleOffset;
    MicroSecond mSignalDelay;
    MicroSecond mSamplePeriod;
    double mDivisor;
    double mSampleGain;
    QString mLine;
    QString mOperator;
    QString mFileName;
    QString mUnit;
    QString mLabel;
    QList<double> mValues;
public:
    explicit InfoLine(const QString & txt):
        mErrors(0),
        mSampleMask(0xffffu),
        mSampleOffset(0),
        mSignalDelay(0),
        mSamplePeriod(0),
        mDivisor(1.0),
        mSampleGain(1.0),
        mLine(txt),
        mOperator(""),
        mFileName(""),
        mUnit(""),
        mLabel(""),
        mValues()
    {
        Parse();
        if (mFileName.size() > 0) {SetSamples();}
    }
    
    MicroSecond SamplePeriod() const
    {
        return mSamplePeriod;
    }

    MicroSecond Duration() const
    {
        return mSignalDelay + SamplePeriod() * mValues.size();
    }

    double At(MicroSecond us) const
    {
        const IntType index = (us - mSignalDelay) / SamplePeriod();
        return ((index >= 0) && (index < mValues.count()))
            ? mValues[index]
            : NAN;
    }

    bool IsValid() const
    {
        return (mErrors == 0);
    }

    bool IsOperator(const QString & arg) const
    {
        return (mOperator == arg);
    }

    void Minus(const InfoLine & other)
    {
        QList<double> result;

        for (IntType us = 0; us < Duration(); us += SamplePeriod())
        {
            const double a = At(us);
            const double b = other.At(us);
            if (isnan(a) || (isnan(b))) continue;
            result.append(a - b);
        }

        mValues = result;
        mSignalDelay = 0;
    }
private:
    void SetSamples()
    {
        QFile read(mFileName);

        if (!read.open(QIODevice::ReadOnly))
        {
            AddError();
            return;
        }

        QDataStream stream(&read);
        stream.setByteOrder(QDataStream::BigEndian);

        while (!stream.atEnd())
        {
            quint16 sample;
            stream >> sample;
            const int lsb = (sample & mSampleMask) - mSampleOffset;
            mValues.append(mSampleGain * lsb);
        }

        read.close();
    }

    void Parse()
    {
        if (!QRegularExpression("^[>+-]").match(mLine).hasMatch())
        {
            mLine = "> " + mLine;
        }

        int optPos = -1;
        bool isValid = true;
        QRegularExpression re("^([>+-])\\s*(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s*");
        QRegularExpressionMatch match = re.match(mLine);

        if (match.hasMatch())
        {
            int sps = 0;
            mOperator = match.captured(1);
            mFileName = match.captured(2);
            if (isValid) {sps = match.captured(3).toInt(&isValid);}
            if (isValid) {mDivisor = match.captured(4).toDouble(&isValid);}
            mUnit = match.captured(5);
            mLabel = match.captured(6);
            optPos = match.capturedEnd(0);

            if (sps == 0)
            {
                AddError();
                return;
            }

            mSamplePeriod = FromMilliSec(1000) / sps;
        }
        else
        {
            AddError();
            return;
        }

        QString dst;
        if (isValid && Find(dst, optPos, "s-mask")) {mSampleMask   = dst.toUInt(&isValid, 0);}
        if (isValid && Find(dst, optPos, "offset")) {mSampleOffset = dst.toInt(&isValid, 0);}
        if (isValid && Find(dst, optPos, "delay"))  {mSignalDelay = FromMilliSec(dst.toInt(&isValid, 0));}
        if (isValid && Find(dst, optPos, "gain"))   {mSampleGain   = dst.toDouble(&isValid);}
        if (!isValid) {AddError();}
    }

    bool Find(QString & dst, int startPos, const QString & key) const
    {
        const QString pattern = key + QString("[=\\s](\\S+)");
        QRegularExpression re(pattern);
        QRegularExpressionMatch match = re.match(mLine, startPos);

        if (match.hasMatch())
        {
            dst = match.captured(1);
            return true;
        }

        dst = "";
        return false;
    }

    void AddError()
    {
        ++mErrors;
        qDebug() << "Could not parse: " << qPrintable(mLine);
    }
};

TEST(InfoLine, Parse)
{
    EXPECT_TRUE(InfoLine("wave.dat 500 1 mV X").IsValid());
    EXPECT_TRUE(InfoLine("wave.dat 500 1.0 mV X").IsValid());
    EXPECT_FALSE(InfoLine("wave.dat 500 1 mV ").IsValid()); // label missing
    EXPECT_FALSE(InfoLine("wave.dat 500 D mV X").IsValid()); // divisor wrong
    EXPECT_FALSE(InfoLine("wave.dat 500.0 1 mV X").IsValid()); // sps wrong
}

////////////////////////////////////////////////////////////////////////////////
// class ChannelData
////////////////////////////////////////////////////////////////////////////////

class ChannelData
{
private:
    QList<InfoLine> mLines;
public:
    void Plus(InfoLine & line)
    {
        mLines.append(line);
    }

    void Minus(InfoLine & line)
    {
        const int index = mLines.count() - 1;
        mLines[index].Minus(line);
    }
    
    const QList<InfoLine> & Lines() const
    {
       return mLines;
    }
};

////////////////////////////////////////////////////////////////////////////////
// class MainData
////////////////////////////////////////////////////////////////////////////////

class MainData
{
private:
    QList<ChannelData> mChannels;
public:
    void OpenFile(const QString & name)
    {
        mChannels.clear();
        QFile file(name);

        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::information(0, "error", file.errorString());
            return;
        }

        QTextStream in(&file);
        QList<InfoLine> lines;

        while (!in.atEnd())
        {
            lines.append(InfoLine(in.readLine()));
        }
        
        for (auto & line:lines)
        {
            if (line.IsValid())
            {
                if (line.IsOperator(">")) New(line);
                if (line.IsOperator("+")) Plus(line);
                if (line.IsOperator("-")) Minus(line);
            }
        }
    }

    const QList<ChannelData> & Channels() const
    {
       return mChannels;
    }
private:
    void New(InfoLine & line)
    {
        ChannelData data;
        mChannels.append(data);
        Plus(line);
    }

    void Plus(InfoLine & line)
    {
        const int index = mChannels.count() - 1;
        mChannels[index].Plus(line);
    }

    void Minus(InfoLine & line)
    {
        const int index = mChannels.count() - 1;
        mChannels[index].Minus(line);
    }
};

////////////////////////////////////////////////////////////////////////////////
// class PixelScaling
////////////////////////////////////////////////////////////////////////////////

class PixelScaling
{
public:
    explicit PixelScaling(int xzoom, int yzoom);
    int MilliMeterAsXPixel(double arg) const;
    int MilliMeterAsYPixel(double arg) const;
    int MilliVoltAsYPixel(MilliVolt arg) const;
    int MicroSecondAsXPixel(MicroSecond arg) const;
    double XPixelAsMilliMeter(int xpx) const;
    double YPixelAsMilliMeter(int ypx) const;
    MilliSecond XPixelAsMilliSecond(int xpx) const;
    MicroSecond XPixelAsMicroSecond(int xpx) const;
    MilliVolt YPixelAsMilliVolt(int ypx) const;
private:
    static double MilliMeterPerMilliVolt() {return 10.0;}
    static double MilliMeterPerSecond() {return 25.0;}
    const int mXZoom;
    const int mYZoom;
    double mXmm;
    double mXpx;
    double mYmm;
    double mYpx;
};

////////////////////////////////////////////////////////////////////////////////
// DrawGrid
////////////////////////////////////////////////////////////////////////////////

class DrawGrid
{
public:
    explicit DrawGrid(const PixelScaling & scaling);
    void Draw(QWidget & parent);
private:
    void DrawTimeGrid(QWidget & parent);
    void DrawValueGrid(QWidget & parent);

    const PixelScaling & mScaling;
};

////////////////////////////////////////////////////////////////////////////////
// DrawChannel
////////////////////////////////////////////////////////////////////////////////

class DrawChannel
{
public:
    explicit DrawChannel(const ChannelData & wave, const PixelScaling & scaling);
    void SetHighlightSamples(bool isTrue) {mIsHighlightSamples = isTrue;}
    void Draw(QWidget & parent, const QRect & rect, int offset);
    int MinimumWidth() const;
private:
    void DrawSamples(QPainter & painter, const QRect & rect);
    int YPixel() const;
    int XPixel() const {return XPixel(mSampleTime);}
    int XPixel(MicroSecond sampleTime) const;
    bool IsValidPoint() const;
    QPoint Point() const;
    const InfoLine & Line() const {return mData.Lines()[mLineIndex];}

    const PixelScaling & mScaling;
    const ChannelData & mData;
    int mChannelOffset;
    MicroSecond mSampleTime;
    int mLineIndex;
    bool mIsHighlightSamples;
};

////////////////////////////////////////////////////////////////////////////////

class Measure;
class MainView : public QWidget
{
public:
    MainView();
    void ZoomIn();
    void ZoomOut();
    void XZoomIn();
    void XZoomOut();
    void YZoomIn();
    void YZoomOut();
    void ResetZoom();
    void SetHighlightSamples(bool isTrue);
    void SetData(MainData & arg);
    void OpenFile(const QString & fileName);
    void UpdateMeasurement(const QRect & rect);
protected:
    void paintEvent(QPaintEvent *);
    void mousePressEvent(QMouseEvent *);
private:
    void InitSize();
    void RebuildView();
    PixelScaling ZoomScaling() const;
    PixelScaling StandardScaling() const;
    int ChannelCount() const {return mDataPtr->Channels().count();}
    const ChannelData & DataChannel(int index) {return mDataPtr->Channels()[index];}

    MainData * mDataPtr;
    Measure * mMeasurePtr;
    int mXZoomValue;
    int mYZoomValue;
    bool mIsHighlightSamples;
};

////////////////////////////////////////////////////////////////////////////////

class Measure : public QWidget
{
public:
    Measure(MainView * parent):
        QWidget(parent),
        mParent(parent)
    {
        QPalette pal = palette();
        pal.setBrush(QPalette::Window, QColor(0, 0, 0, 50) );
        setPalette(pal);
        setAutoFillBackground(true);

        setWindowFlags(Qt::SubWindow);
        QHBoxLayout * layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        QSizeGrip * grip = new QSizeGrip(this);
        layout->addWidget(grip, 0, Qt::AlignRight | Qt::AlignBottom);
    }
private:
    void mousePressEvent(QMouseEvent *evt)
    {
        mLastPos = evt->globalPos();
    }

    void mouseMoveEvent(QMouseEvent *evt)
    {
        const QPoint delta = evt->globalPos() - mLastPos;
        move(x()+delta.x(), y()+delta.y());
        mLastPos = evt->globalPos();
    }

    void moveEvent(QMoveEvent *)
    {
        mParent->UpdateMeasurement(geometry());
    }

    void resizeEvent(QResizeEvent *)
    {
        mParent->UpdateMeasurement(geometry());
    }

    MainView * mParent;
    QPoint mLastPos;
};

////////////////////////////////////////////////////////////////////////////////

class ArgumentParser
{
public:
    ArgumentParser();
    void ParseList(QStringList list);
    void PrintUsage();
    bool IsInvalid() const {return mIsInvalid;}
    bool IsUnitTest() const {return mIsUnitTest;}
    bool IsHighlightSamples() const {return mIsHighlightSamples;}
    bool IsShowHelp() const {return mIsShowHelp;}
    const QStringList & Files() const {return mFiles;}
private:
    void ParseLine(const QString & line);
    bool mIsInvalid;
    bool mIsUnitTest;
    bool mIsHighlightSamples;
    bool mIsShowHelp;
    QString mApplication;
    QStringList mFiles;
};
    
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

int PixelScaling::MilliMeterAsXPixel(double arg) const
{
    const double xpx = arg * mXpx / mXmm;
    return AddZoom(xpx, mXZoom);
}

int PixelScaling::MilliMeterAsYPixel(double arg) const
{
    const double ypx = arg * mYpx / mYmm;
    return AddZoom(ypx, mYZoom);
}

int PixelScaling::MilliVoltAsYPixel(MilliVolt mv) const
{
    return MilliMeterAsYPixel(mv * MilliMeterPerMilliVolt());
}

int PixelScaling::MicroSecondAsXPixel(MicroSecond us) const
{
    const double sec = static_cast<double>(us) / 1000000.0;
    return MilliMeterAsXPixel(sec * MilliMeterPerSecond());
}

double PixelScaling::XPixelAsMilliMeter(int xpx) const
{
    const double xmm = (static_cast<double>(xpx) * mXmm) / mXpx;
    return RemoveZoom(xmm, mXZoom);
}

double PixelScaling::YPixelAsMilliMeter(int px) const
{
    const double mm = (static_cast<double>(px) * mYmm) / mYpx;
    return RemoveZoom(mm, mYZoom);
}

MilliSecond PixelScaling::XPixelAsMilliSecond(int px) const
{
    const FloatType mm = XPixelAsMilliMeter(px);
    return static_cast<MilliSecond>(1000.0 * mm / MilliMeterPerSecond());
}

MicroSecond PixelScaling::XPixelAsMicroSecond(int px) const
{
    return static_cast<MicroSecond>(XPixelAsMilliSecond(px) * 1000ll);
}

MilliVolt PixelScaling::YPixelAsMilliVolt(int px) const
{
    return YPixelAsMilliMeter(px) / MilliMeterPerMilliVolt();
}

////////////////////////////////////////////////////////////////////////////////
// class DrawGrid
////////////////////////////////////////////////////////////////////////////////

DrawGrid::DrawGrid(const PixelScaling & scaling):
    mScaling(scaling)
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
        const int xpx = mScaling.MilliMeterAsXPixel(mm);
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
        const int ypx = mScaling.MilliMeterAsYPixel(mm);
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

DrawChannel::DrawChannel(const ChannelData & data, const PixelScaling & scaling):
    mScaling(scaling),
    mData(data),
    mChannelOffset(0),
    mSampleTime(0),
    mLineIndex(0),
    mIsHighlightSamples(false)
{
}

int DrawChannel::MinimumWidth() const
{
    MicroSecond max = 0;
    for (auto & data:mData.Lines())
    {
        const MicroSecond time = data.Duration();
        if (max < time) max = time;
    }
    return XPixel(max);
}

void DrawChannel::Draw(QWidget & parent, const QRect & rect, int offset)
{
    mChannelOffset = offset;
    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);
    DrawSamples(painter, rect);
}

void DrawChannel::DrawSamples(QPainter & painter, const QRect & rect)
{
    QPen defaultPen(Qt::gray, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen highlightPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    if (!mIsHighlightSamples)
    {
        defaultPen.setColor(Qt::black);
    }

    const MicroSecond drawBegin = mScaling.XPixelAsMicroSecond(rect.left());
    const MicroSecond drawEnd = mScaling.XPixelAsMicroSecond(rect.right());
    const MicroSecond pixelPeriod = mScaling.XPixelAsMicroSecond(1);

    for (mLineIndex = 0; mLineIndex < mData.Lines().count(); ++mLineIndex)
    {
        const MicroSecond samplePeriod = Line().SamplePeriod();
        MicroSecond begin = 0;
        MicroSecond end = Line().Duration();

        if (begin < drawBegin)
        {
            begin = drawBegin;
        }

        if (end > drawEnd)
        {
            end = drawEnd;
        }

        begin -= 2 * (samplePeriod + pixelPeriod);
        end += 2 * (samplePeriod + pixelPeriod);

        mSampleTime = begin;
        QPoint fromPoint = Point();
        bool fromValid = IsValidPoint();
        mSampleTime += samplePeriod;

        while (mSampleTime < end)
        {
            const QPoint toPoint = Point();
            const bool toValid = IsValidPoint();
            mSampleTime += samplePeriod;

            if (fromValid && toValid)
            {
                painter.drawLine(fromPoint, toPoint);

                if (mIsHighlightSamples)
                {
                    painter.setPen(highlightPen);
                    painter.drawPoint(fromPoint);
                    painter.setPen(defaultPen);
                }
            }

            fromValid = toValid;
            fromPoint = toPoint;
        }
    }
}

int DrawChannel::YPixel() const
{
    const double value = Line().At(mSampleTime);
    return mChannelOffset - mScaling.MilliVoltAsYPixel(value);
}

int DrawChannel::XPixel(MicroSecond sampleTime) const
{
    return mScaling.MicroSecondAsXPixel(sampleTime);
}

bool DrawChannel::IsValidPoint() const
{
    return !isnan(Line().At(mSampleTime));
}

QPoint DrawChannel::Point() const
{
    return QPoint(XPixel(), YPixel());
}
    
////////////////////////////////////////////////////////////////////////////////
// class MainView
////////////////////////////////////////////////////////////////////////////////

MainView::MainView():
    QWidget(),
    mDataPtr(nullptr),
    mMeasurePtr(new Measure(this)),
    mXZoomValue(0),
    mYZoomValue(0),
    mIsHighlightSamples(false)
{
    const PixelScaling & zs = ZoomScaling();
    const int x = zs.MilliMeterAsXPixel(25);
    const int y = zs.MilliMeterAsYPixel(10);
    mMeasurePtr->move(x, y);
    mMeasurePtr->resize(x, y);
    mMeasurePtr->setMinimumSize(25, 25);
}

void MainView::ZoomIn()
{
    ++mXZoomValue;
    ++mYZoomValue;
    RebuildView();
}

void MainView::ZoomOut()
{
    --mXZoomValue;
    --mYZoomValue;
    RebuildView();
}

void MainView::XZoomIn()
{
    ++mXZoomValue;
    RebuildView();
}

void MainView::XZoomOut()
{
    --mXZoomValue;
    RebuildView();
}

void MainView::YZoomIn()
{
    ++mYZoomValue;
    RebuildView();
}

void MainView::YZoomOut()
{
    --mYZoomValue;
    RebuildView();
}

void MainView::ResetZoom()
{
    mXZoomValue = 0;
    mYZoomValue = 0;
    RebuildView();
}

void MainView::SetHighlightSamples(bool isTrue)
{
    mIsHighlightSamples = isTrue;
    update();
}

void MainView::OpenFile(const QString & fileName)
{
    mDataPtr->OpenFile(fileName);
    RebuildView();
}

void MainView::SetData(MainData & arg)
{
    mDataPtr = &arg;
    InitSize();
}

void MainView::InitSize()
{
    const PixelScaling & zs = ZoomScaling();
    int minimumWidth = 0;

    for (int index = 0; index < ChannelCount(); ++index)
    {
        DrawChannel channel(DataChannel(index), zs);
        const int channelWidth = channel.MinimumWidth();

        if (minimumWidth < channelWidth)
        {
            minimumWidth = channelWidth;
        }
    }

    const int width = minimumWidth + 10;
    setMinimumWidth(width);
    resize(width, height());
}

void MainView::RebuildView()
{
    InitSize();
    update();
    UpdateMeasurement(mMeasurePtr->rect());
}

void MainView::UpdateMeasurement(const QRect & rect)
{
    const PixelScaling scale = ZoomScaling();
    const MilliSecond ms = scale.XPixelAsMilliSecond(rect.width());
    const MilliVolt mv = scale.YPixelAsMilliVolt(rect.height());
    qDebug() << ms << "ms, " << mv << "mV";
}

void MainView::mousePressEvent(QMouseEvent * evt)
{
    QRect frame = mMeasurePtr->rect();
    frame.moveCenter(evt->pos());
    mMeasurePtr->move(frame.topLeft());
}

void MainView::paintEvent(QPaintEvent * ptr)
{
    DrawGrid grid(StandardScaling());
    grid.Draw(*this);

    const PixelScaling & zs = ZoomScaling();
    const int count = ChannelCount();
    const int widgetHeight = height();

    for (int index = 0; index < count; ++index)
    {
        const int channelHeight = widgetHeight / count;
        const int channelOffset = (index * channelHeight) + (channelHeight / 2);
        DrawChannel channel(DataChannel(index), zs);
        channel.SetHighlightSamples(mIsHighlightSamples);
        channel.Draw(*this, ptr->rect(), channelOffset);
    }
}
    
PixelScaling MainView::ZoomScaling() const
{
    return PixelScaling(mXZoomValue, mYZoomValue);
}

PixelScaling MainView::StandardScaling() const
{
    return PixelScaling(0, 0);
}

////////////////////////////////////////////////////////////////////////////////
// class MainWindow
////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
{
    mMainViewPtr = new MainView();
    mMainViewPtr->setBackgroundRole(QPalette::Base);
    mMainViewPtr->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    mScrollAreaPtr = new QScrollArea();
    mScrollAreaPtr->setBackgroundRole(QPalette::Dark);
    mScrollAreaPtr->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mScrollAreaPtr->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mScrollAreaPtr->setWidgetResizable(true);
    mScrollAreaPtr->setWidget(mMainViewPtr);

    setCentralWidget(mScrollAreaPtr);
    setWindowTitle(QString("no"));
    resize(600, 300);

    mZoomInActionPtr = new QAction(QString("Zoom In"), this);
    mZoomInActionPtr->setShortcut(QKeySequence(Qt::Key_Plus + Qt::KeypadModifier));
    connect(mZoomInActionPtr, SIGNAL(triggered()), this, SLOT(ZoomIn()));

    mZoomOutActionPtr = new QAction(QString("Zoom Out"), this);
    mZoomOutActionPtr->setShortcut(QKeySequence(Qt::Key_Minus + Qt::KeypadModifier));
    connect(mZoomOutActionPtr, SIGNAL(triggered()), this, SLOT(ZoomOut()));

    mXZoomInActionPtr = new QAction(QString("X-Zoom In"), this);
    mXZoomInActionPtr->setShortcut(QKeySequence(Qt::Key_X));
    connect(mXZoomInActionPtr, SIGNAL(triggered()), this, SLOT(XZoomIn()));

    mXZoomOutActionPtr = new QAction(QString("X-Zoom Out"), this);
    mXZoomOutActionPtr->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_X));
    connect(mXZoomOutActionPtr, SIGNAL(triggered()), this, SLOT(XZoomOut()));

    mYZoomInActionPtr = new QAction(QString("Y-Zoom In"), this);
    mYZoomInActionPtr->setShortcut(QKeySequence(Qt::Key_Y));
    connect(mYZoomInActionPtr, SIGNAL(triggered()), this, SLOT(YZoomIn()));

    mYZoomOutActionPtr = new QAction(QString("Y-Zoom Out"), this);
    mYZoomOutActionPtr->setShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Y));
    connect(mYZoomOutActionPtr, SIGNAL(triggered()), this, SLOT(YZoomOut()));

    mZoomResetActionPtr = new QAction(QString("Reset Zoom"), this);
    mZoomResetActionPtr->setShortcut(QKeySequence(Qt::Key_Equal));
    connect(mZoomResetActionPtr, SIGNAL(triggered()), this, SLOT(ResetZoom()));

    mHighlightSamplesActionPtr = new QAction(QString("Highlight &Samples"), this);
    mHighlightSamplesActionPtr->setShortcut(QKeySequence(Qt::Key_H));
    mHighlightSamplesActionPtr->setChecked(false);
    mHighlightSamplesActionPtr->setCheckable(true);
    connect(mHighlightSamplesActionPtr, SIGNAL(triggered()), this, SLOT(HighlightSamples()));

    mOpenActionPtr = new QAction(tr("&Open..."), this);
    mOpenActionPtr->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_O));
    connect(mOpenActionPtr, SIGNAL(triggered()), this, SLOT(OpenFile()));

    mExitActionPtr = new QAction(tr("E&xit"), this);
    mExitActionPtr->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    connect(mExitActionPtr, SIGNAL(triggered()), this, SLOT(ExitApplication()));

    mFileMenuPtr = menuBar()->addMenu(tr("&File"));
    mFileMenuPtr->addAction(mOpenActionPtr);
    mFileMenuPtr->addAction(mExitActionPtr);

    mViewMenuPtr = menuBar()->addMenu(tr("&View"));
    mViewMenuPtr->addAction(mZoomInActionPtr);
    mViewMenuPtr->addAction(mZoomOutActionPtr);
    mViewMenuPtr->addAction(mXZoomInActionPtr);
    mViewMenuPtr->addAction(mXZoomOutActionPtr);
    mViewMenuPtr->addAction(mYZoomInActionPtr);
    mViewMenuPtr->addAction(mYZoomOutActionPtr);
    mViewMenuPtr->addAction(mZoomResetActionPtr);
    mViewMenuPtr->addSeparator();
    mViewMenuPtr->addAction(mHighlightSamplesActionPtr);
    
    // adding all actions to QMainWindow solves issue
    // "keyboard shortcuts not working on ubuntu 14.04":
    // https://bugs.launchpad.net/ubuntu/+source/appmenu-qt5/+bug/1313248
    addAction(mOpenActionPtr);
    addAction(mExitActionPtr);
    addAction(mZoomInActionPtr);
    addAction(mZoomOutActionPtr);
    addAction(mXZoomInActionPtr);
    addAction(mXZoomOutActionPtr);
    addAction(mYZoomInActionPtr);
    addAction(mYZoomOutActionPtr);
    addAction(mZoomResetActionPtr);
    addAction(mHighlightSamplesActionPtr);
}

void MainWindow::OpenFile(const QString & file)
{
    mMainViewPtr->OpenFile(file);
}

void MainWindow::SetData(MainData & data)
{
    mMainViewPtr->SetData(data);
    show();
}

void MainWindow::ZoomIn()
{
    mMainViewPtr->ZoomIn();
}

void MainWindow::ZoomOut()
{
    mMainViewPtr->ZoomOut();
}

void MainWindow::XZoomIn()
{
    mMainViewPtr->XZoomIn();
}

void MainWindow::XZoomOut()
{
    mMainViewPtr->XZoomOut();
}

void MainWindow::YZoomIn()
{
    mMainViewPtr->YZoomIn();
}

void MainWindow::YZoomOut()
{
    mMainViewPtr->YZoomOut();
}

void MainWindow::SetHighlightSamples(bool isTrue)
{
    qDebug() << "SetHighlightSamples " << isTrue;
    mMainViewPtr->SetHighlightSamples(isTrue);
}

void MainWindow::HighlightSamples()
{
    SetHighlightSamples(mHighlightSamplesActionPtr->isChecked());
}

void MainWindow::ResetZoom()
{
    mMainViewPtr->ResetZoom();
}

void MainWindow::ExitApplication()
{
    close();
}

void MainWindow::OpenFile()
{
    OpenFile(QFileDialog::getOpenFileName(this, QString("Open File"),
                QDir::currentPath()));
}

////////////////////////////////////////////////////////////////////////////////
// class ArgumentParser
////////////////////////////////////////////////////////////////////////////////

ArgumentParser::ArgumentParser():
    mIsInvalid(false),
    mIsUnitTest(false),
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
        mIsUnitTest = true;
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
    QApplication app(argc, argv);
    ArgumentParser arguments;
    arguments.ParseList(app.arguments());

    if (arguments.IsUnitTest())
    {
        return LightTest::RunTests(argc, argv);
    }

    if (arguments.IsShowHelp() || arguments.IsInvalid())
    {
        arguments.PrintUsage();
        return 0;
    }

    MainData data;
    MainWindow window;
    window.SetData(data);
    window.SetHighlightSamples(arguments.IsHighlightSamples());

    if (arguments.Files().count() > 0)
    {
        window.OpenFile(arguments.Files()[0]);
    }

    return app.exec();
}


