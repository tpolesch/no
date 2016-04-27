
#include <QtWidgets>
#include <QDebug>
#include <util/LightTestImplementation.h>

////////////////////////////////////////////////////////////////////////////////

typedef double Second;

////////////////////////////////////////////////////////////////////////////////
// GlobalSetup
////////////////////////////////////////////////////////////////////////////////

enum ByteOrderMode
{
    AutoByteOrder,
    KeepByteOrder,
    SwapByteOrder
};

static QString toString(ByteOrderMode mode)
{
    switch (mode)
    {
    default:
    case AutoByteOrder: return "AutoByteOrder";
    case KeepByteOrder: return "KeepByteOrder";
    case SwapByteOrder: return "SwapByteOrder";
    }
}

class GlobalSetup
{
public:
    static GlobalSetup & Instance()
    {
        static GlobalSetup setup;
        return setup;
    }

    void setFileName(const QString & arg) {mFileName = arg;}
    void setDefaultFont(QWidget * parent) {mDefaultFont = parent->font();}
    void setDisplayMilliSeconds(bool arg) {mDisplayMilliSeconds = arg;}
    void setDebug(bool arg) {mDebug = arg;}
    void setByteOrder(ByteOrderMode arg) {mByteOrder = arg;}

    const QString & fileName() const {return mFileName;}
    const QFont & defaultFont() const {return mDefaultFont;}
    bool displayMilliSeconds() const {return mDisplayMilliSeconds;}
    bool debug() const {return mDebug;}
    ByteOrderMode byteOrder() const {return mByteOrder;}
private:
    GlobalSetup():
        mFileName(),
        mDefaultFont(),
        mByteOrder(AutoByteOrder),
        mDebug(false),
        mDisplayMilliSeconds(false)
    {
    }
private:
    QString mFileName;
    QFont mDefaultFont;
    ByteOrderMode mByteOrder;
    bool mDebug;
    bool mDisplayMilliSeconds;
};

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

class Annotation
{
private:
    Second mSec;
    QString mTxt;
public:
    explicit Annotation(double msec, const QString & txt):
        mSec(msec / 1000.0),
        mTxt(txt)
    {
    }

    void addDelay(Second sec) {mSec += sec;}
    Second sec() const {return mSec;}
    const QString & txt() const {return mTxt;}
};

struct MergedAnnotation
{
    Annotation annotation;
    size_t fileIndex;
};

class Interleave
{
private:
    int mBlockSize;
    int mChannelOffset;
    int mChannelSize;
    int mPosition;
public:
    Interleave():
        mBlockSize(1),
        mChannelOffset(0),
        mChannelSize(1),
        mPosition(0)
    {
    }

    void parse(const QString & txt)
    {
        // some data files contain more multiple channels
        QRegularExpression re(QString("interleave\\s(\\d+)\\s(\\d+)\\s(\\d+)"));
        QRegularExpressionMatch match = re.match(txt);
        if (!match.hasMatch()) {return;}
        mBlockSize = match.captured(1).toInt();
        mChannelOffset = match.captured(2).toInt();
        mChannelSize = match.captured(3).toInt();
        qDebug() << "Interleave" << mBlockSize << mChannelOffset << mChannelSize;
    }

    void first()
    {
        mPosition = 0;
    }

    void next()
    {
        ++mPosition;
        if (mPosition >= mBlockSize) {mPosition = 0;}
    }

    bool isUsed() const
    {
        if (mPosition < mChannelOffset) {return false;}
        return (mPosition < (mChannelOffset + mChannelSize));
    }
};

class InfoParser
{
private:
    QString mRemaining;
public:
    InfoParser(const QString & data):
        mRemaining(data)
    {
    }
    
    const QString & remaining() const
    {
        return mRemaining;
    }

    QString oper()
    {
        // Allow one leading operand:
        // ">" creates a new channel for the info line
        // "+" adds the info line to an existing channel
        // "-" substract the data file from the last data
        //     file in existing channel
        QRegularExpression find("^\\s*([>+-])\\s*");
        QRegularExpressionMatch match = find.match(mRemaining);
        QString result(">");

        if (match.hasMatch())
        {
            result = match.captured(1);
        }
        else
        {
            // minimum: jump behind leading whitespace
            QRegularExpression spaces("^\\s+");
            match = spaces.match(mRemaining);
        }

        if (match.hasMatch())
        {
            mRemaining = mRemaining.remove(0, match.capturedEnd(0));
        }

        return result;
    }

    QString pop()
    {
        // anything between double quotes
        QRegularExpression quoted("^(\".+\")\\s*");
        QRegularExpressionMatch match = quoted.match(mRemaining);

        if (!match.hasMatch())
        {
            // non-whitespace before next whitespace
            QRegularExpression normal("^(\\S+)\\s*");
            match = normal.match(mRemaining);
        }

        if (match.hasMatch())
        {
            mRemaining = mRemaining.remove(0, match.capturedEnd(0));
            return match.captured(1);
        }

        mRemaining.clear();
        return mRemaining;
    }

    QString unquoted(const QString & data) const
    {
        QRegularExpression quoted("^\"(.+)\"$");
        QRegularExpressionMatch match = quoted.match(data);
        return match.hasMatch() ? match.captured(1).trimmed() : data;
    }

    bool tag(const QString & key) const
    {
        QRegularExpression re(QString("\\b%1\\b").arg(key));
        return re.match(remaining()).hasMatch();
    }

    bool value(QString & dst, const QString & key) const
    {
        QRegularExpression re(QString("\\b%1").arg(key) + "[=\\s](\\S+)");
        QRegularExpressionMatch match = re.match(remaining());

        if (match.hasMatch())
        {
            dst = match.captured(1);
            return true;
        }

        dst = "";
        return false;
    }
};

class DataFile
{
private:
    std::vector<int> mSamples;
    std::vector<Annotation> mAnnotations;
    Second mDelay;
    double mSps;
    double mGain;
    QString mTxt;
    QString mPath;
    QString mData;
    QString mAnno;
    QString mOper;
    QString mUnit;
    QString mLabel;
    Interleave mInterleave;
    int mErrors;
    int mSampleMask;
    int mSampleOffset;
    bool mIsSigned;
    bool mIsBigEndian;
    ByteOrderMode mByteOrderMode;
public:
    DataFile & operator=(const DataFile &) = default;
    DataFile(const DataFile &) = default;
    DataFile() = delete;
    explicit DataFile(const QString & txt, const QString & path = ""):
        mSamples(),
        mDelay(0.0),
        mSps(0.0),
        mGain(1.0),
        mTxt(txt),
        mPath(path),
        mData(),
        mAnno(),
        mOper(),
        mUnit(),
        mLabel(),
        mErrors(0),
        mSampleMask(0xffff),
        mSampleOffset(0),
        mIsSigned(true),
        mIsBigEndian(true),
        mByteOrderMode(GlobalSetup::Instance().byteOrder())
    {
        parseInfo();
        readData();
        readAnno();
        debug();
    }

    int sampleMask() const
    {
        return mSampleMask;
    }

    int sampleOffset() const
    {
        return mSampleOffset;
    }

    double gain() const
    {
        return mGain;
    }

    double sps() const
    {
        return mSps;
    }

    Second delay() const
    {
        return mDelay;
    }

    Second duration() const
    {
        return delay() + static_cast<double>(mSamples.size()) / sps();
    }

    const std::vector<int> & samples() const
    {
        return mSamples;
    }

    const std::vector<Annotation> & annotations() const
    {
        return mAnnotations;
    }

    bool valid() const
    {
        return (mErrors == 0);
    }

    const QString & txt() const
    {
        return mTxt;
    }

    const QString & unit() const
    {
        return mUnit;
    }

    const QString & label() const
    {
        return mLabel;
    }

    bool isOperator(const QString & arg) const
    {
        return (mOper == arg);
    }

    void minus(const DataFile & other)
    {
        std::vector<int> result;
        Second time = 0;
        size_t index = 0;

        while (time < duration())
        {
            double a = at(time);
            double b = other.at(time);
            if (isnan(a)) {a = 0;}
            if (isnan(b)) {b = 0;}
            const int lsb = static_cast<int>((a - b) / gain());
            result.push_back(lsb);
            ++index;
            time = static_cast<Second>(index) / sps();
        }

        mSamples = result;
        mDelay = 0;
        mLabel = label() + "-" + other.label();
    }

    int clipIndex(int index) const
    {
        const int max = static_cast<int>(samples().size()) - 1;
        if (index > max) return max;
        if (index < 0) return 0;
        return index;
    }

    struct MinMax {double min; double max;};
    MinMax minmax(int indexBegin, int indexEnd) const
    {
        MinMax result = {0, 0};
        if (samples().size() < 1) return result;

        Q_ASSERT(indexBegin <= indexEnd);
        auto begin = samples().begin() + clipIndex(indexBegin);
        auto end = samples().begin() + clipIndex(indexEnd);
        auto mm = std::minmax_element(begin, end);
        auto one = gain() * (*mm.first);
        auto two = gain() * (*mm.second);

        if (gain() > 0)
        {
            result.min = one;
            result.max = two;
            return result;
        }

        result.max = two;
        result.min = one;
        return result;
    }

private:
    double at(Second sec) const
    {
        const size_t index = static_cast<size_t>((sec - delay()) * sps());
        return (index < samples().size())
            ? (gain() * samples()[index])
            : NAN;
    }

    void readAnno()
    {
        if (mAnno.size() < 1) return;
        const QString name = mPath + mAnno;
        QFile read(name);

        if (!read.open(QIODevice::ReadOnly))
        {
            error();
            return;
        }

        QTextStream stream(&read);
        QRegularExpression re("^\\s*(\\S+)\\s+(.+)\\s*$");

        while (!stream.atEnd())
        {
            const QString line = stream.readLine();
            QRegularExpressionMatch match = re.match(line);

            if (match.hasMatch())
            {
                bool valid = true;
                const Annotation anno(match.captured(1).toDouble(&valid), match.captured(2));
                if (valid) {mAnnotations.push_back(anno);}
            }
            else
            {
                qDebug() << "anno error:" << line;
            }
        }

        read.close();
    }

    void debug() const
    {
        if (!GlobalSetup::Instance().debug()) return;
        QTextStream out(stdout);
        const QString bo = mIsBigEndian
            ? (mIsSigned ? "bei16" : "beu16")
            : (mIsSigned ? "lei16" : "leu16");
        out << mLabel;
        out << "|" << bo;
        out << "|mask=0x" << hex << mSampleMask;
        out << "|offset=0x" << hex << mSampleOffset;
        out << "|delay=" << dec << mDelay;
        out << endl;
    }

    void readData()
    {
        readData(mSamples, mIsBigEndian);
        if (mByteOrderMode == AutoByteOrder) autoByteOrder();
    }

    void autoByteOrder()
    {
        std::vector<int> swap;
        readData(swap, !mIsBigEndian);

        const size_t size = swap.size();
        Q_ASSERT(mSamples.size() == size);
        if (size < 2) return;

        int compare = 0;
        auto o1 = mSamples.begin();
        auto o2 = o1 + 1;
        auto s1 = swap.begin();
        auto s2 = s1 + 1;

        // Idea: The difference between 2 samples is usually small (e.g. ECG baseline
        // sections) but can increase dramatically when assuming the wrong byte order.
        for (size_t index = 1; index < size; ++index, ++o1, ++o2, ++s1, ++s2)
        {
            const int diffOrig = (*o1) - (*o2);
            const int diffSwap = (*s1) - (*s2);
            const int cmp = std::abs(diffOrig) - std::abs(diffSwap);
            if (cmp > 0) ++compare;
            if (cmp < 0) --compare;
        }

        if (compare > 0)
        {
            qDebug() << "autoByteOrder: swap" << mData;
            mIsBigEndian = !mIsBigEndian;
            mSamples = swap;
        }
    }

    void readData(std::vector<int> & dst, bool isBigEndian)
    {
        mInterleave.first();
        dst.clear();

        if (mData == "dummy") return;
        if (mData.size() < 1) return;
        const bool isAbsPath = mData.startsWith('/');
        const QString fullName = (isAbsPath ? (mData) : (mPath + mData));
        QFile read(fullName);

        if (!read.open(QIODevice::ReadOnly))
        {
            error();
            return;
        }

        const size_t size = static_cast<size_t>(QFileInfo(fullName).size());
        dst.reserve(size / sizeof(qint16));
        QDataStream stream(&read);
        stream.setByteOrder(isBigEndian ? QDataStream::BigEndian : QDataStream::LittleEndian);

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

            if (mInterleave.isUsed())
            {
                dst.push_back(lsb);
            }

            mInterleave.next();
        }

        read.close();
    }

    void parseInfo()
    {
        InfoParser parser(mTxt);
        mOper = parser.oper();
        mData = parser.pop();
        const QString txtSps = parser.pop();
        bool done = true;
        mSps  = txtSps.toDouble(&done);
        if (!done) error();
        const QString txtDiv = parser.pop();
        double gainDividend = 1.0;
        const double gainDivisor = txtDiv.toDouble(&done);
        if (!done) error();
        mUnit = parser.pop();
        mLabel = parser.unquoted(parser.pop());

        QString dst;
        if (done && parser.value(dst, "anno_file")) {mAnno = dst;}
        if (done && parser.value(dst, "s-mask")) {mSampleMask = dst.toInt(&done, 16);}
        if (done && parser.value(dst, "offset")) {mSampleOffset = dst.toInt(&done, 0);}
        if (done && parser.value(dst, "delay")) {mDelay = dst.toDouble(&done) / 1000.0;}
        if (done && parser.value(dst, "gain")) {gainDividend = dst.toDouble(&done);}
        if (!done) {error();}

        mGain = gainDividend / gainDivisor;

        if (mSampleMask == 0x3fff)
        {
            // Many info files do not contain any of the u16/i16 keywords. Thus we are
            // guessing and testing for other typical properties of unsigned ecg samples.
            mIsSigned = ((mSampleOffset != 0x1fff) && (mSampleOffset != 0x2000));
        }

        // Hint: Avoid these keywords. They describe only a part of the data.
        if (parser.tag("swab"))  {mIsBigEndian = !mIsBigEndian;}
        if (parser.tag("u16"))   {mIsSigned = false;}
        if (parser.tag("i16"))   {mIsSigned = true;}

        bool keep = false;
        // Hint: Use these keywords instead: They fully describe the data.
        if (parser.tag("beu16")) {keep = true; mIsSigned = false; mIsBigEndian = true;}
        if (parser.tag("leu16")) {keep = true; mIsSigned = false; mIsBigEndian = false;}
        if (parser.tag("bei16")) {keep = true; mIsSigned = true;  mIsBigEndian = true;}
        if (parser.tag("lei16")) {keep = true; mIsSigned = true;  mIsBigEndian = false;}

        if (keep)
        {
            // neither swapping nor auto-detecting
            mByteOrderMode = KeepByteOrder;
        }
        else if (mByteOrderMode == SwapByteOrder)
        {
            mIsBigEndian = !mIsBigEndian;
        }

        mInterleave.parse(parser.remaining());
    }

    void error()
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
    Second mDuration;
    std::vector<DataFile> mFiles;
    std::vector<MergedAnnotation> mMergedAnnotations;
public:
    void plus(DataFile & file)
    {
        mFiles.push_back(file);
    }

    void minus(const DataFile & file)
    {
        if (files().size() < 1) return;
        const size_t index = files().size() - 1;
        mFiles[index].minus(file);
    }

    void done()
    {
        if (files().size() < 1)
        {
            mDuration = 0;
            return;
        }

        size_t index = 0;
        mMergedAnnotations.clear();
        mDuration = files()[0].duration();

        for (auto & file:files())
        {
            if (mDuration < file.duration()) {mDuration = file.duration();}

            for (auto & anno:file.annotations())
            {
                MergedAnnotation ma{anno, index};
                ma.annotation.addDelay(file.delay());
                mMergedAnnotations.push_back(ma);
            }

            ++index;
        }

        auto cmp = [](const MergedAnnotation & a, const MergedAnnotation & b)
        {
            return a.annotation.sec() < b.annotation.sec();
        };

        std::sort(mMergedAnnotations.begin(), mMergedAnnotations.end(), cmp);
    }

    bool hasSamples() const
    {
        for (auto & file:files())
        {
            if (file.samples().size() > 0) return true;
        }

        return false;
    }
    
    const std::vector<MergedAnnotation> & mergedAnnotations() const
    {
        return mMergedAnnotations;
    }
    
    const std::vector<DataFile> & files() const
    {
       return mFiles;
    }

    QString unit() const
    {
        if (files().size() < 1) return "";
        if (files()[0].samples().size() < 1) return "";
        return files()[0].unit();
    }

    Second duration() const {return mDuration;}
};

////////////////////////////////////////////////////////////////////////////////
// class DataMain
////////////////////////////////////////////////////////////////////////////////

class DataMain
{
private:
    std::vector<DataChannel> mChannels;
    Second mDuration;
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
            if (!file.valid())
            {
                qDebug() << "Could not parse:" << file.txt();
                continue;
            }
                
            if (file.isOperator(">")) create(file);
            if (file.isOperator("+")) plus(file);
            if (file.isOperator("-")) minus(file);
        }

        mIsValid = (channels().size() > 0);
        mDuration = 0;
        
        for (auto & chan:mChannels)
        {
            chan.done();
            if (mDuration < chan.duration()) {mDuration = chan.duration();}
        }
    }
    
    bool valid() const {return mIsValid;}
    Second duration() const {return mDuration;}

    const std::vector<DataChannel> & channels() const
    {
       return mChannels;
    }
private:
    void create(DataFile & file)
    {
        DataChannel data;
        mChannels.push_back(data);
        plus(file);
    }

    void plus(DataFile & file)
    {
        if (channels().size() < 1) return;
        const size_t index = channels().size() - 1;
        mChannels[index].plus(file);
    }

    void minus(DataFile & file)
    {
        if (channels().size() < 1) return;
        const size_t index = channels().size() - 1;
        mChannels[index].minus(file);
    }
};

////////////////////////////////////////////////////////////////////////////////
// UnitScale
////////////////////////////////////////////////////////////////////////////////

class UnitScale
{
private:
    const QString mUnit;
    const double mMillimeterPerUnit;
    double mPixelPerMillimeter;
    double mMinData;
    double mMaxData;
    double mMin;
    double mFocus;
    int mPixelSize;
    int mZoom;
public:
    explicit UnitScale(double speed, const QString & unit):
        mUnit(unit),
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

    void setPixelSize(int px)
    {
        qDebug() << "UnitScale::setPixelSize" << mPixelSize << "->" << px;
        mPixelSize = px;
        updateAutoZoom();
    }

    void setFocus(double unit)
    {
        qDebug() << "UnitScale::setFocus" << unit << "min:" << min() << "max:" << max();
        mFocus = unit;
    }

    void setFocusPixel(int px)
    {
        setFocus(fromPixel(px));
    }

    double millimeterToPixel(double mm) const
    {
        return mPixelPerMillimeter * mm;
    }

    double pixelPerUnit() const
    {
        return mPixelPerMillimeter * mmPerUnit();
    }

    int toPixel(double unit) const
    {
        double result = (unit - min()) * pixelPerUnit();
        result += (result > 0) ? 0.5 : -0.5;
        return static_cast<int>(result);
    }

    double pixelToUnit(int px) const
    {
        return px / pixelPerUnit();
    }

    double fromPixel(int px) const
    {
        return min() + pixelToUnit(px);
    }

    void autoZoom(double min, double max)
    {
        qDebug() << "UnitScale::autoZoom" << min << max;
        mMinData = min;
        mMaxData = max;
        updateAutoZoom();
    }
private:
    void updateAutoZoom()
    {
        const double range = mMaxData - mMinData;
        const double offset = (mMaxData + mMinData) / 2;
        mZoom = 0;
        if ((range > 0) && (pixelSize() > 0))
        {
            while (range < unitSize()) {zoomIn();}
            while (range > unitSize()) {zoomOut();}
        }
        mMin = offset - unitSize() / 2;
        mFocus = (min() + max()) / 2;
    }
public:
    void zoomIn()
    {
        ++mZoom;
        mMin += (mFocus - min()) / 2;
        qDebug() << "UnitScale::zoomIn" << mZoom;
    }

    void zoomOut()
    {
        --mZoom;
        mMin -= (mFocus - min());
        qDebug() << "UnitScale::zoomOut" << mZoom;
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
    const QString & unit() const {return mUnit;}
};

class Translate
{
private:
    const UnitScale & mX;
    const UnitScale & mY;
    double mGain;
    double mSps;
    Second mDelay;
public:
    explicit Translate(const UnitScale & x, const UnitScale & y):
        mX(x),
        mY(y),
        mGain(0),
        mSps(0),
        mDelay(0)
    {
    }

    void debug(const QRect & rect)
    {
        int rl = rect.left();
        int rr = rect.right();
        int il = xpxToSampleIndex(rl);
        int ir = xpxToSampleIndex(rr);
        qDebug() << "Translate px:" << rl << rr
            << "index:" << il << ir
            << "time:" << mX.min() << mX.max()
            << "focus:" << mX.focus() 
            << "spp:" << samplesPerPixel();
    }

    void resetData()
    {
        mGain = 0;
        mSps = 0;
        mDelay = 0;
    }

    void setData(const DataFile & data)
    {
        mGain = data.gain();
        mSps = data.sps();
        mDelay = data.delay();
    }

    void setGain(double gain)
    {
        mGain = gain;
    }

    const UnitScale & X() const
    {
        return mX;
    }

    const UnitScale & Y() const
    {
        return mY;
    }

    double samplesPerPixel() const
    {
        return mSps / mX.pixelPerUnit();
    }

    int xpxToSampleIndex(int xpx) const
    {
        return static_cast<int>((mX.fromPixel(xpx) - mDelay) * mSps);
    }

    int sampleIndexToXpx(double idx) const
    {
        return secondToXpx(idx / mSps);
    }

    int secondToXpx(Second sec) const
    {
        return mX.toPixel(sec + mDelay);
    }

    double ypxToUnit(int ypx) const
    {
        return mY.fromPixel(mY.pixelSize() - ypx);
    }
    
    int unitToYpx(double unit) const
    {
        return static_cast<int>(mY.pixelSize() - mY.toPixel(unit));
    }
    
    int lsbToYpx(int lsb) const
    {
        return unitToYpx(mGain * lsb);
    }
};

////////////////////////////////////////////////////////////////////////////////
// DrawChannel
////////////////////////////////////////////////////////////////////////////////

class DrawChannel
{
public:
    explicit DrawChannel(QWidget & parent,
            const QRect & rect,
            const DataChannel & data,
            const UnitScale & timeScale,
            const UnitScale & valueScale);
private:
    struct ColorSchema {QColor dark; QColor normal; QColor anno;};
    void SetColorSchema(size_t index);
    void DrawDecorations(const DataChannel & chan);
    void DrawSampleWise(const DataFile & data);
    void DrawPixelWise(const DataFile & data);
    void DrawAnnotations(const DataChannel & chan);
    void DrawRulers();
    void DrawRange();

    QWidget & mParent;
    const QRect & mRect;
    Translate mTranslate;
    QPainter mPainter;
    MeasurePerformance mMeasurePerformance;
    QPen mDefaultPen;
    ColorSchema mColorSchema;
};

////////////////////////////////////////////////////////////////////////////////

struct TimeValueStrings
{
    QString time;
    QString value;
    enum Mode {Position, Measure} mode;
};

class GuiMeasure : public QWidget
{
private:
    Q_OBJECT
    QPoint mLastPos;
    TimeValueStrings mTimeValue;
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

    void deltaMove(int dx, int dy)
    {
        move(x() + dx, y() + dy);
        emit signalMoved();
    }

    void setTimeValue(const TimeValueStrings & arg)
    {
        mTimeValue = arg;
        update();
    }
signals:
    void signalMoved();
    void signalResized();
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
        emit signalResized();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPen black(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        const int y1 = painter.fontMetrics().ascent();
        const int y2 = height() - 2;
        painter.setPen(black);
        painter.drawText(2, y1, mTimeValue.time);
        painter.drawText(2, y2, mTimeValue.value);

        const int w = width();
        const int h = height();
        const QPen red(Qt::red, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(red);

        const bool isMeasure = (mTimeValue.mode == TimeValueStrings::Measure);
        if (isMeasure)
        {
            // draw red box to signal measure mode
            painter.drawRect(1, 1, w-2, h-2);
        }
        else
        {
            // draw red crosshairs to mark the focus point
            const int x = w / 2;
            const int y = h / 2;
            painter.drawLine(0, y, w, y);
            painter.drawLine(x, 0, x, h);
        }
    }
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

static QString FormatTime(double seconds)
{
    int64_t us = static_cast<int64_t>(std::round(std::abs(1e6 * seconds)));
    int64_t ms = us / 1e3;
    us -= ms * 1e3;
    auto msstr = [](int64_t mst, int64_t ust) {
        return QString("%1.%2ms").arg(mst).arg(ust, 3, 10, QChar('0'));
    };

    if (GlobalSetup::Instance().displayMilliSeconds())
    {
        return msstr(ms, us);
    }

    int64_t factor = 1000 * 60 * 60;
    const int64_t hour = ms / factor;
    ms -= hour * factor;
    factor /= 60;
    const int64_t min = ms / factor;
    ms -= min * factor;
    factor /= 60;
    const int64_t sec = ms / factor;
    ms -= sec * factor;
        
    QString result;
    QTextStream s(&result);
    if (seconds < 0) s << "-";
    if (hour != 0) s << hour << "h";
    if ( min != 0) s << min  << "m";
    if ( sec != 0) s << sec  << "s";
    s << msstr(ms, us);
    return result;
}

////////////////////////////////////////////////////////////////////////////////
// class DrawChannel
////////////////////////////////////////////////////////////////////////////////
    
DrawChannel::DrawChannel(QWidget & parent,
            const QRect & rect,
            const DataChannel & chan,
            const UnitScale & timeScale,
            const UnitScale & valueScale):
    mParent(parent),
    mRect(rect),
    mTranslate(timeScale, valueScale),
    mPainter(&parent),
    mMeasurePerformance("DrawChannel"),
    mDefaultPen(Qt::magenta, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin),
    mColorSchema()
{
    mPainter.setRenderHint(QPainter::Antialiasing, true);
    mPainter.fillRect(mRect, Qt::white);

    size_t dataIndex = 0;
    for (auto & data:chan.files())
    {
        SetColorSchema(dataIndex++);
        mTranslate.setData(data);
        mTranslate.debug(rect);

        if (data.samples().size() > 1)
        {
            if (mTranslate.samplesPerPixel() > 5)
            {
                DrawPixelWise(data);
            }
            else
            {
                DrawSampleWise(data);
            }
        }
    }

    mTranslate.resetData();
    DrawAnnotations(chan);
    DrawDecorations(chan);
    DrawRulers();
    DrawRange();
}

void DrawChannel::DrawDecorations(const DataChannel & chan)
{
    const int x = 20;
    const int step = mPainter.fontMetrics().height();
    int y = 2 * step;

    size_t dataIndex = 0;
    for (auto & data:chan.files())
    {
        SetColorSchema(dataIndex++);
        mPainter.setPen(mDefaultPen);
        mPainter.drawText(QPoint(x, y), data.label());
        y += step;
    }
}

void DrawChannel::DrawRange()
{
    const QPen rulerPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    mPainter.setPen(rulerPen);
    const int pxmax = 0;
    const int pxmin = mParent.height();
    const double max = mTranslate.ypxToUnit(pxmax);
    const double min = mTranslate.ypxToUnit(pxmin);
    const int as = mPainter.fontMetrics().ascent();
    const QString unit = mTranslate.Y().unit();
    mPainter.drawText(QPoint(0, pxmax + as), QString::number(max) + unit);
    mPainter.drawText(QPoint(0, pxmin), QString::number(min) + unit);
}

void DrawChannel::DrawRulers()
{
    const QPen rulerPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    mPainter.setPen(rulerPen);
    const UnitScale & xs = mTranslate.X();
    const UnitScale & ys = mTranslate.Y();
    const double xmm = 25;
    const double ymm = 10;
    const int x1 = mParent.width() - 10;
    const int y1 = mParent.height() - 10;
    const int x2 = x1 - xs.millimeterToPixel(xmm);
    const int y2 = y1 - ys.millimeterToPixel(ymm);
    mPainter.drawLine(QPoint(x1, y1), QPoint(x2, y1));
    mPainter.drawLine(QPoint(x1, y1), QPoint(x1, y2));
    mPainter.drawLine(QPoint(x2, (y1 - 3)), QPoint(x2, (y1 + 3)));
    mPainter.drawLine(QPoint((x1 - 3), y2), QPoint((x1 + 3), y2));
    
    const double xu = xmm / xs.mmPerUnit();
    const double yu = ymm / ys.mmPerUnit();
    const QString xt = FormatTime(xu);
    const QString yt = QString::number(yu) + ys.unit();
    const auto fm = mPainter.fontMetrics();
    const QRect yb = fm.boundingRect(yt);
    mPainter.drawText(QPoint((x1 - yb.width() - 3), (y2 + fm.ascent())), yt);
    mPainter.drawText(QPoint((x2 + 3), y1 - 3), xt);
}

void DrawChannel::DrawAnnotations(const DataChannel & chan)
{

    const int flags = Qt::TextSingleLine|Qt::TextDontClip;
    const int requestLeft = mRect.left();
    const int requestRight = mRect.right();
    const int max = INT_MAX;
    QRect lastBounds(INT_MIN, 0, 0, 0);
    const int bottom = mParent.rect().bottom();

    for (auto & merged:chan.mergedAnnotations())
    {
        SetColorSchema(merged.fileIndex);
        const QPen annoPen(mColorSchema.anno, 1, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin);
        mPainter.setPen(annoPen);

        auto & anno = merged.annotation;
        const int textLeft = mTranslate.secondToXpx(anno.sec());
        if (textLeft > requestRight) return;
        QRect bounds(textLeft, lastBounds.top(), max, max);
        bounds = mPainter.boundingRect(bounds, flags, anno.txt());
        if (bounds.right() < requestLeft) continue;

        const bool isOverlapping = bounds.left() < lastBounds.right();

        if (isOverlapping)
        {
            bounds.moveTop(lastBounds.bottom());

            if (bounds.bottom() > mParent.height())
            {
                bounds.moveTop(0);
            }
        }

        mPainter.drawText(bounds.bottomLeft(), anno.txt());
        mPainter.drawLine(bounds.bottomLeft(), QPoint(bounds.left(), bottom));
        lastBounds = bounds;
    }
}

void DrawChannel::DrawPixelWise(const DataFile & data)
{
    mPainter.setPen(mDefaultPen);
    const int indexEnd = static_cast<int>(data.samples().size()) - 1;
    const int xpxEnd = mRect.right() + 2;
        
    for (int xpx = mRect.left(); xpx < xpxEnd; ++xpx)
    {
        int indexFirst = mTranslate.xpxToSampleIndex(xpx);
        if (indexFirst < 0) indexFirst = 0;
        if (indexFirst > indexEnd) return;

        int indexLast = mTranslate.xpxToSampleIndex(xpx + 1);
        if (indexLast > indexEnd) indexLast = indexEnd;
        if (indexLast < 0) continue;

        // 1st line per xpx:
        // - from last sample in previous xpx
        // - to first sample in current xpx
        auto itFirst = data.samples().begin() + indexFirst;
        auto first = mTranslate.lsbToYpx(*itFirst);
        auto last = mTranslate.lsbToYpx(*(itFirst - 1));
        mPainter.drawLine(xpx - 1, last, xpx, first);

        // 2nd line per xpx:
        // - from min sample in current xpx
        // - to max sample in current xpx
        auto itLast = itFirst + (indexLast - indexFirst);
        auto minmax = std::minmax_element(itFirst, itLast);
        auto min = mTranslate.lsbToYpx(*minmax.first);
        auto max = mTranslate.lsbToYpx(*minmax.second);
        mPainter.drawLine(xpx, min, xpx, max);
    }
}

void DrawChannel::DrawSampleWise(const DataFile & data)
{
    const int indexLeft = mTranslate.xpxToSampleIndex(mRect.left() - 1) - 1;
    const int indexRight = mTranslate.xpxToSampleIndex(mRect.right() + 1) + 1;
    const int indexBegin = data.clipIndex(indexLeft);
    const int indexEnd = data.clipIndex(indexRight);
    if ((indexEnd - indexBegin) < 1) return;

    QPen linePen = mDefaultPen;
    const QPen pointPen(mColorSchema.dark, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const bool drawPoints = mTranslate.samplesPerPixel() < 0.5;
    auto indexNow = indexBegin;
    auto now  = data.samples().begin() + indexNow;
    auto end  = data.samples().begin() + indexEnd;
    auto yold = mTranslate.lsbToYpx(*now);
    auto xold = mTranslate.sampleIndexToXpx(indexNow);
    ++now;
    ++indexNow;

    if (drawPoints)
    {
        linePen.setColor(mColorSchema.normal);
        mPainter.setPen(pointPen);
        mPainter.drawPoint(xold, yold);
    }

    while (now <= end)
    {
        auto ynow = mTranslate.lsbToYpx(*now);
        auto xnow = mTranslate.sampleIndexToXpx(indexNow);
        ++now;
        ++indexNow;
        mPainter.setPen(linePen);
        mPainter.drawLine(xold, yold, xnow, ynow);
        xold = xnow;
        yold = ynow;

        if (drawPoints)
        {
            mPainter.setPen(pointPen);
            mPainter.drawPoint(xnow, ynow);
        }
    }
}

void DrawChannel::SetColorSchema(size_t index)
{
    static const std::vector<ColorSchema> schema = {
        {Qt::black,     Qt::gray,  Qt::lightGray},
        {Qt::darkGreen, Qt::green, Qt::green},
        {Qt::darkRed,   Qt::red,   Qt::red},
        {Qt::darkBlue,  Qt::blue,  Qt::blue},
    };

    if (index >= schema.size()) {index = 0;}
    mColorSchema = schema[index];
    mDefaultPen.setColor(mColorSchema.dark);
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

class GuiWave : public QWidget
{
    Q_OBJECT
public:
    explicit GuiWave(QWidget * parent, const DataChannel & data, double seconds):
        QWidget(parent),
        mData(data),
        mResizeCounter(0),
        mTimeScale(25.0, "s"),
        mValueScale(10.0, data.unit())
    {
        qDebug() << "GuiWave::ctor";
        mTimeScale.setXResolution();
        mTimeScale.setPixelSize(width());
        mTimeScale.autoZoom(0, seconds);
        
        mValueScale.setYResolution();
        mValueScale.setPixelSize(height());
        yzoomAuto();

        setFocusPolicy(Qt::StrongFocus);
    }

    QString FormatValue(double value) const
    {
        QString result;
        QTextStream ss(&result);
        ss << value << mValueScale.unit();
        return result;
    }

    TimeValueStrings focusStrings() const
    {
        TimeValueStrings p;
        p.time = FormatTime(mTimeScale.focus());
        p.value = FormatValue(mValueScale.focus());
        p.mode = TimeValueStrings::Position;
        return p;
    }

    TimeValueStrings measureStrings(QWidget * ptr) const
    {
        TimeValueStrings p;
        p.time = FormatTime(mTimeScale.pixelToUnit(ptr->width()));
        p.value = FormatValue(mValueScale.pixelToUnit(ptr->height()));
        p.mode = TimeValueStrings::Measure;
        return p;
    }

    QString zoomString() const
    {
        QString result;
        QTextStream s(&result);
        s << "zoom = ";
        s << mTimeScale.mmPerUnit() << "mm/" << mTimeScale.unit() << ", ";
        s << mValueScale.mmPerUnit() << "mm/" << mValueScale.unit();
        return result;
    }

    QString timeString() const
    {
        QString result;
        QTextStream s(&result);

        if (mData.files().size() > 0)
        {
            const DataFile & file = mData.files()[0];
            s << "data = " << FormatTime(file.duration()) << ", ";
        }

        s << "visible = {" << FormatTime(mTimeScale.min());
        s << ", " << FormatTime(mTimeScale.max()) << "}";
        return result;
    }

    QString valueString() const
    {
        QString result;
        QTextStream s(&result);
        s << "visible = {" << FormatValue(mValueScale.min());
        s << ", " << FormatValue(mValueScale.max()) << "}";
        return result;
    }

    void setXFocus(int xpx)
    {
        mTimeScale.setFocusPixel(xpx);
    }

    void setYFocus(int ypx)
    {
        const Translate t(mTimeScale, mValueScale);
        mValueScale.setFocus(t.ypxToUnit(ypx));
    }

    void yzoomAuto()
    {
        bool first = true;
        double min = 0;
        double max = 0;
        for (auto & data:mData.files())
        {
            Translate t(mTimeScale, mValueScale);
            t.setData(data);
            auto indexBegin = t.xpxToSampleIndex(0);
            auto indexEnd = t.xpxToSampleIndex(width());
            auto mm = data.minmax(indexBegin, indexEnd);

            if (first)
            {
                first = false;
                min = mm.min;
                max = mm.max;
                continue;
            }

            if (min > mm.min) min = mm.min;
            if (max < mm.max) max = mm.max;
        }

        mValueScale.autoZoom(min, max);
        update();
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
    int mResizeCounter;
    UnitScale mTimeScale;
    UnitScale mValueScale;

    void paintEvent(QPaintEvent * e) override
    {
        DrawChannel(*this, e->rect(), mData, mTimeScale, mValueScale);
    }

    void mousePressEvent(QMouseEvent * evt) override
    {
        emit signalClicked(this, evt);
    }

    void resizeEvent(QResizeEvent *) override
    {
        qDebug() << "GuiWave::resizeEvent" << width() << "X" << height() << mResizeCounter;
        mValueScale.setPixelSize(height());
        if ((mResizeCounter < 100) && (++mResizeCounter < 2))
        {
            // 1st resize is an internal event.
            // following resize events should not touch the time scaling
            mTimeScale.setPixelSize(width());
        }
        update();
    }
    
    void focusInEvent(QFocusEvent *) override
    {
        emit signalSelected(this);
    }
};

class GuiMain : public QWidget
{
    Q_OBJECT
private:
    const DataMain & mData;
    QStatusBar * mStatus;
    GuiMeasure * mMeasure;
    GuiWave * mSelected;
    std::vector<GuiWave *> mChannels;
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

    void slotMeasureResized()
    {
        setFocus();
        statusMeasure();
    }

    void slotMeasureMoved()
    {
        setFocus();
        statusFocus();
    }
public:
    GuiMain(QMainWindow * parent, const DataMain & data):
        QWidget(parent),
        mData(data),
        mStatus(parent->statusBar()),
        mMeasure(nullptr),
        mSelected(nullptr),
        mChannels()
    {
        QVBoxLayout * layout = new QVBoxLayout(this);
        for (auto & chan:mData.channels())
        {
            GuiWave * gui = new GuiWave(this, chan, data.duration());
            layout->addWidget(gui);
            mChannels.push_back(gui);
            connect(gui, SIGNAL(signalClicked(GuiWave *, QMouseEvent *)),
                    this, SLOT(slotWaveClicked(GuiWave *, QMouseEvent *)));
            connect(gui, SIGNAL(signalSelected(GuiWave *)),
                    this, SLOT(slotWaveSelected(GuiWave *)));
        }
        setLayout(layout);
        if (mChannels.size() > 0) {slotWaveSelected(mChannels[0]);}
    }

    void refresh()
    {
        update();
        statusFocus();
    }

    void showStatus(const QString & msg)
    {
        if (mStatus) {mStatus->showMessage(msg);}
    }

    void xzoomIn()  {for (auto & chan:mChannels) {chan->xzoomIn(); }; statusZoom();}
    void xzoomOut() {for (auto & chan:mChannels) {chan->xzoomOut();}; statusZoom();}
    void left()     {for (auto & chan:mChannels) {chan->left();    }; statusTime();}
    void right()    {for (auto & chan:mChannels) {chan->right();   }; statusTime();}
    void yzoomIn()  {if (mSelected) {mSelected->yzoomIn();  }; statusZoom();}
    void yzoomOut() {if (mSelected) {mSelected->yzoomOut(); }; statusZoom();}
    void up()       {if (mSelected) {mSelected->up();       }; statusValue();}
    void down()     {if (mSelected) {mSelected->down();     }; statusValue();}
    void yzoomAuto(){if (mSelected) {mSelected->yzoomAuto();}; statusZoom();}
    void yzoomAutoAll() {for (auto & chan:mChannels) {chan->yzoomAuto();}; statusZoom();}

    void measureLeft()  {mMeasure->deltaMove(-10, 0);}
    void measureRight() {mMeasure->deltaMove( 10, 0);}
    void measureUp()    {mMeasure->deltaMove(0, -10);}
    void measureDown()  {mMeasure->deltaMove(0,  10);}
private:
    void setFocus()
    {
        const QPoint focus = mMeasure->geometry().center();
        for (auto & chan:mChannels) {chan->setXFocus(focus.x());}
        if (mSelected) {mSelected->setYFocus(focus.y());}
    }

    void statusTime()
    {
        statusFocus();
        if (!mSelected) return;
        showStatus(mSelected->timeString());
    }

    void statusValue()
    {
        statusFocus();
        if (!mSelected) return;
        showStatus(mSelected->valueString());
    }

    void statusZoom()
    {
        statusFocus();
        if (!mSelected) return;
        showStatus(mSelected->zoomString());
    }

    void statusFocus()
    {
        if (mSelected && mMeasure)
        {
            mMeasure->setTimeValue(mSelected->focusStrings());
        }
    }

    void statusMeasure()
    {
        if (mSelected && mMeasure)
        {
            mMeasure->setTimeValue(mSelected->measureStrings(mMeasure));
        }
    }

    void setMeasuredWave(GuiWave * wave)
    {
        if ((mSelected == wave) || (wave == nullptr))
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
            geo.setSize(QSize(80, 50));
            geo.moveCenter(wave->rect().center());
        }

        delete mMeasure;
        GuiMeasure * gui = new GuiMeasure(wave);
        gui->setGeometry(geo);
        gui->setMinimumSize(40, 40);
        gui->show();
        connect(gui, SIGNAL(signalMoved()), this, SLOT(slotMeasureMoved()));
        connect(gui, SIGNAL(signalResized()), this, SLOT(slotMeasureResized()));

        mSelected = wave;
        mMeasure = gui;
    }

    void resizeEvent(QResizeEvent *) override
    {
        if (mMeasure && mSelected)
        {
            QRect geo;
            geo.setSize(QSize(80, 50));
            geo.moveCenter(mSelected->rect().center());
            mMeasure->setGeometry(geo);
            slotMeasureMoved();
        }
    }

    void paintEvent(QPaintEvent * e) override
    {
        QPainter painter(this);
        painter.fillRect(e->rect(), QWidget::palette().color(
                    QWidget::backgroundRole()));
    }
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
private:
    DataMain * mData;
    GuiMain * mGui;
private slots:
    void Open()     {Open(QFileDialog::getOpenFileName(this, QString("Open"), QDir::currentPath()));}
    void Reload()   {Open(GlobalSetup::Instance().fileName());}
    void Exit()     {close();}
    void xzoomIn()  {if (mGui) {mGui->xzoomIn();}}
    void xzoomOut() {if (mGui) {mGui->xzoomOut();}}
    void yzoomIn()  {if (mGui) {mGui->yzoomIn();}}
    void yzoomOut() {if (mGui) {mGui->yzoomOut();}}
    void yzoomAuto(){if (mGui) {mGui->yzoomAuto();}}
    void yzoomAutoAll(){if (mGui) {mGui->yzoomAutoAll();}}
    void left()     {if (mGui) {mGui->left();}}
    void right()    {if (mGui) {mGui->right();}}
    void up()       {if (mGui) {mGui->up();}}
    void down()     {if (mGui) {mGui->down();}}
    void measureLeft()  {if (mGui) {mGui->measureLeft();}}
    void measureRight() {if (mGui) {mGui->measureRight();}}
    void measureUp()    {if (mGui) {mGui->measureUp();}}
    void measureDown()  {if (mGui) {mGui->measureDown();}}
    void toggleByteOrder()
    {
        if (!mGui) return;
        GlobalSetup & gs = GlobalSetup::Instance();
        switch (gs.byteOrder())
        {
        default:
        case AutoByteOrder: gs.setByteOrder(KeepByteOrder); break;
        case KeepByteOrder: gs.setByteOrder(SwapByteOrder); break;
        case SwapByteOrder: gs.setByteOrder(AutoByteOrder); break;
        }
        Reload();
        mGui->showStatus(toString(gs.byteOrder()));
    }
    void toggleDebug()
    {
        const bool dbg = !GlobalSetup::Instance().debug();
        GlobalSetup::Instance().setDebug(dbg);
        if (mGui) {mGui->showStatus(dbg ? "Debug:On" : "Debug:Off");}
    }
    void toggleTime()
    {
        if (!mGui) return;
        GlobalSetup & gs = GlobalSetup::Instance();
        gs.setDisplayMilliSeconds(!gs.displayMilliSeconds());
        mGui->refresh();
        mGui->showStatus(gs.displayMilliSeconds() ? "Time/ms" : "Time/0h0m0s0.000ms");
    }
    void toggleFont()
    {
        if (!mGui) return;
        QFont normal = GlobalSetup::Instance().defaultFont();
        QFont small(normal);
        small.setPixelSize(9);
        const bool useSmall = mGui->font().pixelSize() != small.pixelSize();
        mGui->setFont(useSmall ? small : normal);
        mGui->showStatus(useSmall ? "Font:Small" : "Font:Normal");
    }
    void vim()
    {
        const QString cmd = "gvim " + GlobalSetup::Instance().fileName();
        const std::string std = cmd.toStdString();
        const auto rv = system(std.c_str());
        qDebug() << rv;
    }
public:
    MainWindow():
        mData(nullptr),
        mGui(nullptr)
    {
        GlobalSetup::Instance().setDefaultFont(this);
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
        ACTION(fileMenu, "&ByteOrder", toggleByteOrder, Qt::Key_B);
        ACTION(fileMenu, "&Debug", toggleDebug, Qt::Key_D);
        ACTION(fileMenu, "&Vim", vim, Qt::Key_V);
        ACTION(fileMenu, "&Exit", Exit, QKeySequence::Quit);

        QMenu * viewMenu = menuBar()->addMenu(tr("&View"));
        ACTION(viewMenu, "X-Zoom-In", xzoomIn, Qt::Key_X);
        ACTION(viewMenu, "X-Zoom-Out", xzoomOut, Qt::Key_X + Qt::SHIFT);
        ACTION(viewMenu, "Y-Zoom-In", yzoomIn, Qt::Key_Y);
        ACTION(viewMenu, "Y-Zoom-Out", yzoomOut, Qt::Key_Y + Qt::SHIFT);
        ACTION(viewMenu, "Y-Zoom-Auto", yzoomAuto, Qt::Key_A);
        ACTION(viewMenu, "Y-Zoom-Auto-All", yzoomAutoAll, Qt::Key_A + Qt::SHIFT);
        ACTION(viewMenu, "Left", left, Qt::Key_Left);
        ACTION(viewMenu, "Right", right, Qt::Key_Right);
        ACTION(viewMenu, "Up", up, Qt::Key_Up);
        ACTION(viewMenu, "Down", down, Qt::Key_Down);
        ACTION(viewMenu, "Measure-Left", measureLeft, Qt::Key_Left + Qt::SHIFT);
        ACTION(viewMenu, "Measure-Right", measureRight, Qt::Key_Right + Qt::SHIFT);
        ACTION(viewMenu, "Measure-Up", measureUp, Qt::Key_Up + Qt::SHIFT);
        ACTION(viewMenu, "Measure-Down", measureDown, Qt::Key_Down + Qt::SHIFT);
        ACTION(viewMenu, "Font", toggleFont, Qt::Key_F);
        ACTION(viewMenu, "Time", toggleTime, Qt::Key_T);
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
        GlobalSetup::Instance().setFileName(name);
        mData = new DataMain(name);

        if (mData->valid())
        {
            mGui = new GuiMain(this, *mData);
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

TEST(FormatTime, FormatTime)
{
    double time = 0.456001;
    EXPECT_EQ("456.001ms", FormatTime(time));
    time += 3.0;
    EXPECT_EQ("3s456.001ms", FormatTime(time));
    time += 2 * 60;
    EXPECT_EQ("2m3s456.001ms", FormatTime(time));
    time += 60 * 60;
    EXPECT_EQ("1h2m3s456.001ms", FormatTime(time));
    
    EXPECT_EQ("123.456ms", FormatTime(0.1234564));
    EXPECT_EQ("123.457ms", FormatTime(0.1234566));
}

TEST(InfoParser, value)
{
    InfoParser info("one=aaa two=bbb three=ccc");
    QString dst;
    EXPECT_TRUE(info.value(dst, "one"));
    EXPECT_EQ("aaa", dst);
    EXPECT_TRUE(info.value(dst, "two"));
    EXPECT_EQ("bbb", dst);
    EXPECT_TRUE(info.value(dst, "three"));
    EXPECT_EQ("ccc", dst);
}

TEST(InfoParser, tag)
{
    InfoParser info("one two three");
    EXPECT_TRUE(info.tag("one"));
    EXPECT_TRUE(info.tag("two"));
    EXPECT_TRUE(info.tag("three"));
    EXPECT_FALSE(info.tag("\"one\""));
}

TEST(InfoParser, unquoted)
{
    InfoParser info("");
    EXPECT_EQ("quoted 1", info.unquoted("\" \tquoted 1 \""));
}

TEST(InfoParser, pop)
{
    InfoParser info("+   normal \"quoted 1\"  remaining");
    EXPECT_EQ("+", info.oper());
    EXPECT_EQ("normal", info.pop());
    EXPECT_EQ("\"quoted 1\"", info.pop());
    EXPECT_EQ("remaining", info.remaining());
    EXPECT_EQ("remaining", info.pop());
    EXPECT_EQ("", info.remaining());
}

TEST(InfoParser, oper)
{
    InfoParser a("remaining");
    EXPECT_EQ(">", a.oper());
    EXPECT_EQ("remaining", a.remaining());

    InfoParser b("  remaining");
    EXPECT_EQ(">", b.oper());
    EXPECT_EQ("remaining", b.remaining());

    InfoParser c("+remaining");
    EXPECT_EQ("+", c.oper());
    EXPECT_EQ("remaining", c.remaining());

    InfoParser d("  -   remaining");
    EXPECT_EQ("-", d.oper());
    EXPECT_EQ("remaining", d.remaining());
}

inline bool IsEqual(double a, double b)
{
    if (std::abs(a - b) < 0.00001) return true;
    qDebug() << "IsEqual" << a << b;
    return false;
}

TEST(DataFile, parse)
{
    DataFile a("dummy 500 2 mv \"Ecg 1\" gain=0.5 s-mask 32 offset=0x10");
    EXPECT_TRUE(a.valid());
    EXPECT_TRUE(IsEqual(500, a.sps()));
    EXPECT_TRUE(IsEqual(0.25, a.gain()));
    EXPECT_EQ("mv", a.unit());
    EXPECT_EQ("Ecg 1", a.label());
    EXPECT_EQ(0x32, a.sampleMask());
    EXPECT_EQ(16, a.sampleOffset());

    DataFile b("dummy 100 0.5");
    EXPECT_TRUE(b.valid());
    EXPECT_TRUE(IsEqual(100, b.sps()));
    EXPECT_TRUE(IsEqual(2, b.gain()));
    EXPECT_EQ("", b.unit());
    EXPECT_EQ("", b.label());

    DataFile c("dummy x");
    EXPECT_FALSE(c.valid());

    DataFile d("dummy 100 x");
    EXPECT_FALSE(d.valid());
}

TEST(UnitScale, xy)
{
    UnitScale x(25, "s");
    x.setPixelPerMillimeter(40, 10);
    x.setPixelSize(420);
    x.autoZoom(0, 4);
    EXPECT_TRUE(IsEqual(105.0, x.mmSize()));
    EXPECT_TRUE(IsEqual(  1.0, x.zoomFactor()));
    EXPECT_TRUE(IsEqual( 25.0, x.mmPerUnit()));
    EXPECT_TRUE(IsEqual(  4.2, x.unitSize()));
    EXPECT_TRUE(IsEqual( -0.1, x.min()));
    EXPECT_TRUE(IsEqual(  4.1, x.max()));
    EXPECT_TRUE(IsEqual( -0.1, x.fromPixel(0)));
    EXPECT_TRUE(IsEqual(  2.0, x.fromPixel(210)));
    EXPECT_TRUE(IsEqual(  4.1, x.fromPixel(420)));
    EXPECT_TRUE(IsEqual(100.0, x.pixelPerUnit()));
    EXPECT_EQ(  0, x.toPixel(-0.1));
    EXPECT_EQ(210, x.toPixel(2.0));
    EXPECT_EQ(420, x.toPixel(4.1));

    x.autoZoom(0, 5);
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

    UnitScale y(10, "mV");
    y.setPixelPerMillimeter(5, 1);
    y.setPixelSize(500);
    y.autoZoom(-1, 1);
    EXPECT_TRUE(IsEqual(100, y.mmSize()));
    EXPECT_TRUE(IsEqual(2.5, y.unitSize()));
    EXPECT_TRUE(IsEqual(4, y.zoomFactor()));
    EXPECT_TRUE(IsEqual(1.25, y.max()));
    EXPECT_TRUE(IsEqual(-1.25, y.min()));
    
    y.autoZoom(-1, 3);
    EXPECT_TRUE(IsEqual(5, y.unitSize()));
    EXPECT_TRUE(IsEqual(2, y.zoomFactor()));
    EXPECT_TRUE(IsEqual(3.5, y.max()));
    EXPECT_TRUE(IsEqual(-1.5, y.min()));
    
    y.autoZoom(-6, 12);
    EXPECT_TRUE(IsEqual(20, y.unitSize()));
    EXPECT_TRUE(IsEqual(0.5, y.zoomFactor()));
    EXPECT_TRUE(IsEqual(13, y.max()));
    EXPECT_TRUE(IsEqual(-7, y.min()));

    EXPECT_EQ(500, y.toPixel(13));
    EXPECT_EQ(250, y.toPixel(3.0));
    EXPECT_EQ(  0, y.toPixel(-7));

    Translate t(x, y);
    t.setGain(0.5);
    
    EXPECT_EQ(  0, t.unitToYpx(13));
    EXPECT_EQ(250, t.unitToYpx(3.0));
    EXPECT_EQ(500, t.unitToYpx(-7));
    
    EXPECT_EQ(  0, t.lsbToYpx(26));
    EXPECT_EQ(250, t.lsbToYpx( 6));
    EXPECT_EQ(500, t.lsbToYpx(-14));
    
    EXPECT_TRUE(IsEqual(13, t.ypxToUnit(0)));
    EXPECT_TRUE(IsEqual( 3, t.ypxToUnit(250)));
    EXPECT_TRUE(IsEqual(-7, t.ypxToUnit(500)));
}


