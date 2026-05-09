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
#include "SerialController.h"
#include <QSerialPortInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QThread>

SerialController::SerialController(QObject *parent)
    : QObject(parent)
{
    connect(&m_serial, &QSerialPort::readyRead, this, &SerialController::handleReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &SerialController::handleError);
    connect(&m_ackTimer, &QTimer::timeout, this, &SerialController::onAckTimeout);
    connect(&m_echoTimer, &QTimer::timeout, this, &SerialController::onEchoTimeout);
    m_ackTimer.setSingleShot(true);
    m_echoTimer.setSingleShot(true);
    refreshPorts();
}

SerialController::~SerialController()
{
    if (m_serial.isOpen()) {
        m_serial.close();
    }
}

QStringList SerialController::availablePorts() const
{
    return m_availablePorts;
}

bool SerialController::isConnected() const
{
    return m_serial.isOpen();
}

QString SerialController::portName() const
{
    return m_connectedPortName;
}

QString SerialController::deviceName() const
{
    return m_deviceName;
}

QString SerialController::hwVersion() const
{
    return m_hwVersion;
}

QString SerialController::swVersion() const
{
    return m_swVersion;
}

void SerialController::refreshPorts()
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ports << info.portName();
    }
    
    if (m_availablePorts != ports) {
        m_availablePorts = ports;
        emit availablePortsChanged();
    }
}

void SerialController::connectToPort(const QString &portName)
{
    if (m_serial.isOpen()) {
        m_serial.close();
        m_echoTimer.stop();
        emit isConnectedChanged();
    }

    m_pendingPortName = portName;
    m_serial.setPortName(portName);
    m_serial.setBaudRate(QSerialPort::Baud115200);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial.open(QIODevice::ReadWrite)) {
        // As per specs: setting DTR/RTS to false can prevent unwanted reboots
        m_serial.setDataTerminalReady(false);
        m_serial.setRequestToSend(false);

        m_readBuffer.clear();
        emit isConnectedChanged();
        
        // Reset device info
        m_deviceName.clear();
        m_hwVersion.clear();
        m_swVersion.clear();
        emit deviceInfoChanged();
        
        // Start echo timer (3 seconds timeout)
        m_echoTimer.start(3000);
        requestEcho();
    } else {
        emit errorOccurred(m_serial.errorString());
    }
}

void SerialController::disconnectPort()
{
    if (m_serial.isOpen()) {
        m_serial.close();
        m_echoTimer.stop();
        emit isConnectedChanged();
        
        m_deviceName.clear();
        m_hwVersion.clear();
        m_swVersion.clear();
        m_connectedPortName.clear();
        emit deviceInfoChanged();
    }
    if (m_firmwareState != FirmwareState::Idle) {
        m_ackTimer.stop();
        finishFirmwareUpdate(false, "Disconnected");
    }
}

void SerialController::requestLightSetup()
{
    if (!m_serial.isOpen()) return;

    QJsonObject obj;
    obj["cmd"] = "API_REQUEST_LIGHT_SETUP";
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";
    writeDataChunked(data);
}

void SerialController::requestEcho()
{
    if (!m_serial.isOpen()) return;

    QJsonObject obj;
    obj["cmd"] = "API_REQUEST_ECHO";
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";
    writeDataChunked(data);
}

void SerialController::requestMeasurement(int sensorIndex, int curtainMovement)
{
    if (!m_serial.isOpen()) return;

    QJsonObject obj;
    obj["cmd"] = "API_REQUEST_MEASURE";
    obj["sensorIndex"] = sensorIndex;
    obj["curtainMovement"] = curtainMovement;
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";
    writeDataChunked(data);
}

void SerialController::abortOperation()
{
    if (!m_serial.isOpen()) return;

    QJsonObject obj;
    obj["cmd"] = "API_REQUEST_ABORT_OPERATION";
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";
    writeDataChunked(data);
}

void SerialController::startFirmwareUpdate(const QString &filePath)
{
    if (m_firmwareState != FirmwareState::Idle) {
        emit errorOccurred("Firmware update already in progress");
        return;
    }
    if (!m_serial.isOpen()) {
        emit errorOccurred("Device not connected");
        return;
    }

    m_firmwareFile = std::make_unique<QFile>(filePath);
    if (!m_firmwareFile->open(QIODevice::ReadOnly)) {
        emit errorOccurred("Cannot open firmware file");
        m_firmwareFile.reset();
        return;
    }

    m_firmwareTotalSize = m_firmwareFile->size();
    m_firmwareSentBytes = 0;

    qDebug() << "[FW] Starting firmware update. File:" << filePath << "Size:" << m_firmwareTotalSize;
    emit firmwareUpdateStatus("Initializing firmware update...");
    emit firmwareUpdateProgress(0, m_firmwareTotalSize);

    QJsonObject obj;
    obj["cmd"] = "API_REQUEST_FIRMWARE_UPDATE";
    QJsonDocument doc(obj);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";
    writeDataChunked(data);

    m_firmwareState = FirmwareState::WaitingReady;
    m_ackTimer.start(15000);
}

void SerialController::cancelFirmwareUpdate()
{
    if (m_firmwareState != FirmwareState::Idle) {
        m_ackTimer.stop();
        finishFirmwareUpdate(false, "Cancelled by user");
    }
}

void SerialController::sendNextFirmwareChunk()
{
    if (!m_firmwareFile || !m_firmwareFile->isOpen()) {
        finishFirmwareUpdate(false, "Firmware file closed unexpectedly");
        return;
    }

    const int chunkSize = 48;
    QByteArray chunk = m_firmwareFile->read(chunkSize);
    if (chunk.isEmpty()) {
        qDebug() << "[FW] All chunks sent. Waiting for finalization...";
        m_firmwareState = FirmwareState::WaitingFinalStatus;
        m_ackTimer.start(30000);
        emit firmwareUpdateStatus("Finalizing firmware update...");
        return;
    }

    QByteArray b64 = chunk.toBase64() + "\n";
    m_serial.write(b64);
    m_serial.flush();

    m_firmwareSentBytes += chunk.size();
    m_firmwareState = FirmwareState::WaitingAck;
    m_ackTimer.start(5000);

    qDebug() << "[FW] Sent chunk. Total sent:" << m_firmwareSentBytes << "of" << m_firmwareTotalSize;
}

void SerialController::onAckTimeout()
{
    if (m_firmwareState == FirmwareState::WaitingReady) {
        finishFirmwareUpdate(false, "Timeout waiting for device ready signal");
    } else if (m_firmwareState == FirmwareState::WaitingAck) {
        finishFirmwareUpdate(false, "Timeout: device did not acknowledge chunk");
    } else if (m_firmwareState == FirmwareState::WaitingFinalStatus) {
        finishFirmwareUpdate(false, "Timeout waiting for final status");
    }
}

void SerialController::onEchoTimeout()
{
    // Echo timeout - no ST device responded
    m_echoTimer.stop();
    if (m_serial.isOpen()) {
        m_serial.close();
        emit isConnectedChanged();
    }
    m_deviceName.clear();
    m_hwVersion.clear();
    m_swVersion.clear();
    m_connectedPortName.clear();
    emit deviceInfoChanged();
    emit echoResponseReceived(false, QString(), QString(), QString());
}

void SerialController::finishFirmwareUpdate(bool success, const QString &message)
{
    m_ackTimer.stop();
    if (m_firmwareFile) {
        m_firmwareFile->close();
        m_firmwareFile.reset();
    }
    m_firmwareState = FirmwareState::Idle;
    qDebug() << "[FW] Finished:" << success << message;
    emit firmwareUpdateFinished(success, message);
}

void SerialController::writeDataChunked(const QByteArray &data, int chunkSize)
{
    for (int i = 0; i < data.size(); i += chunkSize) {
        int written = m_serial.write(data.mid(i, chunkSize));
        m_serial.flush();
        qDebug() << "[FW] writeDataChunked: wrote" << written << "bytes at offset" << i;
        if (i + chunkSize < data.size()) {
            QThread::msleep(20);
        }
    }
}

void SerialController::handleReadyRead()
{
    m_readBuffer.append(m_serial.readAll());
    
    int newlineIndex;
    while ((newlineIndex = m_readBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_readBuffer.left(newlineIndex).trimmed();
        m_readBuffer.remove(0, newlineIndex + 1);
        
        if (!line.isEmpty()) {
            processLine(line);
        }
    }
}

void SerialController::processLine(const QByteArray &line)
{
    // Ignore log lines that don't look like JSON
    if (!line.startsWith('{') || !line.endsWith('}')) {
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }
    
    QJsonObject obj = doc.object();
    QString cmd = obj["cmd"].toString();
    
    if (cmd == "API_REQUEST_ECHO") {
        m_echoTimer.stop();
        if (obj["status"].toString() == "API_RESPONSE_STATUS_OK") {
            m_deviceName = obj["deviceName"].toString();
            m_hwVersion = obj["hwVersion"].toString();
            m_swVersion = obj["swVersion"].toString();
            m_connectedPortName = m_pendingPortName;
            emit deviceInfoChanged();
            emit echoResponseReceived(true, m_deviceName, m_hwVersion, m_swVersion);
        } else {
            m_serial.close();
            m_connectedPortName.clear();
            emit isConnectedChanged();
            emit echoResponseReceived(false, QString(), QString(), QString());
        }
    } else if (cmd == "API_REQUEST_LIGHT_SETUP") {
        emit lightSetupDataReceived(obj);
    } else if (cmd == "API_REQUEST_MEASURE") {
        emit measurementReceived(obj);
    } else if (cmd == "API_REQUEST_FIRMWARE_UPDATE") {
        QString status = obj["status"].toString();
        qDebug() << "[FW] Received status:" << status << "state:" << static_cast<int>(m_firmwareState);
        if (m_firmwareState == FirmwareState::WaitingReady) {
            if (status == "API_RESPONSE_READY_FOR_FIRMWARE_UPDATE_DATA") {
                m_ackTimer.stop();
                m_firmwareState = FirmwareState::SendingChunks;
                emit firmwareUpdateStatus("Uploading firmware...");
                sendNextFirmwareChunk();
            } else if (status == "API_RESPONSE_STATUS_ERROR" || status == "API_RESPONSE_FIRMWARE_UPDATE_FAILED") {
                finishFirmwareUpdate(false, obj["message"].toString("Initialization failed"));
            }
        } else if (m_firmwareState == FirmwareState::WaitingAck) {
            if (status == "API_RESPONSE_FIRMWARE_UPDATE_CHUNK_ACK") {
                qint64 ackBytes = static_cast<qint64>(obj["bytesReceived"].toDouble(0));
                emit firmwareUpdateProgress(ackBytes, m_firmwareTotalSize);
                m_ackTimer.stop();
                m_firmwareState = FirmwareState::SendingChunks;
                sendNextFirmwareChunk();
            } else if (status == "API_RESPONSE_FIRMWARE_UPDATE_FAILED") {
                finishFirmwareUpdate(false, obj["message"].toString("Write failed"));
            }
        } else if (m_firmwareState == FirmwareState::WaitingFinalStatus) {
            if (status == "API_RESPONSE_FIRMWARE_UPDATE_SUCCESS") {
                emit firmwareUpdateProgress(m_firmwareTotalSize, m_firmwareTotalSize);
                finishFirmwareUpdate(true, "Firmware update successful. Restart the device to finish the update.");
            } else if (status == "API_RESPONSE_FIRMWARE_UPDATE_FAILED") {
                finishFirmwareUpdate(false, obj["message"].toString("Finalization failed"));
            }
        }
    }
}

void SerialController::handleError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::ResourceError) {
        emit errorOccurred(m_serial.errorString());
        disconnectPort();
    }
}