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

#include <QDialog>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

class SerialController;

class LightSetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LightSetupDialog(SerialController *controller, QWidget *parent = nullptr);
    ~LightSetupDialog();

private slots:
    void onLightSetupDataReceived(const QJsonObject &data);
    void onConnectedChanged();
    void onCloseClicked();

private:
    void updateSensorUI(int sensorIndex, int level, const QString &status);
    void updateOverallQuality(const QString &quality);

    SerialController *m_controller;

    // Sensor 1
    QProgressBar *m_sensor1Bar;
    QLabel *m_sensor1StatusLabel;

    // Sensor 2
    QProgressBar *m_sensor2Bar;
    QLabel *m_sensor2StatusLabel;

    // Overall
    QLabel *m_qualityLabel;

    QPushButton *m_closeButton;
};