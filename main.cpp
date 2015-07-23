
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
// DataFile
////////////////////////////////////////////////////////////////////////////////

class DataFile
{
private:
    int mErrors;
    int mSampleMask;
    int mSampleOffset;
    bool mIsSigned;
    bool mIsBigEndian;
    MicroSecond mSignalDelay;
    MicroSecond mSamplePeriod;
    double mDivisor;
    double mSampleGain;
    QString mTxt;
    QString mOperator;
    QString mFileName;
    QString mUnit;
    QString mLabel;
    MilliVolt mMin;
    MilliVolt mMax;
    std::vector<MilliVolt> mValues;
public:
    DataFile & operator=(const DataFile &) = default;
    DataFile(const DataFile &) = default;
    DataFile() = delete;
    explicit DataFile(const QString & txt, const QString & path):
        mErrors(0),
        mSampleMask(0xffff),
        mSampleOffset(0),
        mIsSigned(true),
        mIsBigEndian(true),
        mSignalDelay(0),
        mSamplePeriod(0),
        mDivisor(1.0),
        mSampleGain(1.0),
        mTxt(txt),
        mOperator(""),
        mFileName(""),
        mUnit(""),
        mLabel(""),
        mMin(NAN),
        mMax(NAN),
        mValues()
    {
        Parse();
        if (mFileName.size() > 0) {SetSamples(path);}
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
        const int index = (us - mSignalDelay) / SamplePeriod();
        return ((index >= 0) && (index < static_cast<int>(mValues.size())))
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

    void Minus(const DataFile & other)
    {
        std::vector<MilliVolt> result;

        for (IntType us = 0; us < Duration(); us += SamplePeriod())
        {
            const MilliVolt a = At(us);
            const MilliVolt b = other.At(us);
            if (isnan(a) || (isnan(b))) continue;
            result.push_back(a - b);
        }

        mValues = result;
        mSignalDelay = 0;
    }
private:
    void SetSamples(const QString & path)
    {
        const QString fullName = path + mFileName;
        QFile read(fullName);

        if (!read.open(QIODevice::ReadOnly))
        {
            qDebug() << "Could not open:" << fullName;
            AddError();
            return;
        }

        QDataStream stream(&read);
        stream.setByteOrder(mIsBigEndian ? QDataStream::BigEndian : QDataStream::LittleEndian);

        while (!stream.atEnd())
        {
            int lsb;
            if (mIsSigned)
            {
                const qint16 mask = static_cast<qint16>(mSampleMask);
                qint16 sample;
                stream >> sample;
                lsb = static_cast<int>(sample & mask) - mSampleOffset;
            }
            else
            {
                const quint16 mask = static_cast<quint16>(mSampleMask);
                quint16 sample;
                stream >> sample;
                lsb = static_cast<int>(sample & mask) - mSampleOffset;
            }

            const MilliVolt mv = mSampleGain * lsb;
            if (isnan(mMin) || (mMin > mv)) {mMin = mv;}
            if (isnan(mMax) || (mMax < mv)) {mMax = mv;}
            mValues.push_back(mv);
        }

        read.close();
    }

    void Parse()
    {
        if (!QRegularExpression("^[>+-]").match(mTxt).hasMatch())
        {
            mTxt = "> " + mTxt;
        }

        int optPos = -1;
        bool isValid = true;
        QRegularExpression re("^([>+-])\\s*(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s*");
        QRegularExpressionMatch match = re.match(mTxt);

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
        if (isValid && Find(dst, optPos, "s-mask")) {mSampleMask   = dst.toInt(&isValid, 0);}
        if (isValid && Find(dst, optPos, "offset")) {mSampleOffset = dst.toInt(&isValid, 0);}
        if (isValid && Find(dst, optPos, "delay"))  {mSignalDelay  = FromMilliSec(dst.toInt(&isValid, 0));}
        if (isValid && Find(dst, optPos, "gain"))   {mSampleGain   = dst.toDouble(&isValid);}
        if (!isValid) {AddError();}

        if (mSampleMask == 0x3fff)
        {
            // Many info files do not contain any of the u16/i16 keywords. Thus we are
            // guessing and testing for other typical properties of unsigned ecg samples.
            mIsSigned = ((mSampleOffset != 0x1fff) && (mSampleOffset != 0x2000));
        }

        // Hint: Avoid these keywords. They describe only a part of the data.
        if (Contains(optPos, "swab"))  {mIsBigEndian = false;}
        if (Contains(optPos, "u16"))   {mIsSigned = false;}
        if (Contains(optPos, "i16"))   {mIsSigned = true;}

        // Hint: Use these keywords instead: They fully describe the data.
        if (Contains(optPos, "beu16")) {mIsSigned = false; mIsBigEndian = true;}
        if (Contains(optPos, "leu16")) {mIsSigned = false; mIsBigEndian = false;}
        if (Contains(optPos, "bei16")) {mIsSigned = true;  mIsBigEndian = true;}
        if (Contains(optPos, "lei16")) {mIsSigned = true;  mIsBigEndian = false;}
    }

    bool Contains(int startPos, const QString & key) const
    {
        QRegularExpression re(QString("\\b%1\\b").arg(key));
        return re.match(mTxt, startPos).hasMatch();
    }

    bool Find(QString & dst, int startPos, const QString & key) const
    {
        const QString pattern = key + QString("[=\\s](\\S+)");
        QRegularExpression re(pattern);
        QRegularExpressionMatch match = re.match(mTxt, startPos);

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
        qDebug() << "Could not parse: " << qPrintable(mTxt);
    }
};

TEST(DataFile, Parse)
{
    EXPECT_TRUE(DataFile("wave.dat 500 1 mV X", "").IsValid());
    EXPECT_TRUE(DataFile("wave.dat 500 1.0 mV X", "").IsValid());
    EXPECT_FALSE(DataFile("wave.dat 500 1 mV ", "").IsValid()); // label missing
    EXPECT_FALSE(DataFile("wave.dat 500 D mV X", "").IsValid()); // divisor wrong
    EXPECT_FALSE(DataFile("wave.dat 500.0 1 mV X", "").IsValid()); // sps wrong
}

////////////////////////////////////////////////////////////////////////////////
// class DataChannel
////////////////////////////////////////////////////////////////////////////////

class DataChannel
{
private:
    MilliVolt mMin;
    MilliVolt mMax;
    MicroSecond mDuration;
    std::vector<DataFile> mFiles;
public:
    void Plus(DataFile & file)
    {
        mFiles.push_back(file);
    }

    void Minus(DataFile & file)
    {
        const int index = mFiles.size() - 1;
        mFiles[index].Minus(file);
    }

    void SetComplete()
    {
        if (mFiles.size() < 1)
        {
            mMin = 0;
            mMax = 0;
            mDuration = 0;
            return;
        }

        mMin = mFiles[0].Min();
        mMax = mFiles[0].Max();
        mDuration = mFiles[0].Duration();

        for (auto & file:mFiles)
        {
            if (mMin > file.Min()) {mMin = file.Min();}
            if (mMax < file.Max()) {mMax = file.Max();}
            if (mDuration < file.Duration()) {mDuration = file.Duration();}
        }
    }
    
    const std::vector<DataFile> & Files() const
    {
       return mFiles;
    }

    MilliVolt Min() const {return mMin;}
    MilliVolt Max() const {return mMax;}
    MicroSecond Duration() const {return mDuration;}
};

////////////////////////////////////////////////////////////////////////////////
// class DataMain
////////////////////////////////////////////////////////////////////////////////

class DataMain
{
private:
    std::vector<DataChannel> mChannels;
    MicroSecond mDuration;
    bool mIsValid;
public:
    explicit DataMain(const QString & infoName):
        mChannels(),
        mDuration(0),
        mIsValid(false)
    {
        QTime timer;
        timer.start();
        QFile info(infoName);

        if (!info.open(QIODevice::ReadOnly))
        {
            return;
        }

        const QString path = QFileInfo(info).path() + "/";
        QTextStream in(&info);
        std::vector<DataFile> fileList;

        while (!in.atEnd())
        {
            const QString line = in.readLine();

            if (QRegularExpression("^#").match(line).hasMatch())
            {
                // ignore "comment" lines
                continue;
            }

            if (!QRegularExpression("\\S").match(line).hasMatch())
            {
                // ignore "whitespace only" lines
                continue;
            }

            fileList.push_back(DataFile(line, path));
        }
        
        for (auto & file:fileList)
        {
            if (!file.IsValid()) return;
            if (file.IsOperator(">")) New(file);
            if (file.IsOperator("+")) Plus(file);
            if (file.IsOperator("-")) Minus(file);
        }

        mIsValid = (mChannels.size() > 0);
        mDuration = 0;
        
        for (auto & chan:mChannels)
        {
            chan.SetComplete();
            if (mDuration < chan.Duration()) {mDuration = chan.Duration();}
        }

        const MilliSecond ms = timer.elapsed();
        qDebug() << "DataMain::ctor took" << ms << "ms";
    }
    
    bool IsValid() const {return mIsValid;}
    MicroSecond Duration() const {return mDuration;}

    const std::vector<DataChannel> & Channels() const
    {
       return mChannels;
    }
private:
    void New(DataFile & file)
    {
        DataChannel data;
        mChannels.push_back(data);
        Plus(file);
    }

    void Plus(DataFile & file)
    {
        const int index = mChannels.size() - 1;
        mChannels[index].Plus(file);
    }

    void Minus(DataFile & file)
    {
        const int index = mChannels.size() - 1;
        mChannels[index].Minus(file);
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
    ~DrawChannel();
    explicit DrawChannel(const DataChannel & data, const PixelScaling & scaling,
            MicroSecond tmOffset, int ypxOffset);
    void SetDrawPoints(bool arg) {mDrawPoints = arg;}
    void Draw(QWidget & parent, const QRect & rect);
private:
    void DrawSamples(QPainter & painter, const QRect & rect);
    void DrawSampleWise(QPainter & painter, MicroSecond timeBegin, MicroSecond timeEnd);
    void DrawPixelWise(QPainter & painter, MicroSecond timeBegin, MicroSecond timeEnd);
    const DataFile & File() const {return mData.Files()[mFileIndex];}

    const QPen mLinePen;
    const QPen mPointPen;
    const PixelScaling & mScaling;
    const DataChannel & mData;
    const int mYPixelOffset;
    const MicroSecond mTimeOffset;
    QTime mTimer;
    size_t mFileIndex;
    bool mDrawPoints;
};

////////////////////////////////////////////////////////////////////////////////

class GuiMeasure : public QWidget
{
    Q_OBJECT
public:
    GuiMeasure(QWidget * parent):
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
    bool IsDrawPoints() const {return mDrawPoints;}
    bool IsShowHelp() const {return mIsShowHelp;}
    const QStringList & Files() const {return mFiles;}
private:
    void ParseLine(const QString & file);
    bool mIsInvalid;
    bool mIsUnitTest;
    bool mDrawPoints;
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

DrawChannel::DrawChannel(const DataChannel & data, const PixelScaling & scaling,
        MicroSecond tmOffset, int ypxOffset):
    mLinePen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
    mPointPen(Qt::gray, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
    mScaling(scaling),
    mData(data),
    mYPixelOffset(ypxOffset),
    mTimeOffset(tmOffset),
    mTimer(),
    mFileIndex(0),
    mDrawPoints(false)
{
    mTimer.start();
}

DrawChannel::~DrawChannel()
{
    const MilliSecond ms = mTimer.elapsed();
    qDebug() << "~DrawChannel after" << ms << "ms";
}

void DrawChannel::Draw(QWidget & parent, const QRect & rect)
{
    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(mLinePen);

    DrawSamples(painter, rect);
}

void DrawChannel::DrawSamples(QPainter & painter, const QRect & rect)
{
    const MicroSecond paintBegin = mScaling.XPixelAsMicroSecond(rect.left());
    const MicroSecond paintEnd = mScaling.XPixelAsMicroSecond(rect.right());
    const MicroSecond pixelPeriod = mScaling.XPixelAsMicroSecond(1);

    for (mFileIndex = 0; mFileIndex < mData.Files().size(); ++mFileIndex)
    {
        const MicroSecond samplePeriod = File().SamplePeriod();
        const IntType samplesPerPixel = pixelPeriod / samplePeriod;
        const MicroSecond paintMax = paintEnd + 2 * (samplePeriod + pixelPeriod);
        const MicroSecond fileDuration = File().Duration();
        const MicroSecond fileEnd = paintMax < fileDuration ? paintMax : fileDuration;
        const MicroSecond fileBegin = paintBegin > 0 ? paintBegin : 0;

        if (samplesPerPixel > 9)
        {
            // in case of many samples per pixel it is
            // too slow to draw every single sample.
            DrawPixelWise(painter, fileBegin, fileEnd);
        }
        else
        {
            DrawSampleWise(painter, fileBegin, fileEnd);
        }
    }
}

void DrawChannel::DrawPixelWise(QPainter & painter, MicroSecond timeBegin, MicroSecond timeEnd)
{
    const DataFile & file = File();
    const MicroSecond samplePeriod = file.SamplePeriod();
    MicroSecond time = timeBegin;
    MilliVolt value = 0;

    // jump behind invalid beginning
    for (; time < timeEnd; time += samplePeriod)
    {
        value = file.At(mTimeOffset + time);
        if (!isnan(value)) break;
    }

    MilliVolt min = value;
    MilliVolt max = value;
    MilliVolt old = value;
    int xpxOld = mScaling.MicroSecondAsXPixel(time);

    // process valid samples
    for (; time < timeEnd; time += samplePeriod)
    {
        value = file.At(mTimeOffset + time);
        if (isnan(value)) return;
        const int xpx = mScaling.MicroSecondAsXPixel(time);

        if (xpx > xpxOld)
        {
            // We have reached a new ypixel.
            // - Draw a line from min to max in the old ypixel
            // - Draw a line from the last sample in the old ypixel
            //   to the first sample in the new ypixel.
            const int ypxMin = mYPixelOffset - mScaling.MilliVoltAsYPixel(min);
            const int ypxMax = mYPixelOffset - mScaling.MilliVoltAsYPixel(max);
            const int ypxOld = mYPixelOffset - mScaling.MilliVoltAsYPixel(old);
            const int ypx    = mYPixelOffset - mScaling.MilliVoltAsYPixel(value);

            painter.drawLine(xpxOld, ypxMin, xpxOld, ypxMax);
            painter.drawLine(xpxOld, ypxOld, xpx, ypx);

            min = value;
            max = value;
            old = value;
            xpxOld = xpx;
        }
        else
        {
            // We are still in the same old ypixel.
            // Tracking min and max values is sufficient.
            if (min > value) {min = value;}
            if (max < value) {max = value;}
            old = value;
        }
    }
}

void DrawChannel::DrawSampleWise(QPainter & painter, MicroSecond timeBegin, MicroSecond timeEnd)
{
    const DataFile & file = File();
    const MicroSecond samplePeriod = file.SamplePeriod();
    MicroSecond time = timeBegin;
    MilliVolt value = 0;

    // jump behind invalid beginning
    for (; time < timeEnd; time += samplePeriod)
    {
        value = file.At(mTimeOffset + time);
        if (!isnan(value)) break;
    }
        
    int xpxOld = mScaling.MicroSecondAsXPixel(time);
    int ypxOld = mYPixelOffset - mScaling.MilliVoltAsYPixel(value);

    // process valid samples
    for (; time < timeEnd; time += samplePeriod)
    {
        value = file.At(mTimeOffset + time);
        if (isnan(value)) return;
        const int xpx = mScaling.MicroSecondAsXPixel(time);
        const int ypx = mYPixelOffset - mScaling.MilliVoltAsYPixel(value);
        painter.drawLine(xpxOld, ypxOld, xpx, ypx);
        xpxOld = xpx;
        ypxOld = ypx;

        if (mDrawPoints)
        {
            painter.setPen(mPointPen);
            painter.drawPoint(xpx, ypx);
            painter.setPen(mLinePen);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// class ArgumentParser
////////////////////////////////////////////////////////////////////////////////

ArgumentParser::ArgumentParser():
    mIsInvalid(false),
    mIsUnitTest(false),
    mDrawPoints(false),
    mIsShowHelp(false),
    mFiles()
{
}

void ArgumentParser::ParseList(QStringList list)
{
    if (list.size() > 0)
    {
        mApplication = list[0];
    }

    for (int index = 1; index < list.size(); ++index)
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

    if ((line == QString("-p")) || (line == QString("--points")))
    {
        mDrawPoints = true;
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
    mFiles.push_back(line);
}

void ArgumentParser::PrintUsage()
{
    qDebug("Usage:");
    qDebug("  %s [options] [file]", qPrintable(mApplication));
    qDebug("Options:");
    qDebug("  -t --test   ... execute unit tests");
    qDebug("  -p --points ... draw sample points");
    qDebug("  -h --help   ... show this help");
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
    explicit GuiWave(QWidget * parent, const DataChannel & data, const GuiSetup & setup):
        QWidget(parent),
        mData(data),
        mSetup(setup),
        mMeasure(new GuiMeasure(this)),
        mTimeOffset(0)
    {
        const PixelScaling scale = mSetup.Scaling();
        const int x = scale.MilliMeterAsXPixel(25);
        const int y = scale.MilliMeterAsYPixel(10);
        mMeasure->move(x, y);
        mMeasure->resize(x, y);
        mMeasure->setMinimumSize(25, 25);
        connect(mMeasure, SIGNAL(SignalRecalculate()), this, SLOT(UpdateMeasurement()));
        Rebuild();
    }

    void SetTimeOffset(MicroSecond offset)
    {
        mTimeOffset = offset;
        update();
    }
    
    void Rebuild()
    {
        const PixelScaling scale = mSetup.Scaling();
        setMinimumHeight(scale.MilliVoltAsYPixel(mData.Max() - mData.Min()));
        UpdateMeasurement();
        update();
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
    const DataChannel & mData;
    const GuiSetup & mSetup;
    GuiMeasure * mMeasure;
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
        draw.SetDrawPoints(mSetup.drawPoints);
        draw.Draw(*this, e->rect());
    }

    void resizeEvent(QResizeEvent *) override
    {
        emit SignalResize();
    }
};

class GuiChannel : public QScrollArea
{
private:
    GuiWave * mWave;
public:
    GuiChannel(QWidget * parent, const DataChannel & data, const GuiSetup & setup):
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

class GuiMain : public QWidget
{
    Q_OBJECT
private:
    const GuiSetup & mSetup;    
    const DataMain & mData;
    QScrollBar * mScroll;
    std::vector<GuiChannel *> mChannels;
private slots:
    void MoveTimeBar(int)
    {
        if (mScroll->isSliderDown()) {return;}
        const int pos = mScroll->value();
        const int max = mScroll->maximum();
        const int end = mScroll->pageStep() + max - 10;
        const MicroSecond duration = mData.Duration();
        const MicroSecond offset = duration * pos / end;
        for (auto chan:mChannels) {chan->Wave()->SetTimeOffset(offset);}
    }

    void UpdateTimeBar()
    {
        if (mChannels.size() < 1) return;
        const PixelScaling scale = mSetup.Scaling();
        int page = mChannels[0]->Wave()->width();
        if (page < 2) {page = 2;}
        int end = scale.MicroSecondAsXPixel(mData.Duration());
        if (end < 2) {end = 2;}
        int max = end - page;
        if (max < 0) {max = 0;}
        mScroll->setValue(0);
        mScroll->setMinimum(0);
        mScroll->setMaximum(max);
        mScroll->setPageStep(page);
        mScroll->setSingleStep(page / 2);
    }
public:
    GuiMain(QWidget * parent, const GuiSetup & setup, const DataMain & data):
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
            GuiChannel * gui = new GuiChannel(this, chan, mSetup);
            layout->addWidget(gui);
            mChannels.push_back(gui);

            if (!isResizeConnected)
            {
                isResizeConnected = true;
                connect(gui->Wave(), SIGNAL(SignalResize()), this, SLOT(UpdateTimeBar()));
            }
        }
        layout->addWidget(mScroll);
        setLayout(layout);
        connect(mScroll, SIGNAL(valueChanged(int)), this, SLOT(MoveTimeBar(int)));
    }
    
    void Rebuild()
    {
        for (auto chan:mChannels) {chan->Wave()->Rebuild();}
        UpdateTimeBar();
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    GuiSetup mSetup;
    DataMain * mData;
    GuiMain * mGui;

    void Rebuild() {if (mGui) {mGui->Rebuild();}}
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
    MainWindow():
        mSetup(),
        mData(nullptr),
        mGui(nullptr)
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
#undef ACTION
        QAction * points = new QAction(tr("&Draw Points"), this);
        points->setShortcut(QKeySequence(Qt::Key_P));
        points->setChecked(mSetup.drawPoints);
        points->setCheckable(true);
        connect(points, SIGNAL(triggered(bool)), this, SLOT(Points(bool)));
        viewMenu->addAction(points);
        addAction(points);
    }

    ~MainWindow()
    {
        delete mGui;
        delete mData;
    }

    void Open(QString name)
    {
        delete mGui;
        delete mData;
        mGui = nullptr;
        mData = nullptr;
        mSetup.fileName = name;
        mData = new DataMain(name);
        Unzoom();

        if (mData->IsValid())
        {
            mGui = new GuiMain(this, mSetup, *mData);
        }
        else
        {
            QMessageBox::information(0, "error", "Could not parse " + name);
        }

        setCentralWidget(mGui);
        setWindowTitle(name);
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

    if (arguments.IsShowHelp() || arguments.IsInvalid())
    {
        arguments.PrintUsage();
        return 0;
    }

    MainWindow win;

    if (arguments.Files().size() > 0)
    {
        win.Open(arguments.Files()[0]);
    }

    win.show();
    return app.exec();
}


