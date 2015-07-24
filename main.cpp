
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
    MicroSecond mSignalDelay;
    MicroSecond mSamplePeriod;
    double mSampleGain;
    int mErrors;
    int mSampleMask;
    int mSampleOffset;
    int mMin;
    int mMax;
    std::vector<int> mValues;
    bool mIsSigned;
    bool mIsBigEndian;
    QString mTxt;
    QString mOperator;
    QString mFileName;
    QString mUnit;
    QString mLabel;
public:
    DataFile & operator=(const DataFile &) = default;
    DataFile(const DataFile &) = default;
    DataFile() = delete;
    explicit DataFile(const QString & txt, const QString & path):
        mSignalDelay(0),
        mSamplePeriod(0),
        mSampleGain(1.0),
        mErrors(0),
        mSampleMask(0xffff),
        mSampleOffset(0),
        mMin(0),
        mMax(0),
        mValues(),
        mIsSigned(true),
        mIsBigEndian(true),
        mTxt(txt),
        mOperator(""),
        mFileName(""),
        mUnit(""),
        mLabel("")
    {
        Parse();
        if (mFileName.size() > 0) {SetSamples(path);}
    }

    MilliVolt Min() const
    {
        return SampleGain() * mMin;
    }
    
    MilliVolt Max() const
    {
        return SampleGain() * mMax;
    }
    
    double SampleGain() const
    {
        return mSampleGain;
    }

    MicroSecond SamplePeriod() const
    {
        return mSamplePeriod;
    }

    MicroSecond SignalDelay() const
    {
        return mSignalDelay;
    }

    MicroSecond Duration() const
    {
        return mSignalDelay + SamplePeriod() * mValues.size();
    }
    
    const std::vector<int> & Values() const
    {
        return mValues;
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
        std::vector<int> result;

        for (MicroSecond us = 0; us < Duration(); us += SamplePeriod())
        {
            const MilliVolt a = At(us);
            const MilliVolt b = other.At(us);
            if (isnan(a) || (isnan(b))) continue;
            const int lsb = static_cast<int>((a - b) / SampleGain());
            result.push_back(lsb);
        }

        mValues = result;
        mSignalDelay = 0;
    }

    double At(MicroSecond us) const
    {
        const int index = (us - mSignalDelay) / SamplePeriod();
        return ((index >= 0) && (index < static_cast<int>(mValues.size())))
            ? (mSampleGain * mValues[index])
            : NAN;
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

        mValues.reserve(QFileInfo(fullName).size() / sizeof(qint16));
        QDataStream stream(&read);
        stream.setByteOrder(mIsBigEndian ? QDataStream::BigEndian : QDataStream::LittleEndian);
        bool isFirst = true;

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

            mValues.push_back(lsb);
            if (isFirst || (mMin > lsb)) {mMin = lsb;}
            if (isFirst || (mMax < lsb)) {mMax = lsb;}
            isFirst = false;
        }

        read.close();
    }

    void Parse()
    {
        if (!QRegularExpression("^[>+-]").match(mTxt).hasMatch())
        {
            mTxt = "> " + mTxt;
        }

        double div = 1.0;
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
            if (isValid) {div = match.captured(4).toDouble(&isValid);}
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
        if (isValid && Find(dst, optPos, "gain"))   {mSampleGain   = dst.toDouble(&isValid) / div;}
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
private:
    static double MilliMeterPerMilliVolt() {return 10.0;}
    static double MilliMeterPerSecond() {return 25.0;}
    double mLsbGain;
    int mLsbOffset;
    int mXmm;
    int mXpx;
    int mYmm;
    int mYpx;
public:
    explicit PixelScaling(int xzoom, int yzoom);
    void SetLsbGain(double arg) {mLsbGain = arg;}
    void SetLsbOffset(int arg) {mLsbOffset = arg;}

    inline int MilliMeterAsXPixel(double arg) const
    {
        return arg * mXpx / mXmm;
    }

    inline int MilliMeterAsYPixel(double arg) const
    {
        return arg * mYpx / mYmm;
    }

    inline int MilliVoltAsYPixel(MilliVolt mv) const
    {
        return MilliMeterAsYPixel(mv * MilliMeterPerMilliVolt());
    }

    inline int MicroSecondAsXPixel(MicroSecond us) const
    {
        const double sec = static_cast<double>(us) / 1000000.0;
        return MilliMeterAsXPixel(sec * MilliMeterPerSecond());
    }

    inline double XPixelAsMilliMeter(int xpx) const
    {
        return (static_cast<double>(xpx) * mXmm) / mXpx;
    }

    inline double YPixelAsMilliMeter(int px) const
    {
        return (static_cast<double>(px) * mYmm) / mYpx;
    }

    inline MilliSecond XPixelAsMilliSecond(int px) const
    {
        const FloatType mm = XPixelAsMilliMeter(px);
        return static_cast<MilliSecond>(1000.0 * mm / MilliMeterPerSecond());
    }

    inline MicroSecond XPixelAsMicroSecond(int px) const
    {
        return static_cast<MicroSecond>(XPixelAsMilliSecond(px) * 1000ll);
    }

    inline MilliVolt YPixelAsMilliVolt(int px) const
    {
        return YPixelAsMilliMeter(px) / MilliMeterPerMilliVolt();
    }

    inline int LsbAsYPixel(int lsb) const
    {
        return mLsbOffset - MilliVoltAsYPixel(mLsbGain * lsb);
    }
};

PixelScaling::PixelScaling(int xzoom, int yzoom):
    mLsbGain(1.0),
    mLsbOffset(0)
{
    const QDesktopWidget desk;
    mXmm = desk.widthMM();
    mYmm = desk.heightMM();
    mXpx = desk.width();
    mYpx = desk.height();
    if (xzoom > 0) {mXpx *= (1 << xzoom);}
    if (yzoom > 0) {mYpx *= (1 << yzoom);}
    if (xzoom < 0) {mXmm *= (1 << (-xzoom));}
    if (yzoom < 0) {mYmm *= (1 << (-yzoom));}
}

////////////////////////////////////////////////////////////////////////////////
// DrawChannel
////////////////////////////////////////////////////////////////////////////////

class DrawChannel
{
public:
    ~DrawChannel();
    explicit DrawChannel(const DataChannel & data, const PixelScaling & scale,
            MicroSecond scrollTime);
    void SetDrawPoints(bool arg) {mDrawPoints = arg;}
    void Draw(QWidget & parent, const QRect & rect);
private:
    void DrawSamples(QPainter & painter, const QRect & rect);
    void DrawSampleWise(QPainter & painter, const QRect & rect, const DataFile & data);
    void DrawPixelWise(QPainter & painter, const QRect & rect, const DataFile & data);
    const DataFile & File() const {return mData.Files()[mFileIndex];}

    const QPen mPointPen;
    QPen mLinePen;
    PixelScaling mScale;
    const DataChannel & mData;
    const MicroSecond mScrollTime;
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
// class DrawChannel
////////////////////////////////////////////////////////////////////////////////

DrawChannel::DrawChannel(const DataChannel & data, const PixelScaling & scale,
        MicroSecond scrollTime):
    mPointPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
    mLinePen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
    mScale(scale),
    mData(data),
    mScrollTime(scrollTime),
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
    if (mDrawPoints) {mLinePen.setColor(Qt::gray);}
    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(mLinePen);

    DrawSamples(painter, rect);
}

void DrawChannel::DrawSamples(QPainter & painter, const QRect & rect)
{
    auto pixelPeriod = mScale.XPixelAsMicroSecond(1);

    for (auto & data:mData.Files())
    {
        mScale.SetLsbGain(data.SampleGain());
        auto samplesPerPixel = pixelPeriod / data.SamplePeriod();

        if (samplesPerPixel > 9)
        {
            DrawPixelWise(painter, rect, data);
        }
        else
        {
            DrawSampleWise(painter, rect, data);
        }
    }
}

void DrawChannel::DrawPixelWise(QPainter & painter, const QRect & rect, const DataFile & data)
{
    const MicroSecond samplePeriod = data.SamplePeriod();
    const MicroSecond timeOffset = mScrollTime - data.SignalDelay();
    const int indexEnd = data.Values().size() - 1;
    const int xpxEnd = rect.right();

    for (int xpx = rect.left(); xpx < xpxEnd; ++xpx)
    {
        const MicroSecond timeFirst = mScale.XPixelAsMicroSecond(xpx);
        const int indexFirst = (timeFirst + timeOffset) / samplePeriod;
        if (indexFirst < 1) continue;

        const MicroSecond timeLast = mScale.XPixelAsMicroSecond(xpx + 1);
        const int indexLast = (timeLast + timeOffset) / samplePeriod;
        if (indexLast > indexEnd) return;

        // 1st line per xpx:
        // - from last sample in previous xpx
        // - to first sample in current xpx
        auto itFirst = data.Values().begin() + indexFirst;
        auto first = mScale.LsbAsYPixel(*itFirst);
        auto last = mScale.LsbAsYPixel(*(itFirst - 1));
        painter.drawLine(xpx - 1, last, xpx, first);

        // 2nd line per xpx:
        // - from min sample in current xpx
        // - to max sample in current xpx
        auto itLast = itFirst + (indexLast - indexFirst);
        auto minmax = std::minmax_element(itFirst, itLast);
        auto min = mScale.LsbAsYPixel(*minmax.first);
        auto max = mScale.LsbAsYPixel(*minmax.second);
        painter.drawLine(xpx, min, xpx, max);
    }
}

void DrawChannel::DrawSampleWise(QPainter & painter, const QRect & rect, const DataFile & data)
{
    const MicroSecond samplePeriod = data.SamplePeriod();
    const MicroSecond timeOffset = mScrollTime - data.SignalDelay();
    const MicroSecond timeFirst = mScale.XPixelAsMicroSecond(rect.left());
    const MicroSecond timeLast = mScale.XPixelAsMicroSecond(rect.right());
    const int indexFirst = (timeFirst + timeOffset) / samplePeriod;
    const int indexLast = (timeLast + timeOffset) / samplePeriod;
    const int indexBegin = (indexFirst < 0) ? 0 : indexFirst;
    const int indexMax = data.Values().size() - 1;
    const int indexEnd = (indexLast > indexMax) ? indexMax : indexLast;
    if ((indexEnd - indexBegin) < 2) return;

    auto now  = data.Values().begin() + indexBegin;
    auto end  = data.Values().begin() + indexEnd;
    auto yold = mScale.LsbAsYPixel(*now);
    auto time = timeFirst + (indexBegin - indexFirst) * samplePeriod;
    auto xold = mScale.MicroSecondAsXPixel(time);
    ++now;
    time += samplePeriod;

    while (now < end)
    {
        auto ynow = mScale.LsbAsYPixel(*now);
        auto xnow = mScale.MicroSecondAsXPixel(time);
        ++now;
        time += samplePeriod;
        painter.drawLine(xold, yold, xnow, ynow);
        xold = xnow;
        yold = ynow;

        if (mDrawPoints)
        {
            painter.setPen(mPointPen);
            painter.drawPoint(xnow, ynow);
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
        mScrollTime(0)
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

    void SetScrollTime(MicroSecond scrollTime)
    {
        mScrollTime = scrollTime;
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
    MicroSecond mScrollTime;

    void mousePressEvent(QMouseEvent * evt) override
    {
        QRect frame = mMeasure->rect();
        frame.moveCenter(evt->pos());
        mMeasure->move(frame.topLeft());
    }

    void paintEvent(QPaintEvent * e) override
    {
        PixelScaling scale = mSetup.Scaling();
        const int viewOffset = height() / 2;
        const int dataOffset = scale.MilliVoltAsYPixel(mData.Max() + mData.Min()) / 2;
        scale.SetLsbOffset(viewOffset + dataOffset);
        DrawChannel draw(mData, scale, mScrollTime);
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
        const int pos = mScroll->value();
        const int max = mScroll->maximum();
        const int end = mScroll->pageStep() + max - 10;
        const MicroSecond duration = mData.Duration();
        const MicroSecond scrollTime = duration * pos / end;
        for (auto chan:mChannels) {chan->Wave()->SetScrollTime(scrollTime);}
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


