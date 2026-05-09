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
#include "LightSetupDialog.h"
#include "SerialController.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QJsonObject>
#include <QMessageBox>
#include <QFont>
#include <QFrame>

LightSetupDialog::LightSetupDialog(SerialController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
{
    setWindowTitle("Light Setup");
    setMinimumWidth(400);
    setModal(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- Title ---
    QLabel *titleLabel = new QLabel("Light Setup — Adjust lighting and observe real-time sensor levels");
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet("font-weight: bold; font-size: 12px; margin-bottom: 8px;");
    mainLayout->addWidget(titleLabel);

    // --- Overall Quality ---
    QHBoxLayout *qualityLayout = new QHBoxLayout();
    QLabel *qualityTitle = new QLabel("Overall Light Quality:");
    qualityTitle->setStyleSheet("font-weight: bold;");
    m_qualityLabel = new QLabel("Waiting for data...");
    m_qualityLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px 8px; border-radius: 4px;");
    qualityLayout->addWidget(qualityTitle);
    qualityLayout->addWidget(m_qualityLabel, 1);
    mainLayout->addLayout(qualityLayout);

    // --- Separator ---
    QFrame *separator1 = new QFrame();
    separator1->setFrameShape(QFrame::HLine);
    separator1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(separator1);

    // --- Sensor 1 ---
    QLabel *sensor1Title = new QLabel("Sensor 1");
    QFont sensorFont = sensor1Title->font();
    sensorFont.setBold(true);
    sensorFont.setPointSize(11);
    sensor1Title->setFont(sensorFont);
    mainLayout->addWidget(sensor1Title);

    m_sensor1Bar = new QProgressBar();
    m_sensor1Bar->setRange(0, 100);
    m_sensor1Bar->setValue(0);
    m_sensor1Bar->setTextVisible(true);
    m_sensor1Bar->setMinimumHeight(24);
    mainLayout->addWidget(m_sensor1Bar);

    m_sensor1StatusLabel = new QLabel("Waiting...");
    m_sensor1StatusLabel->setAlignment(Qt::AlignCenter);
    m_sensor1StatusLabel->setStyleSheet("padding: 2px; border-radius: 3px;");
    mainLayout->addWidget(m_sensor1StatusLabel);

    // --- Sensor 2 ---
    QLabel *sensor2Title = new QLabel("Sensor 2");
    sensor2Title->setFont(sensorFont);
    mainLayout->addWidget(sensor2Title);

    m_sensor2Bar = new QProgressBar();
    m_sensor2Bar->setRange(0, 100);
    m_sensor2Bar->setValue(0);
    m_sensor2Bar->setTextVisible(true);
    m_sensor2Bar->setMinimumHeight(24);
    mainLayout->addWidget(m_sensor2Bar);

    m_sensor2StatusLabel = new QLabel("Waiting...");
    m_sensor2StatusLabel->setAlignment(Qt::AlignCenter);
    m_sensor2StatusLabel->setStyleSheet("padding: 2px; border-radius: 3px;");
    mainLayout->addWidget(m_sensor2StatusLabel);

    // --- Spacer ---
    mainLayout->addStretch(1);

    // --- Close button ---
    m_closeButton = new QPushButton("Close");
    m_closeButton->setMinimumHeight(32);
    mainLayout->addWidget(m_closeButton);

    // --- Connections ---
    connect(m_closeButton, &QPushButton::clicked, this, &LightSetupDialog::onCloseClicked);
    connect(m_controller, &SerialController::lightSetupDataReceived,
            this, &LightSetupDialog::onLightSetupDataReceived);
    connect(m_controller, &SerialController::isConnectedChanged,
            this, &LightSetupDialog::onConnectedChanged);

    // Enter light setup mode
    m_controller->requestLightSetup();
}

LightSetupDialog::~LightSetupDialog()
{
    // Ensure abort is called when dialog is destroyed via X button
    if (m_controller && m_controller->isConnected()) {
        m_controller->abortOperation();
    }
}

void LightSetupDialog::onLightSetupDataReceived(const QJsonObject &data)
{
    int sensor1Level = data["sensor1Level"].toInt(0);
    int sensor2Level = data["sensor2Level"].toInt(0);
    QString sensor1Status = data["sensor1Status"].toString("LIGHT_STATUS_UNKNOWN");
    QString sensor2Status = data["sensor2Status"].toString("LIGHT_STATUS_UNKNOWN");
    QString lightQuality = data["lightQuality"].toString("LIGHT_QUALITY_UNKNOWN");

    updateSensorUI(1, sensor1Level, sensor1Status);
    updateSensorUI(2, sensor2Level, sensor2Status);
    updateOverallQuality(lightQuality);
}

void LightSetupDialog::updateSensorUI(int sensorIndex, int level, const QString &status)
{
    QProgressBar *bar = (sensorIndex == 1) ? m_sensor1Bar : m_sensor2Bar;
    QLabel *statusLabel = (sensorIndex == 1) ? m_sensor1StatusLabel : m_sensor2StatusLabel;

    bar->setValue(qBound(0, level, 100));

    QString statusText;
    QString statusStyle;

    if (status == "LIGHT_STATUS_OK") {
        statusText = QString("Sensor %1 — OK (%2%)").arg(sensorIndex).arg(level);
        statusStyle = "background-color: #4CAF50; color: white; font-weight: bold; padding: 2px; border-radius: 3px;";
        // Set progress bar to green
        bar->setStyleSheet(
            "QProgressBar { border: 1px solid #ccc; border-radius: 4px; text-align: center; }"
            "QProgressBar::chunk { background-color: #4CAF50; border-radius: 3px; }");
    } else if (status == "LIGHT_STATUS_TOO_DIM") {
        statusText = QString("Sensor %1 — TOO DIM (%2%)").arg(sensorIndex).arg(level);
        statusStyle = "background-color: #FF9800; color: white; font-weight: bold; padding: 2px; border-radius: 3px;";
        bar->setStyleSheet(
            "QProgressBar { border: 1px solid #ccc; border-radius: 4px; text-align: center; }"
            "QProgressBar::chunk { background-color: #FF9800; border-radius: 3px; }");
    } else if (status == "LIGHT_STATUS_TOO_BRIGHT") {
        statusText = QString("Sensor %1 — TOO BRIGHT (%2%)").arg(sensorIndex).arg(level);
        statusStyle = "background-color: #F44336; color: white; font-weight: bold; padding: 2px; border-radius: 3px;";
        bar->setStyleSheet(
            "QProgressBar { border: 1px solid #ccc; border-radius: 4px; text-align: center; }"
            "QProgressBar::chunk { background-color: #F44336; border-radius: 3px; }");
    } else {
        statusText = QString("Sensor %1 — Unknown (%2%)").arg(sensorIndex).arg(level);
        statusStyle = "background-color: #9E9E9E; color: white; font-weight: bold; padding: 2px; border-radius: 3px;";
        bar->setStyleSheet(
            "QProgressBar { border: 1px solid #ccc; border-radius: 4px; text-align: center; }"
            "QProgressBar::chunk { background-color: #9E9E9E; border-radius: 3px; }");
    }

    statusLabel->setText(statusText);
    statusLabel->setStyleSheet(statusStyle);
}

void LightSetupDialog::updateOverallQuality(const QString &quality)
{
    if (quality == "LIGHT_QUALITY_OK") {
        m_qualityLabel->setText("OK");
        m_qualityLabel->setStyleSheet(
            "font-weight: bold; font-size: 14px; padding: 4px 8px; "
            "border-radius: 4px; background-color: #4CAF50; color: white;");
    } else if (quality == "LIGHT_QUALITY_BAD") {
        m_qualityLabel->setText("BAD");
        m_qualityLabel->setStyleSheet(
            "font-weight: bold; font-size: 14px; padding: 4px 8px; "
            "border-radius: 4px; background-color: #F44336; color: white;");
    } else {
        m_qualityLabel->setText("UNKNOWN");
        m_qualityLabel->setStyleSheet(
            "font-weight: bold; font-size: 14px; padding: 4px 8px; "
            "border-radius: 4px; background-color: #FF9800; color: white;");
    }
}

void LightSetupDialog::onConnectedChanged()
{
    if (!m_controller->isConnected()) {
        QMessageBox::warning(this, "Connection Lost",
                             "Device disconnected while in Light Setup mode.\n"
                             "The dialog will close.");
        reject();
    }
}

void LightSetupDialog::onCloseClicked()
{
    if (m_controller && m_controller->isConnected()) {
        m_controller->abortOperation();
    }
    accept();
}