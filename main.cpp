
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
    MilliVolt mMin;
    MilliVolt mMax;
    QList<MilliVolt> mValues;
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
        mMin(NAN),
        mMax(NAN),
        mValues()
    {
        Parse();
        if (mFileName.size() > 0) {SetSamples();}
    }

    MilliVolt Min() const {return mMin;}
    MilliVolt Max() const {return mMax;}
    
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
            const MilliVolt mv = mSampleGain * lsb;
            if (isnan(mMin) || (mMin > mv)) {mMin = mv;}
            if (isnan(mMax) || (mMax < mv)) {mMax = mv;}
            mValues.append(mv);
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
    MilliVolt mMin;
    MilliVolt mMax;
    MicroSecond mDuration;
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

    void SetComplete()
    {
        if (mLines.count() < 1)
        {
            mMin = 0;
            mMax = 0;
            mDuration = 0;
            return;
        }

        mMin = mLines[0].Min();
        mMax = mLines[0].Max();
        mDuration = mLines[0].Duration();

        for (auto & line:mLines)
        {
            if (mMin > line.Min()) {mMin = line.Min();}
            if (mMax < line.Max()) {mMax = line.Max();}
            if (mDuration < line.Duration()) {mDuration = line.Duration();}
        }
    }
    
    const QList<InfoLine> & Lines() const
    {
       return mLines;
    }

    MilliVolt Min() const {return mMin;}
    MilliVolt Max() const {return mMax;}
    MicroSecond Duration() const {return mDuration;}
};

////////////////////////////////////////////////////////////////////////////////
// class MainData
////////////////////////////////////////////////////////////////////////////////

class MainData
{
private:
    QList<ChannelData> mChannels;
    MicroSecond mDuration;
public:
    explicit MainData(const QString & name)
    {
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

        mDuration = 0;
        
        for (auto & chan:mChannels)
        {
            chan.SetComplete();
            if (mDuration < chan.Duration()) {mDuration = chan.Duration();}
        }
    }
    
    MicroSecond Duration() const {return mDuration;}

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
// DrawChannel
////////////////////////////////////////////////////////////////////////////////

class DrawChannel
{
public:
    explicit DrawChannel(const ChannelData & wave, const PixelScaling & scaling,
            MicroSecond tmOffset, int ypxOffset);
    void SetHighlightSamples(bool isTrue) {mIsHighlightSamples = isTrue;}
    void Draw(QWidget & parent, const QRect & rect);
private:
    void DrawSamples(QPainter & painter, const QRect & rect);
    bool IsValidPoint() const;
    QPoint Point() const;
    const InfoLine & Line() const {return mData.Lines()[mLineIndex];}

    const PixelScaling & mScaling;
    const ChannelData & mData;
    const int mYPixelOffset;
    const MicroSecond mTimeOffset;
    MicroSecond mSampleTime;
    int mLineIndex;
    bool mIsHighlightSamples;
};

////////////////////////////////////////////////////////////////////////////////

class Measure : public QWidget
{
    Q_OBJECT
public:
    Measure(QWidget * parent):
        QWidget(parent)
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
signals:
    void SignalRecalculate();
private:
    void mousePressEvent(QMouseEvent *evt) override
    {
        mLastPos = evt->globalPos();
    }

    void mouseMoveEvent(QMouseEvent *evt) override
    {
        const QPoint delta = evt->globalPos() - mLastPos;
        move(x()+delta.x(), y()+delta.y());
        mLastPos = evt->globalPos();
    }

    void resizeEvent(QResizeEvent *) override
    {
        emit SignalRecalculate();
    }

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
// class DrawChannel
////////////////////////////////////////////////////////////////////////////////

DrawChannel::DrawChannel(const ChannelData & data, const PixelScaling & scaling,
        MicroSecond tmOffset, int ypxOffset):
    mScaling(scaling),
    mData(data),
    mYPixelOffset(ypxOffset),
    mTimeOffset(tmOffset),
    mSampleTime(0),
    mLineIndex(0),
    mIsHighlightSamples(false)
{
}

void DrawChannel::Draw(QWidget & parent, const QRect & rect)
{
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

bool DrawChannel::IsValidPoint() const
{
    return !isnan(Line().At(mTimeOffset + mSampleTime));
}

QPoint DrawChannel::Point() const
{
    const double value = Line().At(mTimeOffset + mSampleTime);
    const int y = mYPixelOffset - mScaling.MilliVoltAsYPixel(value);
    const int x = mScaling.MicroSecondAsXPixel(mSampleTime);
    return QPoint(x, y);
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
// new gui
////////////////////////////////////////////////////////////////////////////////

struct GuiSetup
{
    QString fileName;
    int xzoom;
    int yzoom;
    bool drawPoints;

    GuiSetup():
        fileName(),
        xzoom(0),
        yzoom(0),
        drawPoints(false)
    {
    }

    PixelScaling Scaling() const
    {
        return PixelScaling(xzoom, yzoom);
    }
};

class GuiWave : public QWidget
{
    Q_OBJECT
public:
    explicit GuiWave(QWidget * parent, const ChannelData & data, const GuiSetup & setup):
        QWidget(parent),
        mData(data),
        mSetup(setup),
        mMeasure(new Measure(this)),
        mTimeOffset(0)
    {
        const PixelScaling scale = mSetup.Scaling();
        setMinimumHeight(scale.MilliVoltAsYPixel(data.Max() - data.Min()));
        const int x = scale.MilliMeterAsXPixel(25);
        const int y = scale.MilliMeterAsYPixel(10);
        mMeasure->move(x, y);
        mMeasure->resize(x, y);
        mMeasure->setMinimumSize(25, 25);
        connect(mMeasure, SIGNAL(SignalRecalculate()), this, SLOT(UpdateMeasurement()));
    }

    void SetTimeOffset(MicroSecond offset)
    {
        mTimeOffset = offset;
        update();
    }
    
    void Rebuild()
    {
        update();
        UpdateMeasurement();
    }
signals:
    void SignalResize();
private slots:
    void UpdateMeasurement()
    {
        const PixelScaling scale = mSetup.Scaling();
        const MilliSecond ms = scale.XPixelAsMilliSecond(mMeasure->width());
        const MilliVolt mv = scale.YPixelAsMilliVolt(mMeasure->height());
        QString txt = QString("%1ms, %2mV").arg(ms).arg(mv);
        qDebug() << txt;
    }
private:
    const ChannelData & mData;
    const GuiSetup & mSetup;
    Measure * mMeasure;
    MicroSecond mTimeOffset;

    void mousePressEvent(QMouseEvent * evt) override
    {
        QRect frame = mMeasure->rect();
        frame.moveCenter(evt->pos());
        mMeasure->move(frame.topLeft());
    }

    void paintEvent(QPaintEvent * e) override
    {
        const PixelScaling scale = mSetup.Scaling();
        const int viewOffset = height() / 2;
        const int dataOffset = scale.MilliVoltAsYPixel(mData.Max() + mData.Min()) / 2;
        const int ypxOffset = viewOffset + dataOffset;
        DrawChannel draw(mData, scale, mTimeOffset, ypxOffset);
        draw.SetHighlightSamples(mSetup.drawPoints);
        draw.Draw(*this, e->rect());
    }

    void resizeEvent(QResizeEvent *) override
    {
        emit SignalResize();
    }
};

class GuiSingleChannel : public QScrollArea
{
private:
    GuiWave * mWave;
public:
    GuiSingleChannel(QWidget * parent, const ChannelData & data, const GuiSetup & setup):
        QScrollArea(parent),
        mWave(new GuiWave(this, data, setup))
    {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setWidgetResizable(true);
        setWidget(mWave);
    }

    GuiWave * Wave()
    {
        return mWave;
    }
};

class GuiMultiChannel : public QWidget
{
    Q_OBJECT
private:
    const GuiSetup & mSetup;    
    const MainData & mData;
    QScrollBar * mScroll;
    QList<GuiSingleChannel *> mChannels;
private slots:
    void MoveTimeBar(int)
    {
        const int pos = mScroll->value();
        const int max = mScroll->maximum();
        const int end = mScroll->pageStep() + max - 10;
        const MicroSecond duration = mData.Duration();
        const MicroSecond offset = duration * pos / end;
        for (auto chan:mChannels) {chan->Wave()->SetTimeOffset(offset);}
    }

    void UpdateTimeBar()
    {
        if (mChannels.count() < 1) return;
        const PixelScaling scale = mSetup.Scaling();
        const int page = mChannels[0]->Wave()->width();
        const int end = scale.MicroSecondAsXPixel(mData.Duration());
        const int max = end - page;
        mScroll->setValue(0);
        mScroll->setMinimum(0);
        mScroll->setMaximum((max < 0) ? 0 : max);
        mScroll->setPageStep(page);
    }
public:
    GuiMultiChannel(QWidget * parent, const GuiSetup & setup, const MainData & data):
        QWidget(parent),
        mSetup(setup),
        mData(data),
        mScroll(new QScrollBar(Qt::Horizontal, this)),
        mChannels()
    {
        QVBoxLayout * layout = new QVBoxLayout(this);
        bool isResizeConnected = false;
        for (auto & chan:mData.Channels())
        {
            GuiSingleChannel * gui = new GuiSingleChannel(this, chan, mSetup);
            layout->addWidget(gui);
            mChannels.append(gui);

            if (!isResizeConnected)
            {
                isResizeConnected = true;
                connect(gui->Wave(), SIGNAL(SignalResize()), this, SLOT(UpdateTimeBar()));
            }
        }
        layout->addWidget(mScroll);
        setLayout(layout);
        connect(mScroll, SIGNAL(valueChanged (int)), this, SLOT(MoveTimeBar(int)));
    }
    
    void Rebuild()
    {
        for (auto chan:mChannels) {chan->Wave()->Rebuild();}
        UpdateTimeBar();
    }
};

class GuiMain : public QMainWindow
{
    Q_OBJECT
private:
    GuiSetup mSetup;
    MainData * mData;
    GuiMultiChannel * mChannels;

    void Rebuild() {if (mChannels) {mChannels->Rebuild();}}
private slots:
    void Open()     {Open(QFileDialog::getOpenFileName(this, QString("Open"), QDir::currentPath()));}
    void Reload()   {Open(mSetup.fileName);}
    void Exit()     {close();}
    void Unzoom()   {mSetup.xzoom = 0; mSetup.yzoom = 0; Rebuild();}
    void ZoomIn()   {mSetup.xzoom++; mSetup.yzoom++; Rebuild();}
    void ZoomOut()  {mSetup.xzoom--; mSetup.yzoom--; Rebuild();}
    void XZoomIn()  {mSetup.xzoom++; Rebuild();}
    void XZoomOut() {mSetup.xzoom--; Rebuild();}
    void YZoomIn()  {mSetup.yzoom++; Rebuild();}
    void YZoomOut() {mSetup.yzoom--; Rebuild();}
    void Points(bool arg) {mSetup.drawPoints = arg; Rebuild();}
public:
    GuiMain():
        mSetup(),
        mData(nullptr),
        mChannels(nullptr)
    {
        setWindowTitle(QString("no"));
        resize(600, 300);
    
#define ACTION(menu, txt, func, key) do {\
    QAction * act = new QAction(QString(txt), this);\
    act->setShortcut(QKeySequence(key));\
    connect(act, SIGNAL(triggered()), this, SLOT(func()));\
    menu->addAction(act);\
    this->addAction(act);\
} while (0)

        QMenu * fileMenu = menuBar()->addMenu(tr("&File"));
        ACTION(fileMenu, "&Open...", Open, QKeySequence::Open);
        ACTION(fileMenu, "&Reload", Reload, Qt::Key_R);
        ACTION(fileMenu, "&Exit", Exit, QKeySequence::Quit);

        QMenu * viewMenu = menuBar()->addMenu(tr("&View"));
        ACTION(viewMenu, "&Unzoom", Unzoom, Qt::Key_U);
        ACTION(viewMenu, "Zoom-&In", ZoomIn, Qt::Key_Plus + Qt::KeypadModifier);
        ACTION(viewMenu, "Zoom-&Out", ZoomOut, Qt::Key_Minus + Qt::KeypadModifier);
        ACTION(viewMenu, "X-Zoom-In", XZoomIn, Qt::Key_X);
        ACTION(viewMenu, "X-Zoom-Out", XZoomOut, Qt::Key_X + Qt::SHIFT);
        ACTION(viewMenu, "Y-Zoom-In", YZoomIn, Qt::Key_Y);
        ACTION(viewMenu, "Y-Zoom-Out", YZoomOut, Qt::Key_Y + Qt::SHIFT);
        ACTION(viewMenu, "&Highlight Samples", HighlightSamples, Qt::Key_H);
        QAction * points = new QAction(tr("&Draw Points"), this);
        points->setShortcut(QKeySequence(Qt::Key_P));
        points->setChecked(mSetup.drawPoints);
        points->setCheckable(true);
        connect(points, SIGNAL(triggered(bool)), this, SLOT(Points(bool)));
        viewMenu->addAction(points);
        addAction(points);
#undef ACTION
    }

    ~GuiMain()
    {
        delete mChannels;
        delete mData;
    }

    void Open(QString name)
    {
        delete mChannels;
        delete mData;
        mSetup.fileName = name;
        mData = new MainData(name);
        mChannels = new GuiMultiChannel(this, mSetup, *mData);
        setCentralWidget(mChannels);
    }
};

#include "main.moc"

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

    GuiMain win;

    if (arguments.Files().count() > 0)
    {
        win.Open(arguments.Files()[0]);
    }

    win.show();
    return app.exec();
}


