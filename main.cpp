
#include <QtWidgets>
#include <QDebug>
#include <util/LightTestImplementation.h>

////////////////////////////////////////////////////////////////////////////////

typedef double Second;

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
    std::vector<int> mSamples;
    Second mDelay;
    double mSps;
    double mGain;
    QString mTxt;
    QString mOperator;
    QString mFileName;
    QString mUnit;
    QString mLabel;
    int mErrors;
    int mSampleMask;
    int mSampleOffset;
    int mSampleMin;
    int mSampleMax;
    bool mIsSigned;
    bool mIsBigEndian;
public:
    DataFile & operator=(const DataFile &) = default;
    DataFile(const DataFile &) = default;
    DataFile() = delete;
    explicit DataFile(const QString & txt, const QString & path):
        mSamples(),
        mDelay(0.0),
        mSps(0.0),
        mGain(1.0),
        mTxt(txt),
        mOperator(""),
        mFileName(""),
        mUnit(""),
        mLabel(""),
        mErrors(0),
        mSampleMask(0xffff),
        mSampleOffset(0),
        mSampleMin(0),
        mSampleMax(0),
        mIsSigned(true),
        mIsBigEndian(true)
    {
        parse();
        if (mFileName == "dummy") return;
        if (mFileName.size() < 1) return;
        setSamples(path);
    }

    double min() const
    {
        return gain() * mSampleMin;
    }
    
    double max() const
    {
        return gain() * mSampleMax;
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

    bool isOperator(const QString & arg) const
    {
        return (mOperator == arg);
    }

    void minus(const DataFile & other)
    {
        std::vector<int> result;
        mDelay = 0;
        Second time = 0;
        size_t index = 0;

        while (time < duration())
        {
            const double a = at(time);
            const double b = other.at(time);

            ++index;
            time = static_cast<Second>(index) / sps();

            if (isnan(a) || (isnan(b)))
            {
                if (result.size() > 0) {break;}
                mDelay = time;
                continue;
            }

            const int lsb = static_cast<int>((a - b) / gain());
            result.push_back(lsb);
        }

        mSamples = result;
        updateMinMax();
    }
private:
    double at(Second sec) const
    {
        const size_t index = static_cast<size_t>((sec - delay()) * sps());
        return (index < samples().size())
            ? (gain() * samples()[index])
            : NAN;
    }

    void setSamples(const QString & path)
    {
        const QString fullName = path + mFileName;
        QFile read(fullName);

        if (!read.open(QIODevice::ReadOnly))
        {
            error();
            return;
        }

        const size_t size = static_cast<size_t>(QFileInfo(fullName).size());
        mSamples.reserve(size / sizeof(qint16));
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

            mSamples.push_back(lsb);
        }

        read.close();
        updateMinMax();
    }

    void updateMinMax()
    {
        if (samples().size() < 1)
        {
            mSampleMin = 0;
            mSampleMax = 0;
        }
        else
        {
            auto minmax = std::minmax_element(samples().begin(), samples().end());
            mSampleMin = *minmax.first;
            mSampleMax = *minmax.second;
        }
    }

    void parse()
    {
        if (!QRegularExpression("^[>+-]").match(mTxt).hasMatch())
        {
            mTxt = "> " + mTxt;
        }

        double div = 1.0;
        int optPos = -1;
        bool valid = true;
        QRegularExpression re("^([>+-])\\s*(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\S+)\\s*");
        QRegularExpressionMatch match = re.match(mTxt);

        if (match.hasMatch())
        {
            int sps = 0;
            mOperator = match.captured(1);
            mFileName = match.captured(2);
            if (valid) {sps = match.captured(3).toInt(&valid);}
            if (valid) {div = match.captured(4).toDouble(&valid);}
            mUnit = match.captured(5);
            mLabel = match.captured(6);
            optPos = match.capturedEnd(0);

            if (sps == 0)
            {
                error();
                return;
            }

            mSps = sps;
        }
        else
        {
            error();
            return;
        }

        int delay = 0;
        QString dst;
        if (valid && find(dst, optPos, "s-mask")) {mSampleMask = dst.toInt(&valid, 0);}
        if (valid && find(dst, optPos, "offset")) {mSampleOffset = dst.toInt(&valid, 0);}
        if (valid && find(dst, optPos, "delay"))  {delay = dst.toInt(&valid, 0);}
        if (valid && find(dst, optPos, "gain"))   {mGain = dst.toDouble(&valid) / div;}
        if (!valid) {error();}
        mDelay = static_cast<double>(delay) / 1000;

        if (mSampleMask == 0x3fff)
        {
            // Many info files do not contain any of the u16/i16 keywords. Thus we are
            // guessing and testing for other typical properties of unsigned ecg samples.
            mIsSigned = ((mSampleOffset != 0x1fff) && (mSampleOffset != 0x2000));
        }

        // Hint: Avoid these keywords. They describe only a part of the data.
        if (contains(optPos, "swab"))  {mIsBigEndian = false;}
        if (contains(optPos, "u16"))   {mIsSigned = false;}
        if (contains(optPos, "i16"))   {mIsSigned = true;}

        // Hint: Use these keywords instead: They fully describe the data.
        if (contains(optPos, "beu16")) {mIsSigned = false; mIsBigEndian = true;}
        if (contains(optPos, "leu16")) {mIsSigned = false; mIsBigEndian = false;}
        if (contains(optPos, "bei16")) {mIsSigned = true;  mIsBigEndian = true;}
        if (contains(optPos, "lei16")) {mIsSigned = true;  mIsBigEndian = false;}
    }

    bool contains(int startPos, const QString & key) const
    {
        QRegularExpression re(QString("\\b%1\\b").arg(key));
        return re.match(mTxt, startPos).hasMatch();
    }

    bool find(QString & dst, int startPos, const QString & key) const
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
    double mMin;
    double mMax;
    Second mDuration;
    std::vector<DataFile> mFiles;
public:
    void plus(DataFile & file)
    {
        mFiles.push_back(file);
    }

    void minus(DataFile & file)
    {
        if (files().size() < 1) return;
        const size_t index = files().size() - 1;
        mFiles[index].minus(file);
    }

    void done()
    {
        if (files().size() < 1)
        {
            mMin = 0;
            mMax = 0;
            mDuration = 0;
            return;
        }

        mMin = files()[0].min();
        mMax = files()[0].max();
        mDuration = files()[0].duration();

        for (auto & file:files())
        {
            if (mMin > file.min()) {mMin = file.min();}
            if (mMax < file.max()) {mMax = file.max();}
            if (mDuration < file.duration()) {mDuration = file.duration();}
        }
    }
    
    const std::vector<DataFile> & files() const
    {
       return mFiles;
    }

    double min() const {return mMin;}
    double max() const {return mMax;}
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

    void setPixelSize(int px)
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
        setFocus(fromPixel(px));
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
        return mX.toPixel(idx / mSps + mDelay);
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
    explicit DrawChannel(const DataChannel & data,
            const UnitScale & timeScale,
            const UnitScale & valueScale);
    void Draw(QWidget & parent, const QRect & rect);
private:
    void DrawAnnotations(QPainter & painter, const QRect & rect);
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
    QFont font = parent.font();
    if (font.pointSize() > 8) {font.setPointSize(8);}

    QPainter painter(&parent);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(mLinePen);
    painter.setFont(font);
    painter.fillRect(rect, Qt::white);

    DrawSamples(painter, rect);
    DrawAnnotations(painter, rect);
}

void DrawChannel::DrawAnnotations(QPainter & painter, const QRect & )
{
    painter.drawText(20, 10, "Test");
}

void DrawChannel::DrawSamples(QPainter & painter, const QRect & rect)
{
    for (auto & data:mData.files())
    {
        if (data.samples().size() < 2)
        {
            continue;
        }

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
    const int indexEnd = static_cast<int>(data.samples().size()) - 1;
    const int xpxEnd = rect.right() + 2;
        
    for (int xpx = rect.left(); xpx < xpxEnd; ++xpx)
    {
        int indexFirst = mTranslate.xpxToSampleIndex(xpx);
        if (indexFirst < 1) indexFirst = 1;
        if (indexFirst > indexEnd) return;

        int indexLast = mTranslate.xpxToSampleIndex(xpx + 1);
        if (indexLast > indexEnd) indexLast = indexEnd;
        if (indexLast < 1) continue;

        // 1st line per xpx:
        // - from last sample in previous xpx
        // - to first sample in current xpx
        auto itFirst = data.samples().begin() + indexFirst;
        auto first = mTranslate.lsbToYpx(*itFirst);
        auto last = mTranslate.lsbToYpx(*(itFirst - 1));
        painter.drawLine(xpx - 1, last, xpx, first);

        // 2nd line per xpx:
        // - from min sample in current xpx
        // - to max sample in current xpx
        auto itLast = itFirst + (indexLast - indexFirst);
        auto minmax = std::minmax_element(itFirst, itLast);
        auto min = mTranslate.lsbToYpx(*minmax.first);
        auto max = mTranslate.lsbToYpx(*minmax.second);
        painter.drawLine(xpx, min, xpx, max);
    }
}

void DrawChannel::DrawSampleWise(QPainter & painter, const QRect & rect, const DataFile & data)
{
    const int indexFirst = mTranslate.xpxToSampleIndex(rect.left());
    const int indexLast = mTranslate.xpxToSampleIndex(rect.right() + 2);
    const int indexBegin = (indexFirst < 0) ? 0 : indexFirst;
    const int indexMax = static_cast<int>(data.samples().size());
    const int indexEnd = (indexLast > indexMax) ? indexMax : indexLast;
    if ((indexEnd - indexBegin) < 2) return;

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
        painter.setPen(mPointPen);
        painter.drawPoint(xold, yold);
        mLinePen.setColor(Qt::gray);
        painter.setPen(mLinePen);
    }

    while (now < end)
    {
        auto ynow = mTranslate.lsbToYpx(*now);
        auto xnow = mTranslate.sampleIndexToXpx(indexNow);
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
        mTimeScale.setPixelSize(width());
        mTimeScale.setData(0, seconds);
        
        mValueScale.setYResolution();
        mValueScale.setPixelSize(height());
        mValueScale.setData(data.min(), data.max());

        setFocusPolicy(Qt::StrongFocus);
    }

    QString ValueUnit() const
    {
        if (mData.files().size() < 1) return "";
        return mData.files()[0].unit();
    }

    QString FormatValue(double value) const
    {
        QString result;
        QTextStream ss(&result);
        ss << value << ValueUnit();
        return result;
    }

    QString FormatTime(double time) const
    {
        double abs = std::fabs(time);
        QString result;
        QTextStream ss(&result);

        if (abs < 1.0)
        {
            ss << time * 1000.0 << "ms";
        }
        else if (abs > 3600.0)
        {
            ss << time / 3600.0 << "h";
        }
        else if (abs > 60.0)
        {
            ss << time / 60.0 << "min";
        }
        else
        {
            ss << time << "s";
        }

        return result;
    }

    QString status(const GuiMeasure & measure) const
    {
        QString result;
        QTextStream txt(&result);
        txt << "F={"
            << FormatTime(mTimeScale.focus()) << " "
            << FormatValue(mValueScale.focus()) << "} "
            << "} M={"
            << FormatTime(mTimeScale.pixelToUnit(measure.width()))
            << FormatValue(mValueScale.pixelToUnit(measure.height()))
            << "} Z={"
            << mTimeScale.mmPerUnit() << "mm/s, "
            << mValueScale.mmPerUnit() << "mm/" << ValueUnit()
            << "}";
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
        mValueScale.setPixelSize(height());
        mTimeScale.setPixelSize(width());
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

    void slotMeasureMoved()
    {
        const QPoint focus = mMeasure->geometry().center();
        for (auto & chan:mChannels) {chan->setXFocus(focus.x());}
        if (mSelected) {mSelected->setYFocus(focus.y());}
        updateStatus();
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
    }

    void xzoomIn()  {for (auto & chan:mChannels) {chan->xzoomIn(); }; updateStatus();}
    void xzoomOut() {for (auto & chan:mChannels) {chan->xzoomOut();}; updateStatus();}
    void left()     {for (auto & chan:mChannels) {chan->left();    }; updateStatus();}
    void right()    {for (auto & chan:mChannels) {chan->right();   }; updateStatus();}
    void yzoomIn()  {if (mSelected) {mSelected->yzoomIn(); }; updateStatus();}
    void yzoomOut() {if (mSelected) {mSelected->yzoomOut();}; updateStatus();}
    void up()       {if (mSelected) {mSelected->up();      }; updateStatus();}
    void down()     {if (mSelected) {mSelected->down();    }; updateStatus();}
private:
    void updateStatus()
    {
        if (mSelected && mMeasure)
        {
            mStatus->showMessage(mSelected->status(*mMeasure));
        }
        else
        {
            mStatus->clearMessage();
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
            geo.setSize(QSize(50, 50));
            geo.moveCenter(wave->rect().center());
        }

        delete mMeasure;
        GuiMeasure * gui = new GuiMeasure(wave);
        gui->setGeometry(geo);
        gui->setMinimumSize(30, 30);
        gui->show();
        connect(gui, SIGNAL(signalMoved()), this, SLOT(slotMeasureMoved()));

        mSelected = wave;
        mMeasure = gui;
    }

    void resizeEvent(QResizeEvent *) override
    {
        if (mMeasure && mSelected)
        {
            QRect geo;
            geo.setSize(QSize(50, 50));
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

TEST(DataFile, parse)
{
    EXPECT_TRUE(DataFile("dummy 500 1 mV X", "").valid());
    EXPECT_TRUE(DataFile("dummy 500 1.0 mV X", "").valid());
    EXPECT_FALSE(DataFile("dummy 500 1 mV ", "").valid()); // label missing
    EXPECT_FALSE(DataFile("dummy 500 D mV X", "").valid()); // divisor wrong
    EXPECT_FALSE(DataFile("dummy 500.0 1 mV X", "").valid()); // sps wrong
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
    x.setPixelSize(420);
    x.setData(0, 4);
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
    y.setPixelSize(500);
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


