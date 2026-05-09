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
#include "FirmwareUpdateDialog.h"
#include "SerialController.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>

FirmwareUpdateDialog::FirmwareUpdateDialog(SerialController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
{
    setWindowTitle("Firmware Update");
    setMinimumWidth(450);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // File selection row
    QHBoxLayout *fileLayout = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(this);
    m_filePathEdit->setPlaceholderText("Select firmware .bin file...");
    m_filePathEdit->setReadOnly(true);

    m_browseButton = new QPushButton("Browse...", this);
    connect(m_browseButton, &QPushButton::clicked, this, &FirmwareUpdateDialog::onBrowseClicked);

    fileLayout->addWidget(m_filePathEdit, 1);
    fileLayout->addWidget(m_browseButton);
    mainLayout->addLayout(fileLayout);

    // Progress
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);

    // Status label
    m_statusLabel = new QLabel("Ready", this);
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    // Buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_updateButton = new QPushButton("Update Firmware", this);
    connect(m_updateButton, &QPushButton::clicked, this, &FirmwareUpdateDialog::onUpdateClicked);
    btnLayout->addWidget(m_updateButton);

    QPushButton *closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(closeButton);

    mainLayout->addLayout(btnLayout);

    // Controller connections
    if (m_controller) {
        connect(m_controller, &SerialController::firmwareUpdateProgress, this, &FirmwareUpdateDialog::onProgress);
        connect(m_controller, &SerialController::firmwareUpdateStatus, this, &FirmwareUpdateDialog::onStatus);
        connect(m_controller, &SerialController::firmwareUpdateFinished, this, &FirmwareUpdateDialog::onFinished);
        connect(m_controller, &SerialController::isConnectedChanged, this, &FirmwareUpdateDialog::onConnectedChanged);
    }

    onConnectedChanged();
}

FirmwareUpdateDialog::~FirmwareUpdateDialog() = default;

void FirmwareUpdateDialog::onBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Select Firmware File", QString(), "Firmware Files (*.bin)");
    if (!filePath.isEmpty()) {
        m_filePathEdit->setText(filePath);
    }
    onConnectedChanged();
}

void FirmwareUpdateDialog::onUpdateClicked()
{
    QString filePath = m_filePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a firmware file.");
        return;
    }

    if (!m_controller || !m_controller->isConnected()) {
        QMessageBox::warning(this, "Error", "Device not connected.");
        return;
    }

    m_updateButton->setEnabled(false);
    m_browseButton->setEnabled(false);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Starting...");

    m_controller->startFirmwareUpdate(filePath);
}

void FirmwareUpdateDialog::onProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal > 0) {
        int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressBar->setValue(percent);
    }
}

void FirmwareUpdateDialog::onStatus(const QString &status)
{
    m_statusLabel->setText(status);
}

void FirmwareUpdateDialog::onFinished(bool success, const QString &message)
{
    m_updateButton->setEnabled(true);
    m_browseButton->setEnabled(true);

    if (success) {
        QMessageBox::information(this, "Firmware Update", message);
    } else {
        QMessageBox::critical(this, "Firmware Update Failed", message);
    }

    m_statusLabel->setText("Ready");
}

void FirmwareUpdateDialog::onConnectedChanged()
{
    bool canUpdate = m_controller && m_controller->isConnected() && !m_filePathEdit->text().isEmpty();
    m_updateButton->setEnabled(canUpdate);
}
