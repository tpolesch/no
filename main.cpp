
#include "MainWindow.h"
#include <QtGui>

class SingleWave
{
public:
    typedef QList<unsigned int> List;
private:
    List list;
    int error;
public:
    SingleWave():
        list(),
        error(0)
    {
    }

    bool isValid() const
    {
        return (error == 0);
    }

    void buildTestSignal()
    {
        const int sampleCount = 10 * sps();
        int value = lsb();

        for (int index = 0; index < sampleCount; ++index)
        {
            if ((index % sps()) == 0)
            {
                value -= lsb();
            }
            
            list.append(static_cast<unsigned int>(offset() + value));
        }
    }

    void parse(const QString & name)
    {
        qDebug("SingleWave::parse(%s)", qPrintable(name));
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
            list.append(static_cast<unsigned int>(sample));
        }
            
        qDebug("... done with %d samples", count);
    }

    const List & raw() const {return list;}
    unsigned int mask() const {return 0x3fffu;}
    int offset() const {return 0x2000;}
    int sps() const {return 500;}
    int speed() const {return 25;} // mm/s
    int gain() const {return 10;} // mm/mV
    int lsb() const {return 200;} // LSB/mV
};

class WaveList
{
public:
    typedef QList<SingleWave> List;
private:
    List list;
public:
    WaveList() : list()
    {
    }

    void addTestSignal()
    {
        SingleWave wave;
        wave.buildTestSignal();
        list.append(wave);
    }

    void init(const QStringList & args)
    {
        for (int index = 1; index < args.count(); ++index)
        {
            SingleWave wave;
            wave.parse(args[index]);

            if (wave.isValid())
            {
                list.append(wave);
            }
        }
    }

    const List & read() const {return list;}
};

class PixelScaling
{
private:
    static const int zoomDenominator = 10;
    int zoomNumerator;
    int xmm;
    int xpx;
    int ymm;
    int ypx;
public:
    explicit PixelScaling(int zoomNum = zoomDenominator):
        zoomNumerator(zoomNum)
    {
        const QDesktopWidget desk;
        xmm = desk.widthMM();
        ymm = desk.heightMM();
        xpx = desk.width();
        ypx = desk.height();
    }

    int MilliMeterAsXPixel(int numerator, int denominator) const
    {
        qint64 result = numerator;
        result *= zoomNumerator;
        result *= xpx;
        result /= xmm;
        result /= denominator;
        result /= zoomDenominator;
        return static_cast<int>(result);
    }

    int MilliMeterAsYPixel(int numerator, int denominator) const
    {
        qint64 result = numerator;
        result *= ypx;
        result /= ymm;
        result /= denominator;
        return static_cast<int>(result);
    }
};

class DrawGrid
{
private:
    const PixelScaling pixelScaling;
public:
    DrawGrid() : pixelScaling()
    {
    }

    void draw(QWidget * parent)
    {
        drawTimeGrid(parent);
        drawValueGrid(parent);
    }
private:
    void drawTimeGrid(QWidget * parent)
    {
        const int max = parent->widthMM();
        const int height = parent->height();
        QPen pen(Qt::red, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        QPainter painter(parent);
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (int mm = 0; mm < max; mm += 5)
        {
            const int xpx = pixelScaling.MilliMeterAsXPixel(mm, 1);
            pen.setStyle(((mm % 25) == 0) ? Qt::SolidLine : Qt::DotLine);
            painter.setPen(pen);
            painter.drawLine(xpx, 0, xpx, height);
        }
    }

    void drawValueGrid(QWidget * parent)
    {
        const int max = parent->heightMM();
        const int width = parent->width();
        QPainter painter(parent);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(Qt::red, 1, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin));

        for (int mm = 0; mm < max; mm += 5)
        {
            const int ypx = pixelScaling.MilliMeterAsYPixel(mm, 1);
            painter.drawLine(0, ypx, width, ypx);
        }
    }
};

class DrawChannel
{
private:
    const PixelScaling pixelScaling;
    const SingleWave * singleWave;
    int samplePosition;
    int channelOffset;

    bool reading() const
    {
        return (samplePosition < data().raw().count());
    }

    const SingleWave & data() const
    {
        return *singleWave;
    }

    int YPixel(int pos) const
    {
        const unsigned int rawValue = ((data().raw()[pos]) & data().mask());
        const int value = rawValue - data().offset();
        // Example: (100LSB * 10mm/mV) / 200LSB/mV = 5mm
        return pixelScaling.MilliMeterAsYPixel((value * data().gain()), data().lsb());
    }

    int XPixel(int pos) const
    {
        // Example: (200 * 25mm/sec) / 500samples/sec = 10mm
        return pixelScaling.MilliMeterAsXPixel((pos * data().speed()), data().sps());
    }

    QPoint read()
    {
        const int xpx = XPixel(samplePosition);
        const int ypx = YPixel(samplePosition);
        ++samplePosition;
        return QPoint(xpx, (channelOffset - ypx));
    }
public:
    explicit DrawChannel(const SingleWave & arg, int zoom):
        pixelScaling(zoom),
        singleWave(&arg),
        samplePosition(0),
        channelOffset(0)
    {
    }

    int minimumWidth() const
    {
        return XPixel(data().raw().count());
    }

    void draw(QWidget * parent, int offset)
    {
        QPainter samplePainter(parent);
        samplePainter.setRenderHint(QPainter::Antialiasing, true);
        samplePainter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        samplePosition = 0;
        channelOffset = offset;
        QPoint from = read();

        while (reading())
        {
            const QPoint to = read();
            samplePainter.drawLine(from, to);
            from = to;
        }
    }
};

class WaveView : public QWidget
{
private:
    const WaveList * waveList;
    int zoomFactor;

    const WaveList::List & list() const
    {
        return waveList->read();
    }

    void initSize()
    {
        int minimumWidth = 0;

        for (int index = 0; index < list().count(); ++index)
        {
            DrawChannel channel(list()[index], zoomFactor);
            const int channelWidth = channel.minimumWidth();

            if (minimumWidth < channelWidth)
            {
                minimumWidth = channelWidth;
            }

        }

        const int width = minimumWidth + 10;
        setMinimumWidth(width);
        resize(width, height());

        qDebug("WaveView::initSize() width = %d", width);
    }

    void buildZoom()
    {
        initSize();
        update();
    }
public:
    WaveView():
        QWidget(),
        waveList(NULL),
        zoomFactor(10)
    {
    }

    void zoomIn()
    {
        zoomFactor *= 2;
        buildZoom();
    }

    void zoomOut()
    {
        if (zoomFactor > 1)
        {
            zoomFactor /= 2;
            buildZoom();
        }
    }

    void init(const WaveList & arg)
    {
        waveList = &arg;
        initSize();
    }
protected:
    void paintEvent(QPaintEvent *)
    {
        DrawGrid grid;
        grid.draw(this);

        const int count = list().count();
        const int channelHeight = height() / count;

        for (int index = 0; index < count; ++index)
        {
            DrawChannel channel(list()[index], zoomFactor);
            const int channelOffset = (index * channelHeight) + (channelHeight / 2);
            channel.draw(this, channelOffset);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
// class MainWindow
////////////////////////////////////////////////////////////////////////////////

MainWindow::MainWindow()
{
    waveView = new WaveView();
    waveView->setBackgroundRole(QPalette::Base);
    waveView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    scrollArea = new QScrollArea();
    scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(waveView);

    mainWindow.setCentralWidget(scrollArea);
    mainWindow.setWindowTitle(QString("no"));
    mainWindow.resize(600, 300);

    zoomInAction = new QAction(QString("Zoom &In"), this);
    zoomInAction->setShortcut(QString("."));
    zoomInAction->setEnabled(false);
    connect(zoomInAction, SIGNAL(triggered()), this, SLOT(zoomIn()));

    zoomOutAction = new QAction(QString("Zoom &Out"), this);
    zoomOutAction->setShortcut(QString(","));
    zoomOutAction->setEnabled(false);
    connect(zoomOutAction, SIGNAL(triggered()), this, SLOT(zoomOut()));

    viewMenu = new QMenu(QString("&View"), &mainWindow);
    viewMenu->addAction(zoomInAction);
    viewMenu->addAction(zoomOutAction);
    viewMenu->addSeparator();

    mainWindow.menuBar()->addMenu(viewMenu);
}

void MainWindow::init(const WaveList & list)
{
    waveView->init(list);
    updateActions();
    mainWindow.show();
}

void MainWindow::zoomIn()
{
    qDebug("MainWindow::zoomIn()");
    waveView->zoomIn();
    scrollArea->update();
    updateActions();
}

void MainWindow::zoomOut()
{
    qDebug("MainWindow::zoomOut()");
    waveView->zoomOut();
    scrollArea->update();
    updateActions();
}

void MainWindow::updateView()
{
}

void MainWindow::updateActions()
{
    zoomInAction->setEnabled(true);
    zoomOutAction->setEnabled(true);
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

    WaveList waveList;
    // waveList.addTestSignal();
    waveList.init(args);

    MainWindow window;
    window.init(waveList);
    return app.exec();
}

