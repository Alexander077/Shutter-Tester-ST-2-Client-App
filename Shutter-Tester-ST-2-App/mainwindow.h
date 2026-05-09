/*
 * Copyright (C) 2026 Alexander Litvinov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <QMainWindow>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QVector>
#include <QMap>
#include <QScrollArea>
#include <QVBoxLayout>
#include "SerialController.h"
#include "FirmwareUpdateDialog.h"

class BlinkingRowDelegate;
class QLabel;
class QStatusBar;

struct MeasurementRun {
    double sensor0Time = -999;
    double sensor1Time = -999;
    double curtain1spanAtime = -999;
    double curtain1spanAspeed = -999;
    double curtain2spanAtime = -999;
    double curtain2spanAspeed = -999;
    double slitWidthSensor0 = -999;
    double slitWidthSensor1 = -999;
    double slitWidthAverage = -999;
};

struct RowData {
    QVector<MeasurementRun> runs;
    double avg0 = 0.0;
    double avg1 = 0.0;
    double avg = 0.0;
    double devEV = 0.0;
    double devPct = 0.0;
    QString status;
};

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSpeedSeriesComboBoxActivated(int index);
    void onDeleteSeriesButtonClicked();
    void onBlinkTimerTimeout();
    
    // SerialController slots
    void onAvailablePortsChanged();
    void onIsConnectedChanged();
    void onErrorOccurred(const QString &errorString);
    void onMeasurementReceived(const QJsonObject &result);
    void onGenerateReportClicked();
    void onSpeedsTableSelectionChanged();
    void onFirmwareUpdateTriggered();
    void onLightSetupTriggered();
    void onConnectToDeviceTriggered();
    void onSaveSessionTriggered();
    void onRestoreSessionTriggered();
    void onAboutTriggered();
    void onExitTriggered();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateSpeedSeriesModel();
    void updateTableForCurrentSeries();
    QJsonArray loadCustomSeries();
    void saveCustomSeries(const QString &name, const QString &speedsStr, bool temporary = false);
    void deleteCustomSeries(int index);
    bool validateSpeeds(const QString &speedsStr);
    void cleanupTemporaryCustomSeries();
    
    static const QString APP_VERSION;
    
    void startMeasurement(int row);
    void sendMeasurementRequest();
    void finalizeMeasurement(bool aborted = false);
    QString formatSpeed(double ms);
    double getAcceptanceThresholdEV();
    void updateDetailPanel(int row);
    void updateDeviceStatusBar();

    Ui::MainWindow *ui;
    QSettings m_settings;
    SerialController m_serialController;
    int m_previousSpeedSeriesIndex = 0;
    
    int m_currentMeasurementRow = -1;
    int m_measurementsTotal = 0;
    int m_measurementsDone = 0;
    double m_sensor0Sum = 0.0;
    double m_sensor1Sum = 0.0;
    QVector<MeasurementRun> m_currentRuns;
    QVector<RowData> m_rowsData;

    QString generateReportHtml();

    QTimer m_blinkTimer;
    BlinkingRowDelegate *m_delegate = nullptr;

    QMap<int, QVector<MeasurementRun>> m_rowMeasurements;
    QScrollArea *m_detailScrollArea = nullptr;
    QWidget *m_detailContentWidget = nullptr;
    QVBoxLayout *m_detailContentLayout = nullptr;
    QLabel *m_detailTitleLabel = nullptr;
    QLabel *m_deviceStatusLabel = nullptr;

    bool m_sessionDirty = false;

    bool saveSessionInteractive();
};
