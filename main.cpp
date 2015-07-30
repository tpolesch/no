
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
// MeasurePerformance
////////////////////////////////////////////////////////////////////////////////

#ifdef IS_DEBUG_BUILD
class MeasurePerformance
{
private:
    const char * mName;
    QTime mTimer;
public:
    MeasurePerformance(const char * name):
        mName(name),
        mTimer()
    {
        mTimer.start();
    }

    ~MeasurePerformance()
    {
        qDebug() << mName << mTimer.elapsed() << "ms";
    }
};
#endif

#ifdef IS_RELEASE_BUILD
class MeasurePerformance
{
public:
    MeasurePerformance(const char *) {}
    ~MeasurePerformance() {}
};
#endif

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
        if (mFileName == "dummy") return;
        if (mFileName.size() < 1) return;
        SetSamples(path);
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

    const QString & Txt() const
    {
        return mTxt;
    }

    const QString & Unit() const
    {
        return mUnit;
    }

    bool IsOperator(const QString & arg) const
    {
        return (mOperator == arg);
    }

    void Minus(const DataFile & other)
    {
        std::vector<int> result;
        mSignalDelay = 0;

        for (MicroSecond us = 0; us < Duration(); us += SamplePeriod())
        {
            const MilliVolt a = At(us);
            const MilliVolt b = other.At(us);

            if (isnan(a) || (isnan(b)))
            {
                if (result.size() > 0) {break;}
                mSignalDelay = us;
                continue;
            }

            const int lsb = static_cast<int>((a - b) / SampleGain());
            result.push_back(lsb);
        }

        mValues = result;
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
    }
};

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
        MeasurePerformance measure("DataMain::ctor");
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
            if (!file.IsValid())
            {
                qDebug() << "Could not parse:" << file.Txt();
                return;
            }

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
    }
    
    bool IsValid() const {return mIsValid;}
    MicroSecond Duration() const {return mDuration;}
    double Seconds() const {return static_cast<double>(Duration()) / 1000000.0;}

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
// UnitScale
////////////////////////////////////////////////////////////////////////////////

class UnitScale
{
private:
    const double mMillimeterPerUnit;
    double mPixelPerMillimeter;
    double mMinData;
    double mMaxData;
    double mMin;
    double mFocus;
    int mPixelSize;
    int mZoom;
public:
    explicit UnitScale(double speed):
        mMillimeterPerUnit(speed),
        mPixelPerMillimeter(0),
        mMinData(0),
        mMaxData(0),
        mMin(0),
        mFocus(0),
        mPixelSize(0),
        mZoom(0)
    {
    }

    void setYResolution()
    {
        const QDesktopWidget desk;
        setPixelPerMillimeter(desk.height(), desk.heightMM());
    }

    void setXResolution()
    {
        const QDesktopWidget desk;
        setPixelPerMillimeter(desk.width(), desk.widthMM());
    }

    void setPixelPerMillimeter(double px, double mm)
    {
        mPixelPerMillimeter = px / mm;
    }

    void setPixel(int px)
    {
        mPixelSize = px;
        update();
    }

    void setFocus(double unit)
    {
        qDebug() << "setFocus:" << unit << "min:" << min() << "max:" << max();
        mFocus = unit;
    }

    void setFocusPixel(int px)
    {
        setFocus(FromPixel(px));
    }

    double pixelPerUnit() const
    {
        return mPixelPerMillimeter * mmPerUnit();
    }

    int ToPixel(double unit) const
    {
        double result = (unit - min()) * pixelPerUnit();
        result += (result > 0) ? 0.5 : -0.5;
        return static_cast<int>(result);
    }

    double PixelToUnit(int px) const
    {
        return px / pixelPerUnit();
    }

    double FromPixel(int px) const
    {
        return min() + PixelToUnit(px);
    }

    void setData(double minData, double maxData)
    {
        mMinData = minData;
        mMaxData = maxData;
        update();
    }

    void update()
    {
        const double range = mMaxData - mMinData;
        const double offset = (mMaxData + mMinData) / 2;
        mZoom = 0;
        while (range > unitSize()) {zoomOut();}
        mMin = offset - unitSize() / 2;
        mFocus = (min() + max()) / 2;
    }

    void zoomIn()
    {
        ++mZoom;
        mMin += (mFocus - min()) / 2;
    }

    void zoomOut()
    {
        --mZoom;
        mMin -= (mFocus - min());
    }

    void scrollLeft()
    {
        scroll(-unitSize() / 4);
    }

    void scrollRight()
    {
        scroll(unitSize() / 4);
    }

    void scroll(double unit)
    {
        mMin += unit;
        mFocus += unit;
    }

    int pixelSize() const
    {
        return mPixelSize;
    }

    double mmSize() const
    {
        return static_cast<double>(pixelSize()) / mPixelPerMillimeter;
    }

    double unitSize() const
    {
        return mmSize() / mmPerUnit();
    }

    double mmPerUnit() const
    {
        return mMillimeterPerUnit * zoomFactor();
    }

    double zoomFactor() const
    {
        return ((mZoom < 0) ? (1.0 / (1 << (-mZoom))) : (1 << mZoom));
    }

    double focus() const {return mFocus;}
    double min() const {return mMin;}
    double max() const {return min() + unitSize();}
};

class Translate
{
private:
    const UnitScale & mX;
    const UnitScale & mY;
    double mGain;
    MicroSecond mDelay;
    MicroSecond mSamplePeriod;
public:
    explicit Translate(const UnitScale & x, const UnitScale & y):
        mX(x),
        mY(y),
        mGain(0),
        mDelay(0),
        mSamplePeriod(0)
    {
    }

    void debug(const QRect & rect)
    {
        int rl = rect.left();
        int rr = rect.right();
        int il = XPixelToSampleIndex(rl);
        int ir = XPixelToSampleIndex(rr);
        qDebug() << "Translate px:" << rl << rr
            << "index:" << il << ir
            << "time:" << mX.min() << mX.max()
            << "focus:" << mX.focus() 
            << "spp:" << samplesPerPixel();
    }

    void setData(const DataFile & data)
    {
        mGain = data.SampleGain();
        mDelay = data.SignalDelay();
        mSamplePeriod = data.SamplePeriod();
    }

    void setGain(double gain)
    {
        mGain = gain;
    }

    double samplesPerPixel() const
    {
        const double sps = 1000000.0 / mSamplePeriod;
        return sps / mX.pixelPerUnit();
    }

    int XPixelToSampleIndex(int xpx) const
    {
        return (XPixelToMicroSecond(xpx) - mDelay) / mSamplePeriod;
    }

    int SampleIndexToXPixel(int idx) const
    {
        return MicroSecondToXPixel(idx * mSamplePeriod + mDelay);
    }

    MilliVolt YPixelToUnit(int ypx) const
    {
        return mY.FromPixel(mY.pixelSize() - ypx);
    }
    
    int UnitToYPixel(double unit) const
    {
        return mY.pixelSize() - mY.ToPixel(unit);
    }
    
    int LsbToYPixel(int lsb) const
    {
        return UnitToYPixel(mGain * lsb);
    }
private:
    MicroSecond XPixelToMicroSecond(int xpx) const
    {
        return static_cast<MicroSecond>(1000000.0 * mX.FromPixel(xpx));
    }

    int MicroSecondToXPixel(MicroSecond us) const
    {
        return mX.ToPixel(static_cast<double>(us) / 1000000.0);
    }
};

////////////////////////////////////////////////////////////////////////////////
// DrawChannel
////////////////////////////////////////////////////////////////////////////////

class DrawChannel
{
public:
    explicit DrawChannel(const DataChannel & data,
            const UnitScale & timeScale,
            const UnitScale & valueScale);
    void Draw(QWidget & parent, const QRect & rect);
private:
    void DrawSamples(QPainter & painter, const QRect & rect);
    void DrawSampleWise(QPainter & painter, const QRect & rect, const DataFile & data);
    void DrawPixelWise(QPainter & painter, const QRect & rect, const DataFile & data);

    const DataChannel & mData;
    Translate mTranslate;
    MeasurePerformance mMeasurePerformance;
    const QPen mPointPen;
    QPen mLinePen;
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
    void signalMoved();
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

    void mouseReleaseEvent(QMouseEvent *) override
    {
        emit signalMoved();
    }

    void resizeEvent(QResizeEvent *) override
    {
        emit signalMoved();
    }

    void paintEvent(QPaintEvent *) override
    {
        // draw red crosshairs to mark the focus point
        QPen pen(Qt::red, 1, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(pen);
        const int w = width();
        const int h = height();
        const int x = w / 2;
        const int y = h / 2;
        painter.drawLine(0, y, w, y);
        painter.drawLine(x, 0, x, h);
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

DrawChannel::DrawChannel(const DataChannel & data,
        const UnitScale & timeScale,
        const UnitScale & valueScale):
    mData(data),
    mTranslate(timeScale, valueScale),
    mMeasurePerformance("DrawChannel"),
    mPointPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
    mLinePen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)
{
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
    for (auto & data:mData.Files())
    {
        mTranslate.setData(data);
        mTranslate.debug(rect);

        if (mTranslate.samplesPerPixel() > 5)
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
    const int indexEnd = data.Values().size() - 1;
    const int xpxEnd = rect.right() + 2;
        
    for (int xpx = rect.left(); xpx < xpxEnd; ++xpx)
    {
        const int indexFirst = mTranslate.XPixelToSampleIndex(xpx);
        if (indexFirst < 1) continue;

        const int indexLast = mTranslate.XPixelToSampleIndex(xpx + 1);
        if (indexLast > indexEnd) return;

        // 1st line per xpx:
        // - from last sample in previous xpx
        // - to first sample in current xpx
        auto itFirst = data.Values().begin() + indexFirst;
        auto first = mTranslate.LsbToYPixel(*itFirst);
        auto last = mTranslate.LsbToYPixel(*(itFirst - 1));
        painter.drawLine(xpx - 1, last, xpx, first);

        // 2nd line per xpx:
        // - from min sample in current xpx
        // - to max sample in current xpx
        auto itLast = itFirst + (indexLast - indexFirst);
        auto minmax = std::minmax_element(itFirst, itLast);
        auto min = mTranslate.LsbToYPixel(*minmax.first);
        auto max = mTranslate.LsbToYPixel(*minmax.second);
        painter.drawLine(xpx, min, xpx, max);
    }
}

void DrawChannel::DrawSampleWise(QPainter & painter, const QRect & rect, const DataFile & data)
{
    const int indexFirst = mTranslate.XPixelToSampleIndex(rect.left());
    const int indexLast = mTranslate.XPixelToSampleIndex(rect.right() + 2);
    const int indexBegin = (indexFirst < 0) ? 0 : indexFirst;
    const int indexMax = data.Values().size();
    const int indexEnd = (indexLast > indexMax) ? indexMax : indexLast;
    if ((indexEnd - indexBegin) < 2) return;

    const bool drawPoints = mTranslate.samplesPerPixel() < 0.5;
    auto indexNow = indexBegin;
    auto now  = data.Values().begin() + indexNow;
    auto end  = data.Values().begin() + indexEnd;
    auto yold = mTranslate.LsbToYPixel(*now);
    auto xold = mTranslate.SampleIndexToXPixel(indexNow);
    ++now;
    ++indexNow;

    if (drawPoints)
    {
        painter.setPen(mPointPen);
        painter.drawPoint(xold, yold);
        mLinePen.setColor(Qt::gray);
        painter.setPen(mLinePen);
    }

    while (now < end)
    {
        auto ynow = mTranslate.LsbToYPixel(*now);
        auto xnow = mTranslate.SampleIndexToXPixel(indexNow);
        ++now;
        ++indexNow;
        painter.drawLine(xold, yold, xnow, ynow);
        xold = xnow;
        yold = ynow;

        if (drawPoints)
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
        std::cout << "Unknown option: " << longOption.cap(0).toStdString() << std::endl;
        mIsInvalid = true;
        return;
    }

    const QRegExp shortOption("-(\\w)");

    if (shortOption.exactMatch(line))
    {
        std::cout << "Unknown option: " << shortOption.cap(0).toStdString() << std::endl;
        mIsInvalid = true;
        return;
    }

    // assume filename argument
    mFiles.push_back(line);
}

void ArgumentParser::PrintUsage()
{
    std::stringstream ss;
    ss << "Usage:" << std::endl;
    ss << "  " << mApplication.toStdString() << " [options] [file]" << std::endl;
    ss << "Options:" << std::endl;
    ss << "  -t --test   ... execute unit tests" << std::endl;
    ss << "  -h --help   ... show this help" << std::endl;
    std::cout << ss.str();
}

////////////////////////////////////////////////////////////////////////////////
// new gui
////////////////////////////////////////////////////////////////////////////////

struct GuiSetup
{
    QString fileName;
};

class GuiWave : public QWidget
{
    Q_OBJECT
public:
    explicit GuiWave(QWidget * parent, const DataChannel & data, double seconds):
        QWidget(parent),
        mData(data),
        mTimeScale(25.0),
        mValueScale(10.0)
    {
        mTimeScale.setXResolution();
        mTimeScale.setPixel(width());
        mTimeScale.setData(0, seconds);
        
        mValueScale.setYResolution();
        mValueScale.setPixel(height());
        mValueScale.setData(data.Min(), data.Max());

        setFocusPolicy(Qt::StrongFocus);
    }

    QString status(const GuiMeasure & measure) const
    {
        QString unit = mData.Files().size() > 0
            ? mData.Files()[0].Unit()
            : QString("?");
        double time = mTimeScale.PixelToUnit(measure.width());
        QString timeUnit("s");

        if (time < 1)
        {
            time *= 1000;
            timeUnit = "ms";
        }
        else if (time > 3600)
        {
            time /= 3600;
            timeUnit = "h";
        }
        else if (time > 60)
        {
            time /= 60;
            timeUnit = "min";
        }

        QString result;
        QTextStream txt(&result);
        txt << "Z={"
            << mTimeScale.mmPerUnit() << "mm/s, "
            << mValueScale.mmPerUnit() << "mm/" << unit
            << "}, M={"
            << time << timeUnit << ", "
            << mValueScale.PixelToUnit(measure.height()) << unit << "}";
        return result;
    }

    void setXFocus(int xpx)
    {
        mTimeScale.setFocusPixel(xpx);
    }

    void setYFocus(int ypx)
    {
        const Translate t(mTimeScale, mValueScale);
        mValueScale.setFocus(t.YPixelToUnit(ypx));
    }

    void xzoomIn()  {mTimeScale.zoomIn(); update();}
    void xzoomOut() {mTimeScale.zoomOut(); update();}
    void yzoomIn()  {mValueScale.zoomIn(); update();}
    void yzoomOut() {mValueScale.zoomOut(); update();}
    void left()     {mTimeScale.scrollLeft(); update();}
    void right()    {mTimeScale.scrollRight(); update();}
    void down()     {mValueScale.scrollLeft(); update();}
    void up()       {mValueScale.scrollRight(); update();}
signals:
    void signalClicked(GuiWave *, QMouseEvent *);
    void signalSelected(GuiWave *);
private:
    const DataChannel & mData;
    UnitScale mTimeScale;
    UnitScale mValueScale;

    void paintEvent(QPaintEvent * e) override
    {
        DrawChannel draw(mData, mTimeScale, mValueScale);
        draw.Draw(*this, e->rect());
    }

    void mousePressEvent(QMouseEvent * evt) override
    {
        emit signalClicked(this, evt);
    }

    void resizeEvent(QResizeEvent *) override
    {
        mValueScale.setPixel(height());
        mTimeScale.setPixel(width());
        update();
    }
    
    void focusInEvent(QFocusEvent *) override
    {
        emit signalSelected(this);
    }
};

class GuiChannel : public QWidget
{
private:
    GuiWave * mWave;
public:
    GuiChannel(QWidget * parent, const DataChannel & data, double seconds):
        QWidget(parent),
        mWave(new GuiWave(this, data, seconds))
    {
        QVBoxLayout * layout = new QVBoxLayout(this);
        layout->addWidget(mWave);
        setLayout(layout);
    }

    void xzoomIn()  {wave().xzoomIn();}
    void xzoomOut() {wave().xzoomOut();}
    void yzoomIn()  {wave().yzoomIn();}
    void yzoomOut() {wave().yzoomOut();}
    void left()     {wave().left();}
    void right()    {wave().right();}
    void down()     {wave().down();}
    void up()       {wave().up();}

    GuiWave & wave() {return *mWave;}
};

class GuiMain : public QWidget
{
    Q_OBJECT
private:
    const GuiSetup & mSetup;    
    const DataMain & mData;
    QStatusBar * mStatus;
    GuiMeasure * mMeasure;
    GuiWave * mMeasuredWave;
    std::vector<GuiChannel *> mChannels;
private slots:
    void slotWaveSelected(GuiWave * sender)
    {
        setMeasuredWave(sender);
        slotMeasureMoved();
    }

    void slotWaveClicked(GuiWave * sender, QMouseEvent * event)
    {
        setMeasuredWave(sender);
        QRect frame = mMeasure->rect();
        frame.moveCenter(event->pos());
        mMeasure->move(frame.topLeft());
        slotMeasureMoved();
    }

    void slotMeasureMoved()
    {
        const QPoint focus = mMeasure->geometry().center();
        for (auto & chan:mChannels) {chan->wave().setXFocus(focus.x());}
        if (mMeasuredWave) {mMeasuredWave->setYFocus(focus.y());}
        updateStatus();
    }
public:
    GuiMain(QMainWindow * parent, const GuiSetup & setup, const DataMain & data):
        QWidget(parent),
        mSetup(setup),
        mData(data),
        mStatus(parent->statusBar()),
        mMeasure(nullptr),
        mMeasuredWave(nullptr),
        mChannels()
    {
        QVBoxLayout * layout = new QVBoxLayout(this);
        for (auto & chan:mData.Channels())
        {
            GuiChannel * gui = new GuiChannel(this, chan, data.Seconds());
            layout->addWidget(gui);
            mChannels.push_back(gui);
            connect(&gui->wave(), SIGNAL(signalClicked(GuiWave *, QMouseEvent *)),
                    this, SLOT(slotWaveClicked(GuiWave *, QMouseEvent *)));
            connect(&gui->wave(), SIGNAL(signalSelected(GuiWave *)),
                    this, SLOT(slotWaveSelected(GuiWave *)));
        }
        setLayout(layout);
    }

    void xzoomIn()  {for (auto & chan:mChannels) {chan->xzoomIn(); }; updateStatus();}
    void xzoomOut() {for (auto & chan:mChannels) {chan->xzoomOut();}; updateStatus();}
    void yzoomIn()  {for (auto & chan:mChannels) {chan->yzoomIn(); }; updateStatus();}
    void yzoomOut() {for (auto & chan:mChannels) {chan->yzoomOut();}; updateStatus();}
    void left()     {for (auto & chan:mChannels) {chan->left();    }; updateStatus();}
    void right()    {for (auto & chan:mChannels) {chan->right();   }; updateStatus();}
    void up()       {for (auto & chan:mChannels) {chan->up();      }; updateStatus();}
    void down()     {for (auto & chan:mChannels) {chan->down();    }; updateStatus();}
private:
    void updateStatus()
    {
        if (mMeasuredWave && mMeasure)
        {
            mStatus->showMessage(mMeasuredWave->status(*mMeasure));
        }
        else
        {
            mStatus->clearMessage();
        }
    }

    void setMeasuredWave(GuiWave * wave)
    {
        if ((mMeasuredWave == wave) || (wave == nullptr))
        {
            return;
        }

        QRect geo;

        if (mMeasure)
        {
            geo = mMeasure->geometry();
        }
        else
        {
            geo.setSize(QSize(50, 50));
            geo.moveCenter(wave->rect().center());
        }

        delete mMeasure;
        GuiMeasure * gui = new GuiMeasure(wave);
        gui->setGeometry(geo);
        gui->setMinimumSize(30, 30);
        gui->show();
        connect(gui, SIGNAL(signalMoved()), this, SLOT(slotMeasureMoved()));

        mMeasuredWave = wave;
        mMeasure = gui;
    }

    void resizeEvent(QResizeEvent *) override
    {
        if (mMeasure && mMeasuredWave)
        {
            QRect geo;
            geo.setSize(QSize(50, 50));
            geo.moveCenter(mMeasuredWave->rect().center());
            mMeasure->setGeometry(geo);
            slotMeasureMoved();
        }
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    GuiSetup mSetup;
    DataMain * mData;
    GuiMain * mGui;
private slots:
    void Open()     {Open(QFileDialog::getOpenFileName(this, QString("Open"), QDir::currentPath()));}
    void Reload()   {Open(mSetup.fileName);}
    void Exit()     {close();}
    void xzoomIn()  {if (mGui) {mGui->xzoomIn();}}
    void xzoomOut() {if (mGui) {mGui->xzoomOut();}}
    void yzoomIn()  {if (mGui) {mGui->yzoomIn();}}
    void yzoomOut() {if (mGui) {mGui->yzoomOut();}}
    void left()     {if (mGui) {mGui->left();}}
    void right()    {if (mGui) {mGui->right();}}
    void up()       {if (mGui) {mGui->up();}}
    void down()     {if (mGui) {mGui->down();}}
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
        ACTION(viewMenu, "X-Zoom-In", xzoomIn, Qt::Key_X);
        ACTION(viewMenu, "X-Zoom-Out", xzoomOut, Qt::Key_X + Qt::SHIFT);
        ACTION(viewMenu, "Y-Zoom-In", yzoomIn, Qt::Key_Y);
        ACTION(viewMenu, "Y-Zoom-Out", yzoomOut, Qt::Key_Y + Qt::SHIFT);
        ACTION(viewMenu, "Left", left, Qt::Key_Left);
        ACTION(viewMenu, "Right", right, Qt::Key_Right);
        ACTION(viewMenu, "Up", up, Qt::Key_Up);
        ACTION(viewMenu, "Down", down, Qt::Key_Down);
#undef ACTION
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

////////////////////////////////////////////////////////////////////////////////
// main()
////////////////////////////////////////////////////////////////////////////////

#include "main.moc"

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

////////////////////////////////////////////////////////////////////////////////
// Testing
////////////////////////////////////////////////////////////////////////////////

TEST(DataFile, Parse)
{
    EXPECT_TRUE(DataFile("dummy 500 1 mV X", "").IsValid());
    EXPECT_TRUE(DataFile("dummy 500 1.0 mV X", "").IsValid());
    EXPECT_FALSE(DataFile("dummy 500 1 mV ", "").IsValid()); // label missing
    EXPECT_FALSE(DataFile("dummy 500 D mV X", "").IsValid()); // divisor wrong
    EXPECT_FALSE(DataFile("dummy 500.0 1 mV X", "").IsValid()); // sps wrong
}

inline bool IsEqual(double a, double b)
{
    if (std::fabs(a - b) < 0.00001) return true;
    qDebug() << "IsEqual" << a << b;
    return false;
}

TEST(UnitScale, xy)
{
    UnitScale x(25);
    x.setPixelPerMillimeter(40, 10);
    x.setPixel(420);
    x.setData(0, 4);
    EXPECT_TRUE(IsEqual(105.0, x.mmSize()));
    EXPECT_TRUE(IsEqual(  1.0, x.zoomFactor()));
    EXPECT_TRUE(IsEqual( 25.0, x.mmPerUnit()));
    EXPECT_TRUE(IsEqual(  4.2, x.unitSize()));
    EXPECT_TRUE(IsEqual( -0.1, x.min()));
    EXPECT_TRUE(IsEqual(  4.1, x.max()));
    EXPECT_TRUE(IsEqual( -0.1, x.FromPixel(0)));
    EXPECT_TRUE(IsEqual(  2.0, x.FromPixel(210)));
    EXPECT_TRUE(IsEqual(  4.1, x.FromPixel(420)));
    EXPECT_TRUE(IsEqual(100.0, x.pixelPerUnit()));
    EXPECT_EQ(  0, x.ToPixel(-0.1));
    EXPECT_EQ(210, x.ToPixel(2.0));
    EXPECT_EQ(420, x.ToPixel(4.1));

    x.setData(0, 5);
    EXPECT_TRUE(IsEqual( 0.5, x.zoomFactor()));
    EXPECT_TRUE(IsEqual(12.5, x.mmPerUnit()));
    EXPECT_TRUE(IsEqual( 8.4, x.unitSize()));
    EXPECT_TRUE(IsEqual(-1.7, x.min()));
    EXPECT_TRUE(IsEqual( 6.7, x.max()));

    x.scroll(1.7);
    EXPECT_TRUE(IsEqual(0.0, x.min()));
    EXPECT_TRUE(IsEqual(8.4, x.max()));

    x.setFocus(2.1);
    x.zoomOut();
    EXPECT_TRUE(IsEqual(-2.1, x.min()));
    EXPECT_TRUE(IsEqual(14.7, x.max()));

    x.zoomIn();
    EXPECT_TRUE(IsEqual(0.0, x.min()));
    EXPECT_TRUE(IsEqual(8.4, x.max()));

    x.zoomIn();
    EXPECT_TRUE(IsEqual(1.05, x.min()));
    EXPECT_TRUE(IsEqual(5.25, x.max()));

    UnitScale y(10);
    y.setPixelPerMillimeter(5, 1);
    y.setPixel(500);
    y.setData(-1, 1);
    EXPECT_TRUE(IsEqual(100, y.mmSize()));
    EXPECT_TRUE(IsEqual(10, y.unitSize()));
    EXPECT_TRUE(IsEqual(1, y.zoomFactor()));
    EXPECT_TRUE(IsEqual(5, y.max()));
    EXPECT_TRUE(IsEqual(-5, y.min()));
    
    y.setData(-1, 3);
    EXPECT_TRUE(IsEqual(10, y.unitSize()));
    EXPECT_TRUE(IsEqual(1, y.zoomFactor()));
    EXPECT_TRUE(IsEqual(6, y.max()));
    EXPECT_TRUE(IsEqual(-4, y.min()));
    
    y.setData(-6, 12);
    EXPECT_TRUE(IsEqual(20, y.unitSize()));
    EXPECT_TRUE(IsEqual(0.5, y.zoomFactor()));
    EXPECT_TRUE(IsEqual(13, y.max()));
    EXPECT_TRUE(IsEqual(-7, y.min()));

    EXPECT_EQ(500, y.ToPixel(13));
    EXPECT_EQ(250, y.ToPixel(3.0));
    EXPECT_EQ(  0, y.ToPixel(-7));

    Translate t(x, y);
    t.setGain(0.5);
    
    EXPECT_EQ(  0, t.UnitToYPixel(13));
    EXPECT_EQ(250, t.UnitToYPixel(3.0));
    EXPECT_EQ(500, t.UnitToYPixel(-7));
    
    EXPECT_EQ(  0, t.LsbToYPixel(26));
    EXPECT_EQ(250, t.LsbToYPixel( 6));
    EXPECT_EQ(500, t.LsbToYPixel(-14));
    
    EXPECT_TRUE(IsEqual(13, t.YPixelToUnit(0)));
    EXPECT_TRUE(IsEqual( 3, t.YPixelToUnit(250)));
    EXPECT_TRUE(IsEqual(-7, t.YPixelToUnit(500)));
}


