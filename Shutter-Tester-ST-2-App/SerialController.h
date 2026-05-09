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

#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QJsonObject>
#include <QFile>
#include <QTimer>
#include <memory>

class SerialController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList availablePorts READ availablePorts NOTIFY availablePortsChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY isConnectedChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString hwVersion READ hwVersion NOTIFY deviceInfoChanged)
    Q_PROPERTY(QString swVersion READ swVersion NOTIFY deviceInfoChanged)

public:
    explicit SerialController(QObject *parent = nullptr);
    ~SerialController();

    QStringList availablePorts() const;
    bool isConnected() const;
    QString portName() const;
    
    QString deviceName() const;
    QString hwVersion() const;
    QString swVersion() const;

public slots:
    void refreshPorts();
    void connectToPort(const QString &portName);
    void disconnectPort();
    void requestEcho();
    void requestLightSetup();
    void requestMeasurement(int sensorIndex, int curtainMovement);
    void abortOperation();
    void startFirmwareUpdate(const QString &filePath);
    void cancelFirmwareUpdate();

signals:
    void availablePortsChanged();
    void isConnectedChanged();
    void deviceInfoChanged();
    void errorOccurred(const QString &errorString);
    void echoResponseReceived(bool success, const QString &deviceName, const QString &hwVersion, const QString &swVersion);
    void measurementReceived(const QJsonObject &result);
    void lightSetupDataReceived(const QJsonObject &data);
    void firmwareUpdateProgress(qint64 bytesReceived, qint64 bytesTotal);
    void firmwareUpdateStatus(const QString &status);
    void firmwareUpdateFinished(bool success, const QString &message);

private slots:
    void handleReadyRead();
    void handleError(QSerialPort::SerialPortError error);
    void onAckTimeout();
    void onEchoTimeout();

private:
    enum class FirmwareState {
        Idle,
        WaitingReady,
        SendingChunks,
        WaitingAck,
        WaitingFinalStatus
    };

    QSerialPort m_serial;
    QStringList m_availablePorts;
    QString m_deviceName;
    QString m_hwVersion;
    QString m_swVersion;
    QByteArray m_readBuffer;
    FirmwareState m_firmwareState = FirmwareState::Idle;
    std::unique_ptr<QFile> m_firmwareFile;
    qint64 m_firmwareTotalSize = 0;
    qint64 m_firmwareSentBytes = 0;
    QTimer m_ackTimer;
    QTimer m_echoTimer;
    QString m_pendingPortName;
    QString m_connectedPortName;

    void processLine(const QByteArray &line);
    void sendNextFirmwareChunk();
    void finishFirmwareUpdate(bool success, const QString &message);
    void writeDataChunked(const QByteArray &data, int chunkSize = 32);
};