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
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "FirmwareUpdateDialog.h"
#include "LightSetupDialog.h"
#include "ConnectionDialog.h"
#include <QJsonDocument>
#include <QMessageBox>
#include <QCloseEvent>
#include <QInputDialog>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QStyle>
#include <cmath>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QTableWidget>
#include <QComboBox>
#include <QColor>
#include <QFont>
#include <QBrush>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFile>
#include <QTextDocument>
#include <QPdfWriter>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QDateTime>
#include <limits>
#include <QFileInfo>
#include <QScrollArea>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <QApplication>

namespace {
struct ParamDef {
    const char* name;
    const char* unit;
    double MeasurementRun::*field;
};

const ParamDef paramDefs[] = {
    {"Sensor 1 Time", "ms", &MeasurementRun::sensor0Time},
    {"Sensor 2 Time", "ms", &MeasurementRun::sensor1Time},
    {"Curtain 1 Travel Time", "ms", &MeasurementRun::curtain1spanAtime},
    {"Curtain 1 Speed", "mm/ms", &MeasurementRun::curtain1spanAspeed},
    {"Curtain 2 Travel Time", "ms", &MeasurementRun::curtain2spanAtime},
    {"Curtain 2 Speed", "mm/ms", &MeasurementRun::curtain2spanAspeed},
    {"Slit Width Sensor 1", "mm", &MeasurementRun::slitWidthSensor0},
    {"Slit Width Sensor 2", "mm", &MeasurementRun::slitWidthSensor1},
    {"Slit Width Average", "mm", &MeasurementRun::slitWidthAverage},
};
const int paramCount = sizeof(paramDefs) / sizeof(paramDefs[0]);
}

const QString MainWindow::APP_VERSION = "0.6.0 alpha";

class BlinkingRowDelegate : public QStyledItemDelegate {
public:
    int blinkingRow = -1;
    int alpha = 0;
    bool fadingIn = true;

    BlinkingRowDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        if (opt.state & QStyle::State_Selected) {
            // === НАСТРОЙКА ЦВЕТА ВЫДЕЛЕНИЯ РЯДА ===
            // Здесь можно менять цвет выделения выбранного ряда
            opt.palette.setColor(QPalette::Highlight, QColor("#eeeeee"));      // серый фон
            opt.palette.setColor(QPalette::HighlightedText, QColor("black"));   // чёрный текст
        }
        QStyledItemDelegate::paint(painter, opt, index);
        if (index.row() == blinkingRow && alpha > 0) {
            painter->save();
            QColor color("green");
            color.setAlpha(alpha);
            QPen pen(color, 3); // Thicker pen for visibility
            painter->setPen(pen);
            QRect rect = option.rect;
            // Draw lines to simulate row border
            if (index.column() == 0) {
                painter->drawLine(rect.topLeft(), rect.bottomLeft());
            }
            if (index.column() == index.model()->columnCount() - 1) {
                painter->drawLine(rect.topRight(), rect.bottomRight());
            }
            // Draw top and bottom lines for all cells in the row
            painter->drawLine(rect.topLeft(), rect.topRight());
            painter->drawLine(rect.bottomLeft(), rect.bottomRight());
            painter->restore();
        }
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setWindowTitle(QString("Shutter Tester App v%1").arg(APP_VERSION));

    m_delegate = new BlinkingRowDelegate(this);
    ui->speedsTableWidget->setItemDelegate(m_delegate);

    ui->speedsTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->speedsTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    m_blinkTimer.setInterval(30); // ~30fps for smooth fade
    connect(&m_blinkTimer, &QTimer::timeout, this, &MainWindow::onBlinkTimerTimeout);

    ui->frameSizeComboBox->addItems({"35 mm", "6x45", "6x6", "6x7"/* , "6x9" */});
    ui->shutterTypeComboBox->addItems({"Focal Plane - Horizontal", "Focal Plane - Vertical", "Leaf Shutter"});
    ui->runsPerSpeedComboBox->addItems({"1", "2", "3", "4", "5"});

    ui->deleteSeriesButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    ui->deleteSeriesButton->setEnabled(false);

    // Initial Splitter sizes
    ui->splitter->setSizes({250, 600, 250});

    cleanupTemporaryCustomSeries();
    updateSpeedSeriesModel();

    connect(ui->speedSeriesComboBox, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::onSpeedSeriesComboBoxActivated);
    connect(ui->deleteSeriesButton, &QToolButton::clicked, this, &MainWindow::onDeleteSeriesButtonClicked);

    connect(&m_serialController, &SerialController::availablePortsChanged, this, &MainWindow::onAvailablePortsChanged);
    connect(&m_serialController, &SerialController::isConnectedChanged, this, &MainWindow::onIsConnectedChanged);
    connect(&m_serialController, &SerialController::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(&m_serialController, &SerialController::measurementReceived, this, &MainWindow::onMeasurementReceived);
    connect(&m_serialController, &SerialController::deviceInfoChanged, this, &MainWindow::updateDeviceStatusBar);
    connect(ui->actionGenerate_Report, &QAction::triggered, this, &MainWindow::onGenerateReportClicked);
    connect(ui->actionFirmware_Update, &QAction::triggered, this, &MainWindow::onFirmwareUpdateTriggered);
    connect(ui->actionLight_Setup, &QAction::triggered, this, &MainWindow::onLightSetupTriggered);
    connect(ui->actionConnect_To_Device, &QAction::triggered, this, &MainWindow::onConnectToDeviceTriggered);
    connect(ui->actionSave_Current_Session, &QAction::triggered, this, &MainWindow::onSaveSessionTriggered);
    connect(ui->actionRestore_Session, &QAction::triggered, this, &MainWindow::onRestoreSessionTriggered);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::onExitTriggered);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::onAboutTriggered);

    QLayoutItem *spacerItem = ui->verticalLayoutRight->itemAt(ui->verticalLayoutRight->count() - 1);
    if (spacerItem && spacerItem->spacerItem()) {
        ui->verticalLayoutRight->removeItem(spacerItem);
        delete spacerItem;
    }

    m_detailTitleLabel = new QLabel("Measurement Details", this);
    m_detailTitleLabel->setStyleSheet("font-weight: bold; font-size: 13px; margin-top: 6px;");
    ui->verticalLayoutRight->addWidget(m_detailTitleLabel);

    m_detailScrollArea = new QScrollArea(this);
    m_detailScrollArea->setWidgetResizable(true);
    m_detailScrollArea->setFrameShape(QFrame::NoFrame);

    m_detailContentWidget = new QWidget(m_detailScrollArea);
    m_detailContentLayout = new QVBoxLayout(m_detailContentWidget);
    m_detailContentLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *placeholder = new QLabel("Select a speed row to see details");
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("color: gray; font-style: italic; padding: 20px;");
    m_detailContentLayout->addWidget(placeholder);
    m_detailContentLayout->addStretch();

    m_detailScrollArea->setWidget(m_detailContentWidget);
    ui->verticalLayoutRight->addWidget(m_detailScrollArea, 1);

    connect(ui->speedsTableWidget, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSpeedsTableSelectionChanged);

    // Setup device status label in status bar
    m_deviceStatusLabel = new QLabel(this);
    m_deviceStatusLabel->setStyleSheet("padding: 2px 8px;");
    statusBar()->addWidget(m_deviceStatusLabel);
    updateDeviceStatusBar();

    onAvailablePortsChanged();
    QTimer::singleShot(0, this, &MainWindow::onConnectToDeviceTriggered);
}

MainWindow::~MainWindow()
{
    delete ui;
}

QJsonArray MainWindow::loadCustomSeries()
{
    QString jsonStr = m_settings.value("customSpeedSeriesJson", "[]").toString();
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (doc.isArray()) {
        return doc.array();
    }
    return QJsonArray();
}

void MainWindow::updateSpeedSeriesModel()
{
    ui->speedSeriesComboBox->blockSignals(true);
    ui->speedSeriesComboBox->clear();
    ui->speedSeriesComboBox->addItem("Old Style");
    ui->speedSeriesComboBox->addItem("New Style (ISO)");

    QJsonArray customSeries = loadCustomSeries();
    for (int i = 0; i < customSeries.size(); ++i) {
        QJsonObject obj = customSeries[i].toObject();
        ui->speedSeriesComboBox->addItem(obj["name"].toString());
    }

    ui->speedSeriesComboBox->addItem("Custom...");
    ui->speedSeriesComboBox->blockSignals(false);

    int count = ui->speedSeriesComboBox->count();
    ui->speedSeriesComboBox->setCurrentIndex(m_previousSpeedSeriesIndex < count ? m_previousSpeedSeriesIndex : 0);
    onSpeedSeriesComboBoxActivated(ui->speedSeriesComboBox->currentIndex());
}

void MainWindow::saveCustomSeries(const QString &name, const QString &speedsStr, bool temporary)
{
    QJsonArray series = loadCustomSeries();

    // If an identical series (same name and speeds) already exists, don't add duplicate.
    for (int i = 0; i < series.size(); ++i)
    {
        QJsonObject obj = series[i].toObject();
        if (obj["name"].toString() == name && obj["speeds"].toString() == speedsStr)
        {
            // Select existing entry
            m_previousSpeedSeriesIndex = 2 + i; // combo: 0=Old,1=ISO,2+=custom
            updateSpeedSeriesModel();
            return;
        }
    }

    QJsonObject newObj;
    newObj["name"] = name;
    newObj["speeds"] = speedsStr;
    newObj["temporary"] = temporary;
    series.append(newObj);

    m_settings.setValue("customSpeedSeriesJson", QString::fromUtf8(QJsonDocument(series).toJson(QJsonDocument::Compact)));

    // Newly added custom series will be the last in the custom series array.
    m_previousSpeedSeriesIndex = 2 + (series.size() - 1);
    updateSpeedSeriesModel();
}

void MainWindow::deleteCustomSeries(int index)
{
    QJsonArray series = loadCustomSeries();
    if (index >= 0 && index < series.size()) {
        series.removeAt(index);
        m_settings.setValue("customSpeedSeriesJson", QString::fromUtf8(QJsonDocument(series).toJson(QJsonDocument::Compact)));
        m_previousSpeedSeriesIndex = 0;
        updateSpeedSeriesModel();
    }
}

bool MainWindow::validateSpeeds(const QString &speedsStr)
{
    if (speedsStr.trimmed().isEmpty()) return false;
    QStringList parts = speedsStr.split(',');
    QRegularExpression reInt("^\\d+$");
    QRegularExpression reFrac("^1/\\d+$");

    for (const QString &part : parts) {
        QString speed = part.trimmed();
        if (!reInt.match(speed).hasMatch() && !reFrac.match(speed).hasMatch()) {
            return false;
        }
    }
    return true;
}

void MainWindow::onSpeedSeriesComboBoxActivated(int index)
{
    int count = ui->speedSeriesComboBox->count();
    if (ui->speedSeriesComboBox->currentText() == "Custom...") {
        QDialog dialog(this);
        dialog.setWindowTitle("Create Custom Speed Series");
        dialog.resize(350, 250);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        
        layout->addWidget(new QLabel("Series Name:"));
        QLineEdit *nameEdit = new QLineEdit(&dialog);
        layout->addWidget(nameEdit);

        layout->addWidget(new QLabel("Speed Series:"));
        QTextEdit *speedsEdit = new QTextEdit(&dialog);
        speedsEdit->setPlaceholderText("5, 8, 1/2, 1/4, 1/567, etc.");
        layout->addWidget(speedsEdit);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        while (true) {
            if (dialog.exec() == QDialog::Accepted) {
                QString name = nameEdit->text().trimmed();
                QString speeds = speedsEdit->toPlainText();

                if (name.isEmpty()) {
                    QMessageBox::critical(this, "Error", "Series Name cannot be empty.");
                    continue;
                } else if (validateSpeeds(speeds)) {
                    saveCustomSeries(name, speeds);
                    break;
                } else {
                    QMessageBox::critical(this, "Error", "Invalid speeds format.\nPlease use a format like '1/2, 1/4, 1' or '6'.\nDo not use spaces inside fractions.");
                    continue;
                }
            } else {
                ui->speedSeriesComboBox->setCurrentIndex(m_previousSpeedSeriesIndex);
                break;
            }
        }
    } else {
        m_previousSpeedSeriesIndex = index;
    }

    // Enable delete button only for custom series
    ui->deleteSeriesButton->setEnabled(m_previousSpeedSeriesIndex >= 2 && m_previousSpeedSeriesIndex < ui->speedSeriesComboBox->count() - 1);
    
    updateTableForCurrentSeries();
}

void MainWindow::updateTableForCurrentSeries()
{
    int index = ui->speedSeriesComboBox->currentIndex();
    if (index < 0) return;
    QString text = ui->speedSeriesComboBox->itemText(index);
    if (text == "Custom...") return;

    QStringList speeds;
    if (text == "Old Style") {
        speeds = {"1", "1/2", "1/5", "1/10", "1/25", "1/50", "1/100", "1/200", "1/500"};
    } else if (text == "New Style (ISO)") {
        speeds = {"1", "1/2", "1/4", "1/8", "1/15", "1/30", "1/60", "1/125", "1/250", "1/500", "1/1000", "1/2000"};
    } else {
        QJsonArray customSeries = loadCustomSeries();

        // Map combo box index to custom series index. Combo layout:
        // 0 = Old Style, 1 = New Style (ISO), 2..(2+N-1) = custom series, last = "Custom..."
        int comboIndex = ui->speedSeriesComboBox->currentIndex();
        int customIndex = comboIndex - 2;

        if (customIndex >= 0 && customIndex < customSeries.size())
        {
            QJsonObject obj = customSeries[customIndex].toObject();
            QString speedsStr = obj["speeds"].toString();
            QStringList parts = speedsStr.split(',');
            for (const QString &part : parts)
            {
                speeds.append(part.trimmed());
            }
        }
        else
        {
            // Fallback: search by name (keeps previous behavior for unexpected cases)
            for (int i = 0; i < customSeries.size(); ++i)
            {
                QJsonObject obj = customSeries[i].toObject();
                if (obj["name"].toString() == text)
                {
                    QString speedsStr = obj["speeds"].toString();
                    QStringList parts = speedsStr.split(',');
                    for (const QString &part : parts)
                    {
                        speeds.append(part.trimmed());
                    }
                    break;
                }
            }
        }
    }

    m_rowsData.clear();

    ui->speedsTableWidget->clear();
    m_rowMeasurements.clear();
    QStringList headers = {"Speed", "Sen. 1 avg", "Sen. 2 avg", "Avg", "Dev. EV", "Dev %", "Progress", "Status", ""};
    ui->speedsTableWidget->setColumnCount(headers.size());
    ui->speedsTableWidget->setHorizontalHeaderLabels(headers);
    ui->speedsTableWidget->setRowCount(speeds.size());

    ui->speedsTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->speedsTableWidget->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    ui->speedsTableWidget->verticalHeader()->setMinimumSectionSize(40);
    ui->speedsTableWidget->verticalHeader()->setDefaultSectionSize(40);

    for (int i = 0; i < speeds.size(); ++i) {
        QTableWidgetItem *speedItem = new QTableWidgetItem(speeds[i]);
        speedItem->setTextAlignment(Qt::AlignCenter);
        speedItem->setFlags(speedItem->flags() & ~Qt::ItemIsEditable);
        ui->speedsTableWidget->setItem(i, 0, speedItem);

        for (int col = 1; col <= 5; ++col) {
            QTableWidgetItem *item = new QTableWidgetItem("-");
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            ui->speedsTableWidget->setItem(i, col, item);
        }

        QTableWidgetItem *progressItem = new QTableWidgetItem("-/-");
        progressItem->setTextAlignment(Qt::AlignCenter);
        progressItem->setFlags(progressItem->flags() & ~Qt::ItemIsEditable);
        ui->speedsTableWidget->setItem(i, 6, progressItem);

        QTableWidgetItem *statusItem = new QTableWidgetItem("-");
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        ui->speedsTableWidget->setItem(i, 7, statusItem);

        QPushButton *measureButton = new QPushButton("Measure", this);
        connect(measureButton, &QPushButton::clicked, this, [this, i]() {
            startMeasurement(i);
        });
        ui->speedsTableWidget->setCellWidget(i, 8, measureButton);
    }
}

void MainWindow::onDeleteSeriesButtonClicked()
{
    int index = ui->speedSeriesComboBox->currentIndex();
    int count = ui->speedSeriesComboBox->count();
    if (index >= 2 && index < count - 1) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Deletion",
                                      "Are you sure you want to delete this custom series?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            deleteCustomSeries(index - 2);
        }
    }
}

void MainWindow::onAvailablePortsChanged()
{
}

void MainWindow::onIsConnectedChanged()
{
    if (m_serialController.isConnected()) {
        ui->errorText->clear();
    }
    updateDeviceStatusBar();
}

void MainWindow::onErrorOccurred(const QString &errorString)
{
    ui->errorText->setText(errorString);
}

void MainWindow::onBlinkTimerTimeout()
{
    if (m_delegate && m_currentMeasurementRow >= 0) {
        if (m_delegate->fadingIn) {
            m_delegate->alpha += 15;
            if (m_delegate->alpha >= 255) {
                m_delegate->alpha = 255;
                m_delegate->fadingIn = false;
            }
        } else {
            m_delegate->alpha -= 15;
            if (m_delegate->alpha <= 0) {
                m_delegate->alpha = 0;
                m_delegate->fadingIn = true;
            }
        }
        ui->speedsTableWidget->viewport()->update();
    } else if (m_delegate) {
        m_delegate->alpha = 0;
        m_delegate->blinkingRow = -1;
        m_delegate->fadingIn = true;
    }
}

double MainWindow::getAcceptanceThresholdEV()
{
    int val = ui->acceptanceSlider->value();
    if (val == 0) return 0.05;
    if (val == 1) return 0.1;
    if (val == 2) return 0.2;
    if (val == 3) return 0.5;
    return 0.1;
}

QString MainWindow::formatSpeed(double ms)
{
    if (ms <= 0) return "-";
    int den = qRound(1000.0 / ms);
    QString fracPart;
    if (den > 1) {
        fracPart = QString("1/%1").arg(den);
    } else {
        fracPart = QString::number(ms / 1000.0, 'f', 2).remove(QRegularExpression("\\.?0+$"));
        if (fracPart.isEmpty()) fracPart = "1";
    }
    return QString("%1\n(%2 ms)").arg(fracPart).arg(ms, 0, 'f', 3);
}

void MainWindow::startMeasurement(int row)
{
    if (!m_serialController.isConnected()) {
        QMessageBox::warning(this, "Error", "Device not connected.");
        return;
    }

    QPushButton *clickedBtn = qobject_cast<QPushButton*>(ui->speedsTableWidget->cellWidget(row, 8));
    if (clickedBtn && clickedBtn->text() == "Stop") {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Stop measurements?",
                                      "Stop measurements?",
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_serialController.abortOperation();
            finalizeMeasurement(true);
        }
        return;
    }

    if (m_currentMeasurementRow >= 0) {
        if (m_currentMeasurementRow == row) {
            // Already handled by Stop above
            return;
        } else {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Stop current measuring and start another?",
                                          "Stop current measuring and start another?",
                                          QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                m_serialController.abortOperation();
                finalizeMeasurement(true);
                
                // Delay starting the new measurement to allow the hardware to process the abort
                QTimer::singleShot(250, this, [this, row]() {
                    startMeasurement(row);
                });
            }
            return;
        }
    }

    m_currentMeasurementRow = row;
    m_measurementsTotal = ui->runsPerSpeedComboBox->currentText().toInt();
    m_measurementsDone = 0;
    m_currentRuns.clear();
    m_sensor0Sum = 0.0;
    m_sensor1Sum = 0.0;
    m_rowMeasurements.remove(row);
    if (m_rowsData.size() <= row) {
        m_rowsData.resize(row + 1);
    }
    m_rowsData[row] = RowData();

    for (int col = 1; col <= 5; ++col) {
        QTableWidgetItem *item = ui->speedsTableWidget->item(m_currentMeasurementRow, col);
        if (item) item->setText("-");
    }

    QTableWidgetItem *statusItem = ui->speedsTableWidget->item(m_currentMeasurementRow, 7);
    if (statusItem) {
        statusItem->setText("-");
        statusItem->setData(Qt::ForegroundRole, QVariant());
        QFont font = statusItem->font();
        font.setBold(false);
        statusItem->setFont(font);
    }

    ui->speedsTableWidget->item(m_currentMeasurementRow, 6)->setText(QString("0/%1").arg(m_measurementsTotal));

    if (m_delegate) {
        m_delegate->blinkingRow = m_currentMeasurementRow;
        m_delegate->alpha = 0;
        m_delegate->fadingIn = true;
    }
    m_blinkTimer.start();
    ui->speedsTableWidget->viewport()->update();

    sendMeasurementRequest();
}

void MainWindow::sendMeasurementRequest()
{
    int sensorIndex = ui->frameSizeComboBox->currentIndex();
    int curtainMovement = ui->shutterTypeComboBox->currentIndex();

    QPushButton *btn = qobject_cast<QPushButton*>(ui->speedsTableWidget->cellWidget(m_currentMeasurementRow, 8));
    if (btn) {
        btn->setText("Stop");
        btn->setEnabled(true);
    }

    m_serialController.requestMeasurement(sensorIndex, curtainMovement);
}

void MainWindow::onMeasurementReceived(const QJsonObject &result)
{
    if (m_currentMeasurementRow < 0) return;

    QPushButton *btn = qobject_cast<QPushButton*>(ui->speedsTableWidget->cellWidget(m_currentMeasurementRow, 8));

    if (result["status"].toString() != "API_RESPONSE_STATUS_OK") {
        if (btn) { btn->setText("Measure"); btn->setEnabled(true); }
        QMessageBox::warning(this, "Measurement Error", result["error"].toString("Unknown error"));
        m_currentMeasurementRow = -1;
        return;
    }

    double s0 = result["sensor0Time"].toDouble(-999.0);
    double s1 = result["sensor1Time"].toDouble(-999.0);

    if (s0 < 0 || s1 < 0) {
        if (btn) { btn->setText("Measure"); btn->setEnabled(true); }
        QMessageBox::warning(this, "Measurement Error", QString("Invalid sensor reading. Code: s0=%1, s1=%2").arg(s0).arg(s1));
        m_currentMeasurementRow = -1;
        return;
    }

    MeasurementRun run;
    run.sensor0Time = s0;
    run.sensor1Time = s1;
    run.curtain1spanAtime = result["curtain1spanAtime"].toDouble(-999.0);
    run.curtain1spanAspeed = result["curtain1spanAspeed"].toDouble(-999.0);
    run.curtain2spanAtime = result["curtain2spanAtime"].toDouble(-999.0);
    run.curtain2spanAspeed = result["curtain2spanAspeed"].toDouble(-999.0);
    run.slitWidthSensor0 = result["slitWidthSensor0"].toDouble(-999.0);
    run.slitWidthSensor1 = result["slitWidthSensor1"].toDouble(-999.0);
    run.slitWidthAverage = result["slitWidthAverage"].toDouble(-999.0);

    m_rowMeasurements[m_currentMeasurementRow].append(run);
    m_currentRuns.append(run);

    m_sensor0Sum += s0;
    m_sensor1Sum += s1;
    m_measurementsDone++;

    ui->speedsTableWidget->item(m_currentMeasurementRow, 6)->setText(QString("%1/%2").arg(m_measurementsDone).arg(m_measurementsTotal));

    if (ui->speedsTableWidget->currentRow() == m_currentMeasurementRow) {
        updateDetailPanel(m_currentMeasurementRow);
    }

    if (m_measurementsDone < m_measurementsTotal) {
        sendMeasurementRequest();
    } else {
        finalizeMeasurement(false);
    }
}

void MainWindow::finalizeMeasurement(bool aborted)
{
    if (m_currentMeasurementRow < 0) return;

    m_blinkTimer.stop();
    if (m_delegate) {
        m_delegate->blinkingRow = -1;
        m_delegate->alpha = 0;
        m_delegate->fadingIn = true;
    }
    ui->speedsTableWidget->viewport()->update();

    QPushButton *btn = qobject_cast<QPushButton*>(ui->speedsTableWidget->cellWidget(m_currentMeasurementRow, 8));
    if (btn) { btn->setText("Measure"); btn->setEnabled(true); }

    if (m_measurementsDone == 0) {
        ui->speedsTableWidget->item(m_currentMeasurementRow, 6)->setText("-/-");
        m_currentMeasurementRow = -1;
        return;
    }

    double avg0 = m_sensor0Sum / m_measurementsDone;
    double avg1 = m_sensor1Sum / m_measurementsDone;
    double avg = (avg0 + avg1) / 2.0;

    ui->speedsTableWidget->item(m_currentMeasurementRow, 1)->setText(formatSpeed(avg0));
    ui->speedsTableWidget->item(m_currentMeasurementRow, 2)->setText(formatSpeed(avg1));
    ui->speedsTableWidget->item(m_currentMeasurementRow, 3)->setText(formatSpeed(avg));

    QString speedStr = ui->speedsTableWidget->item(m_currentMeasurementRow, 0)->text();
    double targetTimeMs = 0.0;
    if (speedStr.contains('/')) {
        QStringList parts = speedStr.split('/');
        if (parts.size() == 2 && parts[1].toDouble() != 0) {
            targetTimeMs = 1000.0 * parts[0].toDouble() / parts[1].toDouble();
        }
    } else {
        targetTimeMs = 1000.0 * speedStr.toDouble();
    }

    RowData &rowData = m_rowsData[m_currentMeasurementRow];
    rowData.runs = m_currentRuns;
    rowData.avg0 = avg0;
    rowData.avg1 = avg1;
    rowData.avg = avg;

    if (targetTimeMs > 0.0) {
        double devPct = ((avg - targetTimeMs) / targetTimeMs) * 100.0;
        double devEV = std::log2(avg / targetTimeMs);

        rowData.devEV = devEV;
        rowData.devPct = devPct;

        ui->speedsTableWidget->item(m_currentMeasurementRow, 4)->setText(QString::number(devEV, 'f', 2));
        ui->speedsTableWidget->item(m_currentMeasurementRow, 5)->setText(QString::number(devPct, 'f', 2));

        double threshold = getAcceptanceThresholdEV();
        QTableWidgetItem *statusItem = ui->speedsTableWidget->item(m_currentMeasurementRow, 7);
        if (std::abs(devEV) <= threshold) {
            statusItem->setText("PASS");
            statusItem->setForeground(QBrush(QColor("green")));
            QFont font = statusItem->font();
            font.setBold(true);
            statusItem->setFont(font);
            rowData.status = "PASS";
        } else {
            statusItem->setText("FAIL");
            statusItem->setForeground(QBrush(QColor("red")));
            QFont font = statusItem->font();
            font.setBold(true);
            statusItem->setFont(font);
            rowData.status = "FAIL";
        }
    }

    if (!aborted) {
        QMessageBox::information(this, "Measurement Complete", QString("Averaged result over %1 runs:\nSen. 1: %2 ms\nSen. 2: %3 ms\nAvg: %4 ms\nTarget: %5 ms")
            .arg(m_measurementsDone).arg(avg0, 0, 'f', 2).arg(avg1, 0, 'f', 2).arg(avg, 0, 'f', 2).arg(targetTimeMs, 0, 'f', 2));
    }

    // Mark session as changed (unsaved) when measurements were completed
    if (!aborted && m_measurementsDone > 0)
    {
        m_sessionDirty = true;
    }

    int completedRow = m_currentMeasurementRow;
    m_currentMeasurementRow = -1;

    if (ui->speedsTableWidget->currentRow() == completedRow) {
        updateDetailPanel(completedRow);
    }
}

void MainWindow::onSpeedsTableSelectionChanged()
{
    int row = ui->speedsTableWidget->currentRow();
    updateDetailPanel(row);
}

void MainWindow::updateDetailPanel(int row)
{
    QLayoutItem *item;
    while ((item = m_detailContentLayout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    if (row < 0 || !m_rowMeasurements.contains(row) || m_rowMeasurements[row].isEmpty()) {
        m_detailTitleLabel->setText("Measurement Details");
        QLabel *noDataLabel = new QLabel("No measurement data for this speed.");
        noDataLabel->setAlignment(Qt::AlignCenter);
        noDataLabel->setStyleSheet("color: gray; font-style: italic; padding: 20px;");
        m_detailContentLayout->addWidget(noDataLabel);
        m_detailContentLayout->addStretch();
        return;
    }

    QString speedStr = ui->speedsTableWidget->item(row, 0) ? ui->speedsTableWidget->item(row, 0)->text() : "";
    m_detailTitleLabel->setText(QString("Measurement Details — %1").arg(speedStr));

    const QVector<MeasurementRun> &runs = m_rowMeasurements[row];

    for (int p = 0; p < paramCount; ++p) {
        const ParamDef &def = paramDefs[p];

        QVector<double> values;
        for (const MeasurementRun &run : runs) {
            double val = run.*(def.field);
            if (val >= 0) {
                values.append(val);
            }
        }

        if (values.isEmpty()) continue;

        double minVal = *std::min_element(values.constBegin(), values.constEnd());
        double maxVal = *std::max_element(values.constBegin(), values.constEnd());
        double sumVal = 0;
        for (double v : values) sumVal += v;
        double avgVal = sumVal / values.size();

        QGroupBox *groupBox = new QGroupBox(def.name, this);
        QVBoxLayout *groupLayout = new QVBoxLayout(groupBox);
        groupLayout->setContentsMargins(6, 8, 6, 6);
        groupLayout->setSpacing(4);

        QFont statsFont;
        statsFont.setPointSize(8);

        if (values.size() == 1) {
            // Only one measurement - show just the value
            QLabel *valueLabel = new QLabel(QString("%1 %2").arg(values[0], 0, 'f', 3).arg(def.unit));
            valueLabel->setFont(statsFont);
            groupLayout->addWidget(valueLabel);
        } else {
            // Multiple measurements - show min/max/avg
            QHBoxLayout *statsLayout = new QHBoxLayout();
            statsLayout->setSpacing(12);

            QLabel *minLabel = new QLabel(QString("Min: %1 %2").arg(minVal, 0, 'f', 3).arg(def.unit));
            QLabel *maxLabel = new QLabel(QString("Max: %1 %2").arg(maxVal, 0, 'f', 3).arg(def.unit));
            QLabel *avgLabel = new QLabel(QString("Avg: %1 %2").arg(avgVal, 0, 'f', 3).arg(def.unit));

            minLabel->setFont(statsFont);
            maxLabel->setFont(statsFont);
            avgLabel->setFont(statsFont);

            statsLayout->addWidget(minLabel);
            statsLayout->addWidget(maxLabel);
            statsLayout->addWidget(avgLabel);
            statsLayout->addStretch();

            groupLayout->addLayout(statsLayout);
        }

        if (values.size() > 2) {
            QLineSeries *series = new QLineSeries();
            for (int i = 0; i < values.size(); ++i) {
                series->append(i + 1, values[i]);
            }
            series->setPointsVisible(true);
            series->setPointLabelsVisible(true);
            series->setPointLabelsFormat("@yPoint");
            series->setPointLabelsClipping(false);

            QChart *chart = new QChart();
            chart->addSeries(series);
            chart->legend()->hide();
            chart->setMargins(QMargins(20, 20, 20, 20));
            chart->setMinimumSize(200, 150);

            QValueAxis *axisX = new QValueAxis();
            axisX->setTitleText("Run #");
            axisX->setRange(0.5, values.size() + 0.5);
            axisX->setTickCount(values.size() + 1);
            axisX->setLabelFormat("%d");
            chart->addAxis(axisX, Qt::AlignBottom);
            series->attachAxis(axisX);

            QValueAxis *axisY = new QValueAxis();
            axisY->setTitleText(def.unit);
            double yRange = maxVal - minVal;
            double yPadding = yRange * 0.25;
            if (yPadding < 0.01) yPadding = maxVal * 0.25;
            if (yPadding < 0.01) yPadding = 1.0;
            axisY->setRange(minVal - yPadding, maxVal + yPadding);
            axisY->setLabelFormat("%.2f");
            chart->addAxis(axisY, Qt::AlignLeft);
            series->attachAxis(axisY);

            QChartView *chartView = new QChartView(chart);
            chartView->setRenderHint(QPainter::Antialiasing);
            chartView->setMinimumHeight(180);
            chartView->setMaximumHeight(250);

            groupLayout->addWidget(chartView);
        }

        m_detailContentLayout->addWidget(groupBox);
    }

    m_detailContentLayout->addStretch();
}

QString MainWindow::generateReportHtml()
{
    QString templatePath = QCoreApplication::applicationDirPath() + "/assets/st2_fp_report_template.html";
    QFile file(templatePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Report Error", QString("Cannot open template file:\n%1").arg(templatePath));
        return QString();
    }

    QString html = QString::fromUtf8(file.readAll());
    file.close();

    // Extract row templates
    static const QRegularExpression speedRowRe(
        QStringLiteral("<!--FOCAL_PLANE_SPEED_ROW_TEMPLATE_START(.*?)FOCAL_PLANE_SPEED_ROW_TEMPLATE_END-->"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression curtainRowRe(
        QStringLiteral("<!--FOCAL_PLANE_CURTAIN_ROW_TEMPLATE_START(.*?)FOCAL_PLANE_CURTAIN_ROW_TEMPLATE_END-->"),
        QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatch speedMatch = speedRowRe.match(html);
    QRegularExpressionMatch curtainMatch = curtainRowRe.match(html);

    QString speedRowTemplate = speedMatch.hasMatch() ? speedMatch.captured(1).trimmed() : QString();
    QString curtainRowTemplate = curtainMatch.hasMatch() ? curtainMatch.captured(1).trimmed() : QString();

    // Build speed rows
    QString speedRowsHtml;
    int rowCount = ui->speedsTableWidget->rowCount();
    for (int i = 0; i < rowCount; ++i) {
        QString rowHtml = speedRowTemplate;
        QString speedText = ui->speedsTableWidget->item(i, 0)->text();
        QString s1Text = ui->speedsTableWidget->item(i, 1)->text();
        QString s2Text = ui->speedsTableWidget->item(i, 2)->text();
        QString avgText = ui->speedsTableWidget->item(i, 3)->text();
        QString devEvText = ui->speedsTableWidget->item(i, 4)->text();
        QString devPctText = ui->speedsTableWidget->item(i, 5)->text();
        QString statusText = ui->speedsTableWidget->item(i, 7)->text();

        // Clean newlines from avg text for html
        s1Text.replace("\n", "<br>");
        s2Text.replace("\n", "<br>");
        avgText.replace("\n", "<br>");

        rowHtml.replace("{{SPEED}}", speedText);
        rowHtml.replace("{{MEASURED_SPEED_S1}}", s1Text);
        rowHtml.replace("{{MEASURED_SPEED_S2}}", s2Text);
        rowHtml.replace("{{AVERAGE_SPEED}}", avgText);
        rowHtml.replace("{{DEVIATION_EV}}", devEvText);
        rowHtml.replace("{{DEVIATION_PERCENT}}", devPctText);
        rowHtml.replace("{{SPEED_STATUS_VALUE}}", statusText);

        // Uniformity: max avg - min avg across runs in EV
        double uniformityEv = 0.0;
        if (i < m_rowsData.size() && !m_rowsData[i].runs.isEmpty()) {
            double minAvg = std::numeric_limits<double>::max();
            double maxAvg = std::numeric_limits<double>::lowest();
            for (const auto &run : m_rowsData[i].runs) {
                double runAvg = (run.sensor0Time + run.sensor1Time) / 2.0;
                if (runAvg < minAvg) minAvg = runAvg;
                if (runAvg > maxAvg) maxAvg = runAvg;
            }
            if (minAvg > 0.0 && maxAvg > 0.0) {
                uniformityEv = std::log2(maxAvg / minAvg);
            }
        }
        rowHtml.replace("{{UNIFORMITY}}", QString::number(uniformityEv, 'f', 2));

        // Pill colors based on status
        if (statusText == "PASS") {
            rowHtml.replace("{{PILL_BG}}", "#e8f5e9");
            rowHtml.replace("{{PILL_COLOR}}", "#2e7d32");
            rowHtml.replace("{{PILL_BORDER}}", "#2e7d32");
        } else if (statusText == "FAIL") {
            rowHtml.replace("{{PILL_BG}}", "#ffebee");
            rowHtml.replace("{{PILL_COLOR}}", "#c62828");
            rowHtml.replace("{{PILL_BORDER}}", "#c62828");
        } else {
            rowHtml.replace("{{PILL_BG}}", "#f5f5f5");
            rowHtml.replace("{{PILL_COLOR}}", "#777770");
            rowHtml.replace("{{PILL_BORDER}}", "#bbb");
        }

        speedRowsHtml += rowHtml + "\n";
    }

    // Build curtain rows
    QString curtainRowsHtml;
    for (int i = 0; i < rowCount; ++i) {
        QString rowHtml = curtainRowTemplate;
        QString speedText = ui->speedsTableWidget->item(i, 0)->text();

        double fcSpeed = 0.0, scSpeed = 0.0, fcTime = 0.0, scTime = 0.0;
        double slitS1 = 0.0, slitS2 = 0.0;
        if (i < m_rowsData.size() && !m_rowsData[i].runs.isEmpty()) {
            // Average curtain data across runs
            double sumFcSpeed = 0.0, sumScSpeed = 0.0, sumFcTime = 0.0, sumScTime = 0.0;
            double sumSlitS1 = 0.0, sumSlitS2 = 0.0;
            int validRuns = 0;
            for (const auto &run : m_rowsData[i].runs) {
                sumFcSpeed += run.curtain1spanAspeed;
                sumScSpeed += run.curtain2spanAspeed;
                sumFcTime += run.curtain1spanAtime;
                sumScTime += run.curtain2spanAtime;
                sumSlitS1 += run.slitWidthSensor0;
                sumSlitS2 += run.slitWidthSensor1;
                validRuns++;
            }
            if (validRuns > 0) {
                fcSpeed = sumFcSpeed / validRuns;
                scSpeed = sumScSpeed / validRuns;
                fcTime = sumFcTime / validRuns;
                scTime = sumScTime / validRuns;
                slitS1 = sumSlitS1 / validRuns;
                slitS2 = sumSlitS2 / validRuns;
            }
        }

        auto fmt = [](double v) -> QString {
            if (v <= 0.0) return QString("&mdash;");
            return QString::number(v, 'f', 2);
        };

        rowHtml.replace("{{SPEED}}", speedText);
        rowHtml.replace("{{FC_SPEED}}", fmt(fcSpeed));
        rowHtml.replace("{{SC_SPEED}}", fmt(scSpeed));
        rowHtml.replace("{{FC_TIME}}", fmt(fcTime));
        rowHtml.replace("{{SC_TIME}}", fmt(scTime));
        rowHtml.replace("{{SLIT_WIDTH_AT_S1}}", fmt(slitS1));
        rowHtml.replace("{{SLIT_WIDTH_AT_S2}}", fmt(slitS2));

        curtainRowsHtml += rowHtml + "\n";
    }

    // Replace row templates with generated rows
    html.replace(speedRowRe, speedRowsHtml);
    html.replace(curtainRowRe, curtainRowsHtml);

    // Overall stats
    int tested = 0, passed = 0, failed = 0;
    QString overallStatus = "PASS";
    for (int i = 0; i < rowCount; ++i) {
        QString st = (i < m_rowsData.size()) ? m_rowsData[i].status : QString("-");
        if (st == "PASS" || st == "FAIL") {
            tested++;
            if (st == "PASS") passed++;
            else if (st == "FAIL") failed++;
        }
    }
    if (failed > 0) overallStatus = "FAIL";
    else if (tested == 0) overallStatus = "N/A";

    QString statusColor, statusBg, statusBorder;
    if (overallStatus == "PASS") {
        statusColor = "#2e7d32"; statusBg = "#e8f5e9"; statusBorder = "#2e7d32";
    } else if (overallStatus == "FAIL") {
        statusColor = "#c62828"; statusBg = "#ffebee"; statusBorder = "#c62828";
    } else {
        statusColor = "#777770"; statusBg = "#f5f5f5"; statusBorder = "#bbb";
    }

    html.replace("{{STATUS}}", overallStatus);
    html.replace("{{STATUS_COLOR}}", statusColor);
    html.replace("{{STATUS_BG_COLOR}}", statusBg);
    html.replace("{{STATUS_BORDER_COLOR}}", statusBorder);
    html.replace("{{SPEEDS_TESTED}}", QString::number(tested));
    html.replace("{{PASSED_COUNT}}", QString::number(passed));
    html.replace("{{FAILED_COUNT}}", QString::number(failed));

    // Global placeholders
    html.replace("{{DATE}}", QDateTime::currentDateTime().toString("yyyy-MM-dd"));
    html.replace("{{FIRMWARE}}", m_serialController.swVersion().isEmpty() ? QString("ST-2 FW N/A") : QString("ST-2 FW %1").arg(m_serialController.swVersion()));
    html.replace("{{APP_VERSION}}", QString("v1.0"));
    html.replace("{{CAMERA_MODEL}}", ui->cameraModelField->text().toHtmlEscaped());
    html.replace("{{SERIAL_NUMBER}}", ui->serialNumberField->text().toHtmlEscaped());
    html.replace("{{SHUTTER_NAME}}", QString("Shutter").toHtmlEscaped());
    html.replace("{{SHUTTER_TYPE}}", ui->shutterTypeComboBox->currentText().toHtmlEscaped());
    html.replace("{{FRAME_FORMAT}}", ui->frameSizeComboBox->currentText().toHtmlEscaped());
    html.replace("{{SPEED_SERIES}}", ui->speedSeriesComboBox->currentText().toHtmlEscaped());
    html.replace("{{TECHNICIAN}}", QString("Technician").toHtmlEscaped());
    html.replace("{{MEASUREMENT_RUNS}}", ui->runsPerSpeedComboBox->currentText());

    double tol = getAcceptanceThresholdEV();
    html.replace("{{TOLERANCE}}", QString("&plusmn;%1 EV").arg(tol));
    html.replace("{{NOTES}}", ui->notesTextEdit->toPlainText().toHtmlEscaped().replace("\n", "<br>"));

    return html;
}

void MainWindow::onFirmwareUpdateTriggered()
{
    FirmwareUpdateDialog dialog(&m_serialController, this);
    dialog.exec();
}

void MainWindow::onLightSetupTriggered()
{
    // If a measurement is in progress, abort it first
    if (m_currentMeasurementRow >= 0) {
        m_serialController.abortOperation();
        finalizeMeasurement(true);
    }
    LightSetupDialog dialog(&m_serialController, this);
    dialog.exec();
}

void MainWindow::onConnectToDeviceTriggered()
{
    bool isStartup = (sender() == nullptr); // singleShot passes nullptr
    ConnectionDialog dialog(&m_serialController, isStartup, this);
    dialog.exec();
}

void MainWindow::onGenerateReportClicked()
{
    QString html = generateReportHtml();
    if (html.isEmpty()) return;

    QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString defaultReport = docsPath + QDir::separator() + "ST-2_Report.pdf";
    QString fileName = QFileDialog::getSaveFileName(this, "Save Report", defaultReport, "PDF Files (*.pdf)");
    if (fileName.isEmpty()) return;

    // Save HTML alongside PDF
    QFileInfo fi(fileName);
    QString htmlFileName = fi.path() + "/" + fi.completeBaseName() + ".html";
    QFile htmlFile(htmlFileName);
    if (htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        htmlFile.write(html.toUtf8());
        htmlFile.close();
    }

    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setPageSize(QSizeF(210, 297) * 72.0 / 25.4); // A4 size in points
    doc.setHtml(html);

    QPdfWriter writer(fileName);
    writer.setPageSize(QPageSize::A4);
    writer.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);

    doc.print(&writer);

    QMessageBox::information(this, "Report Generated", QString("PDF saved to:\n%1\n\nHTML saved to:\n%2").arg(fileName).arg(htmlFileName));
}

void MainWindow::updateDeviceStatusBar()
{
    if (!m_deviceStatusLabel) return;

    if (m_serialController.isConnected()) {
        QString deviceName = m_serialController.deviceName().isEmpty() ? "Unknown" : m_serialController.deviceName();
        QString fwVersion = m_serialController.swVersion().isEmpty() ? "N/A" : m_serialController.swVersion();
        QString hwVersion = m_serialController.hwVersion().isEmpty() ? "N/A" : m_serialController.hwVersion();
        QString portName = m_serialController.portName().isEmpty() ? "N/A" : m_serialController.portName();

        QString statusText = QString("<span style='color: #45ba4a; font-size: 16px;'>●</span> Connected device: %1 on port %2 | Firmware ver: %3 | Hardware version: %4")
                                 .arg(deviceName)
                                 .arg(portName)
                                 .arg(fwVersion)
                                 .arg(hwVersion);

        m_deviceStatusLabel->setText(statusText);
        m_deviceStatusLabel->setStyleSheet("padding: 2px 8px;");
    } else {
        m_deviceStatusLabel->setText("<span style='color: #e21e1e; font-size: 16px;'>●</span> No device connected");
        m_deviceStatusLabel->setStyleSheet("padding: 2px 8px;");
    }
}

void MainWindow::onSaveSessionTriggered()
{
    // Interactive save helper
    saveSessionInteractive();
}

bool MainWindow::saveSessionInteractive()
{
    QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString defaultSession = docsPath + QDir::separator() + "session.json";
    QString fileName = QFileDialog::getSaveFileName(this, "Save Session", defaultSession, "JSON Files (*.json)");
    if (fileName.isEmpty())
        return false;

    QJsonObject session;

    // Save app version
    session["appVersion"] = APP_VERSION;

    // Save report data
    session["cameraModel"] = ui->cameraModelField->text();
    session["serialNumber"] = ui->serialNumberField->text();
    session["frameSizeIndex"] = ui->frameSizeComboBox->currentIndex();
    session["shutterTypeIndex"] = ui->shutterTypeComboBox->currentIndex();
    session["runsPerSpeedIndex"] = ui->runsPerSpeedComboBox->currentIndex();
    session["acceptanceThresholdIndex"] = ui->acceptanceSlider->value();
    session["notes"] = ui->notesTextEdit->toPlainText();

    // Save speed series - smart handling
    int seriesIndex = ui->speedSeriesComboBox->currentIndex();
    if (seriesIndex == 0 || seriesIndex == 1)
    {
        // Standard built-in series - save index
        session["speedSeriesIndex"] = seriesIndex;
    }
    else
    {
        // Custom series - save name and speeds string
        QString seriesName = ui->speedSeriesComboBox->itemText(seriesIndex);
        session["speedSeriesName"] = seriesName;

        // Find the speeds string for this custom series
        QJsonArray customSeries = loadCustomSeries();
        for (int i = 0; i < customSeries.size(); ++i)
        {
            QJsonObject obj = customSeries[i].toObject();
            if (obj["name"].toString() == seriesName)
            {
                session["speedSeriesSpeeds"] = obj["speeds"].toString();
                break;
            }
        }
    }

    // Save measurements data (only runs, no computed fields)
    QJsonArray rowsDataArray;
    for (int i = 0; i < m_rowsData.size(); ++i) {
        const RowData &rowData = m_rowsData[i];
        if (rowData.runs.isEmpty()) continue;

        QJsonObject rowObj;
        rowObj["rowIndex"] = i;

        QJsonArray runsArray;
        for (const MeasurementRun &run : rowData.runs) {
            QJsonObject runObj;
            runObj["sensor0Time"] = run.sensor0Time;
            runObj["sensor1Time"] = run.sensor1Time;
            runObj["curtain1spanAtime"] = run.curtain1spanAtime;
            runObj["curtain1spanAspeed"] = run.curtain1spanAspeed;
            runObj["curtain2spanAtime"] = run.curtain2spanAtime;
            runObj["curtain2spanAspeed"] = run.curtain2spanAspeed;
            runObj["slitWidthSensor0"] = run.slitWidthSensor0;
            runObj["slitWidthSensor1"] = run.slitWidthSensor1;
            runObj["slitWidthAverage"] = run.slitWidthAverage;
            runsArray.append(runObj);
        }
        rowObj["runs"] = runsArray;
        rowsDataArray.append(rowObj);
    }
    session["rowsData"] = rowsDataArray;

    // Write to file
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Error", QString("Cannot save session to:\n%1").arg(fileName));
        return false;
    }

    QJsonDocument doc(session);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    QMessageBox::information(this, "Session Saved", QString("Session saved to:\n%1").arg(fileName));

    m_sessionDirty = false;

    return true;
}

void MainWindow::onRestoreSessionTriggered()
{
    QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString fileName = QFileDialog::getOpenFileName(this, "Restore Session", docsPath, "JSON Files (*.json)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error", QString("Cannot open session file:\n%1").arg(fileName));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        QMessageBox::warning(this, "Error", "Invalid session file format.");
        return;
    }

    QJsonObject session = doc.object();

    // Restore report data
    ui->cameraModelField->setText(session["cameraModel"].toString());
    ui->serialNumberField->setText(session["serialNumber"].toString());
    ui->frameSizeComboBox->setCurrentIndex(session["frameSizeIndex"].toInt(0));
    ui->shutterTypeComboBox->setCurrentIndex(session["shutterTypeIndex"].toInt(0));
    ui->runsPerSpeedComboBox->setCurrentIndex(session["runsPerSpeedIndex"].toInt(0));
    ui->acceptanceSlider->setValue(session["acceptanceThresholdIndex"].toInt(1));
    ui->notesTextEdit->setPlainText(session["notes"].toString());

    // Restore speed series selection
    QStringList speeds;
    bool customSeriesFound = false;

    if (session.contains("speedSeriesName"))
    {
        // Custom series - find by name and speeds
        QString seriesName = session["speedSeriesName"].toString();
        QString seriesSpeeds = session["speedSeriesSpeeds"].toString();

        QJsonArray customSeries = loadCustomSeries();
        bool exactMatch = false;
        int matchIndex = -1;

        for (int i = 0; i < customSeries.size(); ++i)
        {
            QJsonObject obj = customSeries[i].toObject();
            if (obj["name"].toString() == seriesName)
            {
                if (obj["speeds"].toString() == seriesSpeeds)
                {
                    // Exact match found
                    exactMatch = true;
                    matchIndex = i;
                    break;
                }
                else if (matchIndex < 0)
                {
                    // Name match but different speeds - remember for fallback
                    matchIndex = i;
                }
            }
        }

        if (exactMatch)
        {
            // Exact match - select it
            int comboIndex = 2 + matchIndex; // 0=Old, 1=ISO, 2+=custom
            if (comboIndex < ui->speedSeriesComboBox->count())
            {
                ui->speedSeriesComboBox->setCurrentIndex(comboIndex);
                m_previousSpeedSeriesIndex = comboIndex;
                customSeriesFound = true;
            }
        }
        else
        {
            // No exact match — add temporary custom series without removing existing one
            saveCustomSeries(seriesName, seriesSpeeds, true);

            // Find the newly added series in the combo box (last one with this name)
            for (int i = ui->speedSeriesComboBox->count() - 1; i >= 0; --i)
            {
                if (ui->speedSeriesComboBox->itemText(i) == seriesName)
                {
                    ui->speedSeriesComboBox->setCurrentIndex(i);
                    m_previousSpeedSeriesIndex = i;
                    customSeriesFound = true;
                    break;
                }
            }
        }
    }
    else
    {
        // Standard built-in series - use index
        int speedSeriesIndex = session["speedSeriesIndex"].toInt(0);
        if (speedSeriesIndex >= 0 && speedSeriesIndex < ui->speedSeriesComboBox->count())
        {
            ui->speedSeriesComboBox->setCurrentIndex(speedSeriesIndex);
            m_previousSpeedSeriesIndex = speedSeriesIndex;
            customSeriesFound = true;
        }
    }

    if (!customSeriesFound)
    {
        QMessageBox::warning(this, "Warning", "Could not restore speed series. Using 'Old Style'.");
        ui->speedSeriesComboBox->setCurrentIndex(0);
        m_previousSpeedSeriesIndex = 0;
    }

    // Trigger table update for the selected series
    onSpeedSeriesComboBoxActivated(ui->speedSeriesComboBox->currentIndex());

    // Now get the speeds from the table that was just built
    int rowCount = ui->speedsTableWidget->rowCount();
    for (int i = 0; i < rowCount; ++i)
    {
        speeds.append(ui->speedsTableWidget->item(i, 0)->text());
    }

    // Restore measurements data (recompute avg/dev/status from runs)
    QJsonArray rowsDataArray = session["rowsData"].toArray();
    for (const QJsonValue &v : rowsDataArray) {
        QJsonObject rowObj = v.toObject();
        int rowIndex = rowObj["rowIndex"].toInt();

        if (rowIndex < 0 || rowIndex >= speeds.size()) continue;

        // Ensure m_rowsData is large enough
        if (m_rowsData.size() <= rowIndex) {
            m_rowsData.resize(rowIndex + 1);
        }

        RowData &rowData = m_rowsData[rowIndex];

        // Load runs
        QJsonArray runsArray = rowObj["runs"].toArray();
        for (const QJsonValue &rv : runsArray) {
            QJsonObject runObj = rv.toObject();
            MeasurementRun run;
            run.sensor0Time = runObj["sensor0Time"].toDouble();
            run.sensor1Time = runObj["sensor1Time"].toDouble();
            run.curtain1spanAtime = runObj["curtain1spanAtime"].toDouble();
            run.curtain1spanAspeed = runObj["curtain1spanAspeed"].toDouble();
            run.curtain2spanAtime = runObj["curtain2spanAtime"].toDouble();
            run.curtain2spanAspeed = runObj["curtain2spanAspeed"].toDouble();
            run.slitWidthSensor0 = runObj["slitWidthSensor0"].toDouble();
            run.slitWidthSensor1 = runObj["slitWidthSensor1"].toDouble();
            run.slitWidthAverage = runObj["slitWidthAverage"].toDouble();
            rowData.runs.append(run);
            m_rowMeasurements[rowIndex].append(run);
        }

        if (rowData.runs.isEmpty())
            continue;

        // Recompute avg0, avg1, avg from runs
        double sum0 = 0, sum1 = 0;
        for (const MeasurementRun &run : rowData.runs)
        {
            sum0 += run.sensor0Time;
            sum1 += run.sensor1Time;
        }
        rowData.avg0 = sum0 / rowData.runs.size();
        rowData.avg1 = sum1 / rowData.runs.size();
        rowData.avg = (rowData.avg0 + rowData.avg1) / 2.0;

        // Recompute devEV, devPct, status from speed text
        QString speedStr = speeds[rowIndex];
        double targetTimeMs = 0.0;
        if (speedStr.contains('/'))
        {
            QStringList parts = speedStr.split('/');
            if (parts.size() == 2 && parts[1].toDouble() != 0)
            {
                targetTimeMs = 1000.0 * parts[0].toDouble() / parts[1].toDouble();
            }
        }
        else
        {
            targetTimeMs = 1000.0 * speedStr.toDouble();
        }

        if (targetTimeMs > 0.0)
        {
            rowData.devPct = ((rowData.avg - targetTimeMs) / targetTimeMs) * 100.0;
            rowData.devEV = std::log2(rowData.avg / targetTimeMs);

            double threshold = getAcceptanceThresholdEV();
            if (std::abs(rowData.devEV) <= threshold)
            {
                rowData.status = "PASS";
            }
            else
            {
                rowData.status = "FAIL";
            }
        }

        // Update table display
        ui->speedsTableWidget->item(rowIndex, 1)->setText(formatSpeed(rowData.avg0));
        ui->speedsTableWidget->item(rowIndex, 2)->setText(formatSpeed(rowData.avg1));
        ui->speedsTableWidget->item(rowIndex, 3)->setText(formatSpeed(rowData.avg));
        ui->speedsTableWidget->item(rowIndex, 4)->setText(QString::number(rowData.devEV, 'f', 2));
        ui->speedsTableWidget->item(rowIndex, 5)->setText(QString::number(rowData.devPct, 'f', 2));
        ui->speedsTableWidget->item(rowIndex, 6)->setText(QString("%1/%1").arg(rowData.runs.size()));

        QTableWidgetItem *statusItem = ui->speedsTableWidget->item(rowIndex, 7);
        statusItem->setText(rowData.status);
        if (rowData.status == "PASS") {
            statusItem->setForeground(QBrush(QColor("green")));
            QFont font = statusItem->font();
            font.setBold(true);
            statusItem->setFont(font);
        } else if (rowData.status == "FAIL") {
            statusItem->setForeground(QBrush(QColor("red")));
            QFont font = statusItem->font();
            font.setBold(true);
            statusItem->setFont(font);
        }
    }

    QMessageBox::information(this, "Session Restored", QString("Session restored from:\n%1").arg(fileName));
    // Restored session is considered saved state
    m_sessionDirty = false;
}

void MainWindow::onExitTriggered()
{
    // Trigger window close, which will run the closeEvent handling
    close();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // If there are no measurement rows with data, allow close immediately
    bool hasMeasurements = false;
    for (const RowData &rd : m_rowsData)
    {
        if (!rd.runs.isEmpty())
        {
            hasMeasurements = true;
            break;
        }
    }

    if (!m_sessionDirty || !hasMeasurements)
    {
        event->accept();
        return;
    }

    QMessageBox msg(this);
    msg.setWindowTitle("Unsaved Session");
    msg.setText("You have unsaved session. Really Exit?");

    QAbstractButton *saveBtn = msg.addButton("Save Session", QMessageBox::AcceptRole);
    QAbstractButton *exitBtn = msg.addButton("Exit", QMessageBox::DestructiveRole);
    QAbstractButton *cancelBtn = msg.addButton("Cancel", QMessageBox::RejectRole);

    msg.exec();

    QAbstractButton *clicked = msg.clickedButton();
    if (clicked == saveBtn)
    {
        bool ok = saveSessionInteractive();
        if (ok)
        {
            event->accept();
            qApp->quit();
        }
        else
        {
            event->ignore();
        }
    }
    else if (clicked == exitBtn)
    {
        event->accept();
        qApp->quit();
    }
    else
    {
        event->ignore();
    }
}

void MainWindow::cleanupTemporaryCustomSeries()
{
    QJsonArray customSeries = loadCustomSeries();
    bool changed = false;

    for (int i = customSeries.size() - 1; i >= 0; --i)
    {
        QJsonObject obj = customSeries[i].toObject();
        if (obj["temporary"].toBool(false))
        {
            customSeries.removeAt(i);
            changed = true;
        }
    }

    if (changed)
    {
        m_settings.setValue("customSpeedSeriesJson", QString::fromUtf8(QJsonDocument(customSeries).toJson(QJsonDocument::Compact)));
    }

    // Clear old-style temporary list for backwards compatibility
    m_settings.remove("temporaryCustomSeries");
}

void MainWindow::onAboutTriggered()
{
    QMessageBox::about(this, QString("About Shutter Tester App v%1").arg(APP_VERSION),
        QString(
            "<h3>Shutter Tester ST-2 Client App v%1</h3>"
            "<p>A desktop application for the ST-2 shutter speed tester &mdash; "
            "designed for measuring and calibrating film camera shutters.</p>"
            "<p>Copyright &copy; 2026 Alexander Litvinov</p>"
            "<hr>"
            "<p><b>License</b></p>"
            "<p>This program is free software: you can redistribute it and/or modify "
            "it under the terms of the <b>GNU General Public License v3.0</b> "
            "as published by the Free Software Foundation.</p>"
            "<p>This program is distributed in the hope that it will be useful, "
            "but <b>WITHOUT ANY WARRANTY</b>; without even the implied warranty of "
            "<b>MERCHANTABILITY</b> or <b>FITNESS FOR A PARTICULAR PURPOSE</b>. See the "
            "<a href='https://www.gnu.org/licenses/gpl-3.0.html'>GNU GPLv3</a> for more details.</p>"
            "<p>Source code: <a href='https://github.com/Alexander077/Shutter-tester-ST-2-Client-App-Private'>GitHub</a></p>"
            "<hr>"
            "<p><b>Qt Framework Attribution</b></p>"
            "<p>This application uses the <a href='https://www.qt.io'>Qt framework</a>. "
            "Core and GUI modules are licensed under the LGPLv3, while the Qt Charts module is licensed under the GPLv3.</p>"
            "<p>Copyright &copy; The Qt Company Ltd and other licensors.<br>"
            "Qt source code is available at <a href='https://code.qt.io'>code.qt.io</a>.</p>"
        ).arg(APP_VERSION)
    );
}
