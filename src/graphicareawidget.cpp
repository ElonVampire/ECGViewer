#include "graphicareawidget.h"
#include "leastsquaremethod.h"
#include <QPainter>
#include <math.h>
#include <QMouseEvent>
#include <QDateTime>

GraphicAreaWidget::GraphicAreaWidget(QWidget *parent) : QWidget(parent) {
    mScalingFactor = 1000.0;
    mSweepFactor = 30.0;
    mScroll = 0.0;
    mpEDFHeader = nullptr;
    setMouseTracking(true);
    mRepaint = false;
    mChannelECG = 1;
    mChannelPlethism = 0;
    startTimer(50);

    mBeginPercent = 0;
    mEndPercent = 50;
    mN = 0;
    mMouseX = 0;
    mMouseY = 0;
    setMouseTracking(true);
}

void GraphicAreaWidget::setEDFHeader(edf_hdr_struct *pEDFHeader) {
    mpEDFHeader = pEDFHeader;
    mChannels.resize(mpEDFHeader->edfsignals);
    for (int i = 0; i < mChannels.size(); i++) {
        if (mChannels[i].scalingFactor == 0) {
            mChannels[i].scalingFactor = mScalingFactor;
        }
    }
    mRepaint = true;
}

void GraphicAreaWidget::setData(qint32 channelIndex, QByteArray doubleSamples)
{
    if (channelIndex < mChannels.size()) {
        mChannels[channelIndex].index = channelIndex;
        mChannels[channelIndex].samples = doubleSamples;
        double * pData = (double *) mChannels[channelIndex].samples.data();
        qint32 samplesCountAll = mChannels[channelIndex].samples.size() / sizeof(double);
        for (int i = 0; i < samplesCountAll; i++, pData++) {
            if (i == 0 || mChannels[channelIndex].minValue > *pData) {
                mChannels[channelIndex].minValue = *pData;
            }
            if (i == 0 || mChannels[channelIndex].maxValue < *pData) {
                mChannels[channelIndex].maxValue = *pData;
            }
        }
        mChannels[channelIndex].heartRate.resize(sizeof(int) * samplesCountAll);
        mChannels[channelIndex].timeLag.resize(sizeof(double) * samplesCountAll);
        mChannels[channelIndex].minimums.resize(sizeof(double) * samplesCountAll);
        mChannels[channelIndex].maximums.resize(sizeof(double) * samplesCountAll);
        mChannels[channelIndex].minimumsCalculated.resize(sizeof(double) * samplesCountAll);
        mChannels[channelIndex].maximumsCalculated.resize(sizeof(double) * samplesCountAll);

        mChannels[channelIndex].heartRate.fill(0);
        mChannels[channelIndex].timeLag.fill(0);
        mChannels[channelIndex].minimums.fill(0);
        mChannels[channelIndex].maximums.fill(0);
        mChannels[channelIndex].minimumsCalculated.fill(0);
        mChannels[channelIndex].maximumsCalculated.fill(0);
    }
}

void GraphicAreaWidget::setScalingFactor(qreal scalingFactor)
{
    mScalingFactor = scalingFactor;
    for (int i = 0; i < mChannels.size(); i++) {
        mChannels[i].scalingFactor = mScalingFactor;
    }
    mRepaint = true;
}

void GraphicAreaWidget::setSweepFactor(qreal sweepFactor)
{
    mSweepFactor = sweepFactor;
    mRepaint = true;
}

void GraphicAreaWidget::setScroll(qreal part)
{
    mScroll = part;
    mRepaint = true;
}

void GraphicAreaWidget::calc(int channelECG, int channelP, int channelABP)
{
    for (qint32 channel = 0; channel < qint32(mpEDFHeader->edfsignals); channel++) {
        mChannels[channel].heartRate.fill(0);
        mChannels[channel].timeLag.fill(0);
        mChannels[channel].minimums.fill(0);
        mChannels[channel].maximums.fill(0);
        mChannels[channel].minimumsCalculated.fill(0);
        mChannels[channel].maximumsCalculated.fill(0);
    }

    findHeartRate((double *)mChannels[channelECG].samples.data(),
              (int *)mChannels[channelECG].heartRate.data(),
              mChannels[channelECG].samples.size() / sizeof(double),
              getSampleRate(channelECG),
              1);

    findHeartRate((double *)mChannels[channelP].samples.data(),
              (int *)mChannels[channelP].heartRate.data(),
              mChannels[channelP].samples.size() / sizeof(double),
              getSampleRate(channelP),
              1);

    // ABP макс
    double *pIn, *pMax, *pMin, *pInBase, *pMinBase, *pMaxBase;
    int *pHRate, *pHRateBase;

    int samplesCount = mChannels[channelABP].samples.size() / sizeof(double);

    pInBase = (double *)mChannels[channelABP].samples.data();
    pMinBase = (double *)mChannels[channelABP].minimums.data();
    pMaxBase = (double *)mChannels[channelABP].maximums.data();
    pHRateBase = (int *)mChannels[channelABP].heartRate.data();

    pIn = pInBase;
    pMax = pMaxBase;
    pHRate = pHRateBase;

    findHeartRate(pIn, pHRate, samplesCount, getSampleRate(channelABP), 1);

    for (int sampleIndex = 0; sampleIndex < samplesCount; sampleIndex++, pIn++, pMax++, pHRate++) {
        if (*pHRate != 0) {
           *pMax = *pIn;
        }
    }
    // ABP мин
    pIn = pInBase;
    pMin = pMinBase;
    pMax = pMaxBase;
    pHRate = pHRateBase;

    findHeartRate(pIn, pHRate, samplesCount, getSampleRate(channelABP), -1);

    int minIndex = -1, maxIndex;
    double minValue = 1e10, maxValue;

    // оставим минимумы только между двумя соседними максимумами)
    for (int sampleIndex = 0; sampleIndex < samplesCount; sampleIndex++, pIn++, pMin++, pMax++, pHRate++) {
        if (*pHRate != 0 && minValue > *pIn) {
           minValue = *pIn;
           minIndex = sampleIndex;
        }
        if (*pMax != 0) {
            if (minIndex != -1) {
                pMinBase[minIndex] = pInBase[minIndex];
            }
            minIndex = -1;
            minValue = 1e10;
        }
    }

    mChannels[channelABP].heartRate.fill(0);

    findTimeLag((int *)mChannels[channelECG].heartRate.data(),
                mChannels[channelECG].heartRate.size() / sizeof(int),
                getSampleRate(channelECG),
                (int *)mChannels[channelP].heartRate.data(),
                (double *)mChannels[channelP].timeLag.data(),
                mChannels[channelP].heartRate.size() / sizeof(int),
                getSampleRate(channelP));

    // массив давлений и задержек
    mDelayAndPressureList.clear();
    pMin = pMinBase;
    pMax = pMaxBase;
    double * pDelayMs = (double *)mChannels[channelP].timeLag.data();

    LeastSquareMethod lsmLo;
    LeastSquareMethod lsmHi;

    DelayAndPressure item;
    memset(&item, 0, sizeof(item));
    printf("Delay Low High%\n");
    int plethSampleIndex = 0;
    int plethSamples = mChannels[channelP].timeLag.size() / sizeof(double);
    for (int sampleIndex = 0; sampleIndex < samplesCount; sampleIndex++, pMin++, pMax++) {

        if (sampleIndex < mBeginPercent * samplesCount / 100) continue;
        if (sampleIndex > mEndPercent * samplesCount / 100) continue;

        if (*pMin != 0) item.minPressureMm = *pMin;
        if (*pMax != 0) item.maxPressureMm = *pMax;

        while (plethSampleIndex < plethSamples && (double(plethSampleIndex) * getSampleRate(channelABP)) < (double(sampleIndex) * getSampleRate(channelP))) {
            if (*pDelayMs != 0) item.delayS = *pDelayMs;
            pDelayMs++;
            plethSampleIndex++;
        }

        if (item.delayS != 0) {
            if (item.maxPressureMm != 0 && item.minPressureMm !=0) {
                mDelayAndPressureList.append(item);
                printf("%.4lf %.2lf %.2lf\n", item.delayS, item.minPressureMm, item.maxPressureMm);
                lsmLo.add(item.delayS, item.minPressureMm);
                lsmHi.add(item.delayS, item.maxPressureMm);
                memset(&item, 0, sizeof(item));
            }
        }
    }

    lsmLo.calc();
    lsmHi.calc();

    mAHi = lsmHi.getA();
    mBHi = lsmHi.getB();

    mALo = lsmLo.getA();
    mBLo = lsmLo.getB();

    mN = lsmLo.getN();

    printf("Lo = %.2lf * T + %.2lf\n", mALo, mBLo);
    printf("Hi = %.2lf * T + %.2lf\n", mAHi, mBHi);

    double * pMinCalculated = (double *)mChannels[channelABP].minimumsCalculated.data();
    double * pMaxCalculated = (double *)mChannels[channelABP].maximumsCalculated.data();

    pMin = pMinBase;
    pMax = pMaxBase;
    pDelayMs = (double *)mChannels[channelP].timeLag.data();
    plethSampleIndex = 0;
    minIndex = 0;
    maxIndex = 0;
    double delay = 0;
    for (int sampleIndex = 0; sampleIndex < samplesCount; sampleIndex++, pMin++, pMax++) {
        if (*pMin != 0) {
            minIndex = sampleIndex;
        }
        if (*pMax != 0) {
            maxIndex = sampleIndex;
        }

        while (plethSampleIndex < plethSamples && (double(plethSampleIndex) * getSampleRate(channelABP)) < (double(sampleIndex) * getSampleRate(channelP))) {
            if (*pDelayMs != 0) delay = *pDelayMs;
            pDelayMs++;
            plethSampleIndex++;
        }

        if (delay != 0) {
            // только точки за пределами области, в которой производилась оценка
            if (sampleIndex < mBeginPercent * samplesCount / 100 || sampleIndex > mEndPercent * samplesCount / 100)
            if (minIndex != 0 && maxIndex !=0) {
                minValue = delay * mALo + mBLo;
                maxValue = delay * mAHi + mBHi;

                pMinCalculated[minIndex] = minValue;
                pMaxCalculated[maxIndex] = maxValue;

                delay = 0;
                maxIndex = 0;
                minIndex = 0;
            }
        }
    }

    mRepaint = true;
}

void GraphicAreaWidget::setPressureCalcPercent(int beginPercent, int endPercent)
{
    mBeginPercent = beginPercent;
    mEndPercent = endPercent;
}

void GraphicAreaWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);

    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    painter.setBrush(QBrush(Qt::white, Qt::SolidPattern));
    painter.drawRect(0, 0, width(), height());

    if (mpEDFHeader == nullptr || mpEDFHeader->edfsignals <= 0) {
        // ничего не делаем
        return;
    }

    qreal magicPowerScaler = 5.0;
    qreal magicTimeScaler = 0.25;

    painter.setPen(Qt::black);

    qint32 screenHeight = height();
    qint32 screenWidth = width();
    qint32 channelHeight = screenHeight / mpEDFHeader->edfsignals;

    QBrush brush(Qt::red, Qt::SolidPattern);
    painter.setBrush(brush);

    double pressureLo = 0;
    double pressureHi = 0;
    double pressureLoCalc = 0;
    double pressureHiCalc = 0;
    int pressureLoCount = 0;
    int pressureHiCount = 0;

    QString mouseChannelName = "";
    QString mouseTime = "";
    QString mouseValue = "";

    for (qint32 channel = 0; channel < qint32(mpEDFHeader->edfsignals); channel++)
    {
        qint32 startY = channelHeight / 2 + channelHeight * channel;

        bool mouseInChannel = mMouseY >= startY - channelHeight/2 && mMouseY < startY + channelHeight/2;

        if (mouseInChannel) {
            mouseChannelName = mpEDFHeader->signalparam[channel].label;
        }

        if (channel < mChannels.size() && mChannels[channel].samples.size() > 0)
        {
            qreal scale = magicPowerScaler * qreal(mpEDFHeader->signalparam[channel].dig_max - mpEDFHeader->signalparam[channel].dig_min) /
                    ((mpEDFHeader->signalparam[channel].phys_max - mpEDFHeader->signalparam[channel].phys_min) * mChannels[channel].scalingFactor);

            double * pData = (double *) mChannels[channel].samples.data();
            int * pPeaks = (int *) mChannels[channel].heartRate.data();
            double * pMins = (double *) mChannels[channel].minimums.data();
            double * pMaxs = (double *) mChannels[channel].maximums.data();
            double * pMinsCalc = (double *) mChannels[channel].minimumsCalculated.data();
            double * pMaxsCalc = (double *) mChannels[channel].maximumsCalculated.data();
            double * pTimeLag = (double *) mChannels[channel].timeLag.data();
            qint32 samplesCountAll = mChannels[channel].samples.size() / sizeof(double);
            qint32 samplesViewPort = qreal(screenWidth) * getSampleRate(channel) * magicTimeScaler / mSweepFactor;
            qreal meanValue = (mChannels[channel].maxValue + mChannels[channel].minValue) * 0.5;

            if (samplesViewPort < 1) {
                samplesViewPort = 1;
            }

            qint32 startSampleIndex = qint32(mScroll * qreal(samplesCountAll));
            if (startSampleIndex >= samplesCountAll - 1) {
                startSampleIndex = samplesCountAll - 2;
            }

            qint32 endSampleIndex = startSampleIndex + samplesViewPort;

            if (endSampleIndex >= samplesCountAll) {
                endSampleIndex = samplesCountAll - 1;
            }

            if (channel == 0) {
                // time rulers
                QPen pen;
                pen.setStyle(Qt::DotLine);
                pen.setColor(Qt::gray);
                QFont font = painter.font();
                font.setPointSize(7);
                painter.setFont(font);
                painter.setPen(pen);

                QDateTime startDateTime(QDate(mpEDFHeader->startdate_year, mpEDFHeader->startdate_month, mpEDFHeader->startdate_day),
                                    QTime(mpEDFHeader->starttime_hour, mpEDFHeader->starttime_minute, mpEDFHeader->starttime_second));
                QDateTime correctedDateTime = startDateTime.addMSecs(mpEDFHeader->starttime_subsecond / 10000);
                double leftMs = 1000.0 * double(startSampleIndex) / getSampleRate(0);
                QDateTime left = correctedDateTime.addMSecs(qint64(leftMs));

                double pixelsPerSecond = double(screenWidth) * getSampleRate(0) / double(samplesViewPort) / 10.0;
                int minPixelsPerTick = 50;
                int interval = 1;

                int dividers[4] = {2,3,2,5};
                int dividerIndex = 2;
                while (pixelsPerSecond < minPixelsPerTick) {
                    interval *= dividers[dividerIndex % 4];
                    pixelsPerSecond *= double(dividers[dividerIndex % 4]);
                    dividerIndex++;
                }

                int timePrev = 0;
                for (int x = 0; x < screenWidth; x++) {
                    double shiftMs = 1000.0 * double(samplesViewPort) * double(x) / double(screenWidth) / getSampleRate(0);
                    QDateTime now = left.addMSecs(qint64(shiftMs));

                    int time = (now.time().msecsSinceStartOfDay() / 100) / interval;

                    if (x > 0 && time != timePrev) {
                        painter.drawLine(x, 0, x, screenHeight);
                        painter.drawText(x, screenHeight - 5, now.toString("hh:mm:ss.zzz").left(10));
                    }
                    timePrev = time;
                }
                font.setPointSize(8);
                painter.setFont(font);
                painter.setPen(Qt::SolidLine);
            }

            if (mouseInChannel) {
                // time
                QDateTime startDateTime(QDate(mpEDFHeader->startdate_year, mpEDFHeader->startdate_month, mpEDFHeader->startdate_day),
                                    QTime(mpEDFHeader->starttime_hour, mpEDFHeader->starttime_minute, mpEDFHeader->starttime_second));
                QDateTime correctDateTime = startDateTime.addMSecs(mpEDFHeader->starttime_subsecond / 10000);
                int mouseSampleIndex = startSampleIndex + samplesViewPort * mMouseX / screenWidth;
                if (mouseSampleIndex >= samplesCountAll) {
                    mouseSampleIndex = samplesCountAll - 1;
                }
                qint64 shiftMs = qint64(1000.0 * double(mouseSampleIndex) / getSampleRate(channel));
                QDateTime mouseDateTime = correctDateTime.addMSecs(shiftMs);
                mouseTime = mouseDateTime.toString("hh:mm:ss.zzz");
                double value = *(pData + mouseSampleIndex);

                if (QString(mpEDFHeader->signalparam[channel].physdimension).contains("mm")) {
                    mouseValue = QString::asprintf("%.0lf %s", value, mpEDFHeader->signalparam[channel].physdimension);
                } else {
                    mouseValue = QString::asprintf("%lf %s", value, mpEDFHeader->signalparam[channel].physdimension);
                }
            }

            if (QString(mpEDFHeader->signalparam[channel].physdimension).contains("V") &&
                    !QString(mpEDFHeader->signalparam[channel].label).contains("PLET", Qt::CaseInsensitive) &&
                    !QString(mpEDFHeader->signalparam[channel].label).contains("RESP", Qt::CaseInsensitive))
            {
                QPen pen;
                pen.setStyle(Qt::DotLine);
                pen.setColor(Qt::gray);
                painter.setPen(pen);
                QFont font = painter.font();
                font.setPointSize(7);
                painter.setFont(font);
                int beginY = startY - channelHeight / 2;
                int endY = beginY + channelHeight;
                int minRowHeight = 20;
                for (int row = 0; row <= (endY - beginY) / minRowHeight; row++) {
                    int y = beginY + row * minRowHeight;
                    painter.drawLine(0, y, screenWidth, y);
                    if (y + 10 > endY) break;
                    double value = meanValue - double(y - startY) / scale;
                    painter.drawText(5, y+10,  QString::asprintf("%lf", value));
                }
                pen.setStyle(Qt::SolidLine);
                painter.setPen(pen);
                painter.drawLine(0, endY, screenWidth, endY);
                font.setPointSize(8);
                painter.setFont(font);
            }

            if (channel == mChannelECG) {
                painter.setPen(Qt::darkRed);
            } else if (channel == mChannelPlethism) {
                painter.setPen(Qt::darkGreen);
            } else {
                painter.setPen(Qt::black);
            }

            QPoint * points = nullptr;

            if (samplesViewPort > screenWidth) {
                QPoint * points = new QPoint[screenWidth*2];
                qint32 pointCount = 0;
                qint32 xPrev = 0;
                qint32 yMax = 0;
                qint32 yMin = screenHeight;
                double * pCurrentData = pData + startSampleIndex;
                int * pPeak = pPeaks + startSampleIndex;
                double * pMin = pMins + startSampleIndex;
                double * pMax = pMaxs + startSampleIndex;
                double * pMinCalc = pMinsCalc + startSampleIndex;
                double * pMaxCalc = pMaxsCalc + startSampleIndex;
                double * pLag = pTimeLag + startSampleIndex;
                for (qint32 sampleIndex = startSampleIndex; sampleIndex < endSampleIndex; sampleIndex++, pCurrentData++, pPeak++, pLag++, pMax++, pMin++, pMaxCalc++, pMinCalc++)
                {
                    qint32 x = screenWidth * (sampleIndex - startSampleIndex) / samplesViewPort;
                    if (x < 0) x = 0;
                    if (x > screenWidth - 1) x = screenWidth - 1;

                    qint32 y = startY - (*pCurrentData - meanValue) * scale;
                    if (y < 0) y = 0;
                    if (y > screenHeight - 1) y = screenHeight - 1;

                    if (yMin > y) yMin = y;
                    if (yMax < y) yMax = y;

                    if (x != xPrev) {
                        if (yMin == yMax) {
                            points[pointCount].setX(x);
                            points[pointCount].setY(yMin);
                            pointCount++;
                        } else {
                            points[pointCount].setX(x);
                            points[pointCount].setY(yMin);
                            pointCount++;

                            points[pointCount].setX(x);
                            points[pointCount].setY(yMax);
                            pointCount++;
                        }
                        xPrev = x;
                        yMax = 0;
                        yMin = screenHeight;
                    }
                    //
                    if (*pPeak > 0) {
                        QString text = QString::number(*pPeak);
                        if (*pLag > 0)
                        if (sampleIndex < mBeginPercent * samplesCountAll / 100 || sampleIndex > mEndPercent * samplesCountAll / 100) {
                            text = text + "/" + QString::number(int((*pLag)*1000.0)) + "ms";
                            double pHi = mAHi * (*pLag) + mBHi;
                            double pLo = mALo * (*pLag) + mBLo;
                            painter.drawText(x, y+20, QString::asprintf("Hi = %.2lf", pHi));
                            painter.drawText(x, y+30, QString::asprintf("Lo = %.2lf", pLo));
                        }
                        painter.drawEllipse(x-1, y-1, 4, 4);
                        painter.drawText(x, y, text);
                    }
                    if (*pMax != 0) {
                        painter.drawEllipse(x-1, y-1, 4, 4);
                        QString text = "h=" + QString::number(*pMax);
                        painter.drawText(x, y-15, text);
                        if (*pMaxCalc != 0) {
                            painter.drawText(x, y-5, QString::asprintf("e=%.1lf%%", 100.0 * fabs(*pMax - *pMaxCalc) / *pMax));
                            pressureHi += *pMax;
                            pressureHiCalc += *pMaxCalc;
                            pressureHiCount++;
                        }
                    }
                    if (*pMin != 0) {
                        painter.drawEllipse(x-1, y-1, 4, 4);
                        QString text = "l=" + QString::number(*pMin);
                        painter.drawText(x, y+10, text);
                        if (*pMinCalc != 0) {
                            painter.drawText(x, y+20, QString::asprintf("e=%.1lf%%", 100.0 * fabs(*pMin - *pMinCalc) / *pMin));                            
                            pressureLo += *pMin;
                            pressureLoCalc += *pMinCalc;
                            pressureLoCount++;
                        }
                    }
                }
                painter.drawPolyline(points, pointCount);
            } else {
                QPoint * points = new QPoint[endSampleIndex - startSampleIndex];
                int * pPeak = pPeaks + startSampleIndex;
                double * pMin = pMins + startSampleIndex;
                double * pMax = pMaxs + startSampleIndex;
                double * pMinCalc = pMinsCalc + startSampleIndex;
                double * pMaxCalc = pMaxsCalc + startSampleIndex;
                double * pLag = pTimeLag + startSampleIndex;
                for (qint32 sampleIndex = startSampleIndex; sampleIndex < endSampleIndex; sampleIndex++, pPeak++, pLag++, pMin++, pMax++, pMinCalc++, pMaxCalc++)
                {
                    qint32 x = (qint32)((qreal)screenWidth * (qreal)(sampleIndex - startSampleIndex) / (qreal)samplesViewPort);
                    if (x < 0) x = 0;
                    if (x > screenWidth - 1) x = screenWidth - 1;

                    qint32 y = startY - (qint32)((pData[sampleIndex] - meanValue) * scale);
                    if (y < 0) y = 0;
                    if (y > screenHeight - 1) y = screenHeight - 1;
                    points[sampleIndex - startSampleIndex].setX(x);
                    points[sampleIndex - startSampleIndex].setY(y);
                    //
                    if (*pPeak > 0) {
                        //
                        QString text = QString::number(*pPeak);
                        if (*pLag > 0)
                        if (sampleIndex < mBeginPercent * samplesCountAll / 100 || sampleIndex > mEndPercent * samplesCountAll / 100) {
                            text = text + "/" + QString::number(int((*pLag)*1000.0)) + "ms";

                            double pHi = mAHi * (*pLag) + mBHi;
                            double pLo = mALo * (*pLag) + mBLo;

                            painter.drawText(x, y+20, QString::asprintf("Hi = %.2lf", pHi));
                            painter.drawText(x, y+30, QString::asprintf("Lo = %.2lf", pLo));
                        }
                        painter.drawEllipse(x-1, y-1, 4, 4);
                        painter.drawText(x, y, text);

                    }
                    if (*pMax != 0) {
                        painter.drawEllipse(x-1, y-1, 4, 4);
                        QString text = "h=" + QString::number(*pMax);
                        painter.drawText(x, y-15, text);
                        if (*pMaxCalc != 0) {
                            painter.drawText(x, y-5, QString::asprintf("e=%.1lf%%", 100.0 * fabs(*pMax - *pMaxCalc) / *pMax));
                            pressureHi += *pMax;
                            pressureHiCalc += *pMaxCalc;
                            pressureHiCount++;
                        }
                    }
                    if (*pMin != 0) {
                        painter.drawEllipse(x-1, y-1, 4, 4);
                        QString text = "l=" + QString::number(*pMin);
                        painter.drawText(x, y+10, text);
                        if (*pMinCalc != 0) {
                            painter.drawText(x, y+20, QString::asprintf("e=%.1lf%%", 100.0 * fabs(*pMin - *pMinCalc) / *pMin));
                            pressureLo += *pMin;
                            pressureLoCalc += *pMinCalc;
                            pressureLoCount++;
                        }
                    }
                }
                painter.drawPolyline(points, endSampleIndex - startSampleIndex);
            }
            delete[] points;
        }
    }

    font.setPointSize(14);
    painter.setFont(font);

    painter.setPen(Qt::lightGray);
    painter.setBrush(QBrush(QColor(220,220,220,128), Qt::SolidPattern));
    painter.drawRect(screenWidth-250, 0, screenWidth, 120);
    painter.setPen(Qt::black);

    QString lo = QString::asprintf("Lo = %.2lf * t + %.2lf\n", mALo, mBLo);
    QString hi = QString::asprintf("Hi = %.2lf * t + %.2lf\n", mAHi, mBHi);
    QString n = QString::asprintf("N = %d\n", mN);

    painter.drawText(screenWidth - 250 + 10, 25, lo);
    painter.drawText(screenWidth - 250 + 10, 45, hi);
    painter.drawText(screenWidth - 250 + 10, 65, n);

    if (pressureLoCount > 0)
    {
        pressureLo /= double(pressureLoCount);
        pressureLoCalc /= double(pressureLoCount);
        double pressureError = 100.0 * fabs(pressureLo - pressureLoCalc) / pressureLo;
        QString textLo = QString::asprintf("Mean Lo = %.1lf, e=%.1lf%%", pressureLoCalc, pressureError);
        painter.drawText(screenWidth - 250 + 10, 85, textLo);
    }

    if (pressureHiCount > 0)
    {
        pressureHi /= double(pressureHiCount);
        pressureHiCalc /= double(pressureHiCount);
        double pressureError = 100.0 * fabs(pressureHi - pressureHiCalc) / pressureLo;
        QString textHi = QString::asprintf("Mean Hi = %.1lf, e=%.1lf%%", pressureHiCalc, pressureError);
        painter.drawText(screenWidth - 250 + 10, 105, textHi);
    }

    if (mouseValue != "" && mouseTime != "") {
        painter.setPen(Qt::lightGray);
        painter.setBrush(QBrush(QColor(220,220,220,128), Qt::SolidPattern));

        int mouseX = mMouseX + 10;
        int mouseY = mMouseY + 10;
        int mouseHintW = 140;
        int mouseHintH = 65;

        while (mouseX + mouseHintW > screenWidth - 1) mouseX--;
        while (mouseY + mouseHintH > screenHeight - 1) mouseY--;

        painter.drawRect(mouseX, mouseY, mouseHintW, mouseHintH);
        painter.setPen(Qt::black);
        painter.drawText(mouseX+5, mouseY+20, mouseChannelName);
        painter.drawText(mouseX+5, mouseY+40, mouseValue);
        painter.drawText(mouseX+5, mouseY+60, mouseTime);
    }
}

void GraphicAreaWidget::mouseMoveEvent(QMouseEvent *event) {
    mRepaint = true;
    mMouseX = event->pos().x();
    mMouseY = event->pos().y();
}

void GraphicAreaWidget::timerEvent(QTimerEvent *event)
{
    if (mRepaint) {
        repaint();
    }
}

void GraphicAreaWidget::findHeartRate(double *pInSamples, int *pHeartRate, int samplesCount, double sampleRate, int inversion)
{
    int maxInterval = int(sampleRate / (MIN_HEART_RATE / 60.));
    int minInterval = int(sampleRate / (MAX_HEART_RATE / 60.));
    printf("Finding peaks: start\n");
    memset(pHeartRate, 0, sizeof(int) * samplesCount);
    int windowSize = maxInterval;
    double * pSamples = new double[samplesCount];
    double * pWindow = new double[windowSize];
    double * pInData = pInSamples;
    double * pNormData = pSamples;
    memset(pWindow, 0, sizeof(int) * windowSize);
    int aboveMean = 0;
    // нормализация данных, приведение максимумов и минимумов
    for (int i = 0; i < samplesCount; i++, pInData++, pNormData++) {
        if (i < samplesCount - windowSize) {
            memcpy(pWindow, pInData, sizeof(double) * windowSize);
        }
        double minValue = 0.;
        double maxValue = 0.;
        for (int j = 0; j < windowSize; j++) {
            if (j == 0 || minValue > *(pWindow + j)) minValue = *(pWindow + j);
            if (j == 0 || maxValue < *(pWindow + j)) maxValue = *(pWindow + j);
        }
        if (maxValue == minValue) {
            *pNormData = 0;
        } else {
            *pNormData = (*pInData - minValue) / (maxValue - minValue);
        }
        if (*pNormData > 0.5) aboveMean++;
    }
    // проверки инверсии данных
    if (inversion < 0 || (inversion == 0 && aboveMean > samplesCount / 2)) {
        pNormData = pSamples;
        for (int i = 0; i < samplesCount; i++, pNormData++) {
            *pNormData = 1.0 - *pNormData;
        }
    }
    // поиск максимумов
    double barrier = 0.8;
    int peakStartedIndex = 0;
    int maxIndex = 0;
    int prevMaxIndex = -samplesCount;
    double maxValue = 0.0;
    pNormData = pSamples;
    for (int i = 0; i < samplesCount; i++, pNormData++) {
        if (*pNormData > barrier && i - prevMaxIndex > minInterval) {
            *(pHeartRate + i) = 1;
            // пик начался?
            if (i > 0 && *(pHeartRate + i - 1) == 0) {
                peakStartedIndex = i;
                maxIndex = 0;
                maxValue = 0.0;
            }
            if (maxValue < *pNormData) {
                maxIndex = i;
                maxValue = *pNormData;
            }
        } else {
            // пик закончился?
            if (i > 0 && *(pHeartRate + i - 1) != 0) {
                *(pHeartRate + i) = 0;
                if (peakStartedIndex < i - 1) {
                    // уточняем положение максимума
                    for (int j = peakStartedIndex; j < i; j++) {
                        *(pHeartRate + j) = 0;
                    }
                    // ненулевое значение ЧСС для максимального значения пика
                    if (prevMaxIndex != -1) {
                        // ЧСС
                        *(pHeartRate + maxIndex) = int(sampleRate * 60.0 / double(maxIndex - prevMaxIndex));
                    }
                    prevMaxIndex = maxIndex;

//                    printf("Maximum index = %d, HR = %d\n", maxIndex, *(pHeartRate + maxIndex));
                }
            }
        }
    }
    delete[] pSamples;
    delete[] pWindow;
    printf("Finding peaks: end\n");
}

double GraphicAreaWidget::getSampleRate(int channel)
{
    return ((double)mpEDFHeader->signalparam[channel].smp_in_datarecord /
            (double)mpEDFHeader->datarecord_duration) * EDFLIB_TIME_DIMENSION;
}

void GraphicAreaWidget::findTimeLag(int * pHeartRateECG, int samplesCountECG, double sampleRateECG, int * pHeartRateP, double * pTimeLag, int samplesCountP, double sampleRateP)
{
    int maxTimeLag =  60.0 / MIN_HEART_RATE;

    memset(pTimeLag, 0, sizeof(double) * samplesCountP);

    for (int indexECG = 0; indexECG < samplesCountECG; indexECG++) {
        double timeECG = double(indexECG) / sampleRateECG;
        if (*(pHeartRateECG + indexECG) > 0) {
            int startIndexP = int(double(indexECG) * sampleRateP / sampleRateECG);
            for (int indexP = startIndexP; indexP < samplesCountP; indexP++) {
                double timeP = double(indexP) / sampleRateP;
                if (timeP - timeECG > 0 && timeP - timeECG < maxTimeLag && *(pHeartRateP + indexP) > 0) {
                    *(pTimeLag + indexP) = timeP - timeECG;
//                    printf("Time lag = %lf\n", timeP - timeECG);
                    break;
                }
            }
        }
    }

}
