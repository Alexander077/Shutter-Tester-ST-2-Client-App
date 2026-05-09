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

#include "ConnectionDialog.h"
#include <QMessageBox>

// ComboBox subclass that emits a signal when the dropdown popup is about to show
class PortComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit PortComboBox(QWidget *parent = nullptr) : QComboBox(parent) {}

signals:
    void aboutToShowPopup();

protected:
    void showPopup() override {
        emit aboutToShowPopup();
        QComboBox::showPopup();
    }
};

ConnectionDialog::ConnectionDialog(SerialController *controller, bool isStartup, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_isStartup(isStartup)
{
    setWindowTitle(tr("Connect To Device"));
    setMinimumWidth(300);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    mainLayout->addWidget(new QLabel(tr("Select Port:"), this));

    m_portComboBox = new PortComboBox(this);
    m_portComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mainLayout->addWidget(m_portComboBox);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_connectButton = new QPushButton(tr("Connect"), this);
    m_connectButton->setDefault(true);
    
    m_skipCancelButton = new QPushButton(isStartup ? tr("Skip") : tr("Cancel"), this);

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_connectButton);
    buttonLayout->addWidget(m_skipCancelButton);
    mainLayout->addLayout(buttonLayout);

    // Refresh ports when the user opens the combo box dropdown
    QObject::connect(static_cast<PortComboBox*>(m_portComboBox), &PortComboBox::aboutToShowPopup,
                     m_controller, &SerialController::refreshPorts);

    connect(m_connectButton, &QPushButton::clicked, this, &ConnectionDialog::onConnectClicked);
    connect(m_skipCancelButton, &QPushButton::clicked, this, &ConnectionDialog::onSkipCancelClicked);
    
    connect(m_controller, &SerialController::availablePortsChanged, this, &ConnectionDialog::onAvailablePortsChanged);
    connect(m_controller, &SerialController::isConnectedChanged, this, &ConnectionDialog::onIsConnectedChanged);
    connect(m_controller, &SerialController::echoResponseReceived, this, &ConnectionDialog::onEchoResponseReceived);
    connect(m_controller, &SerialController::errorOccurred, this, &ConnectionDialog::onErrorOccurred);

    onAvailablePortsChanged();
    m_controller->refreshPorts();
}

void ConnectionDialog::onConnectClicked()
{
    QString portName = m_portComboBox->currentText();
    if (!portName.isEmpty()) {
        m_pendingPortName = portName;
        m_connectButton->setEnabled(false);
        m_controller->connectToPort(portName);
    } else {
        QMessageBox::warning(this, tr("Warning"), tr("Please select a port."));
    }
}

void ConnectionDialog::onSkipCancelClicked()
{
    reject();
}

void ConnectionDialog::onAvailablePortsChanged()
{
    m_portComboBox->clear();
    m_portComboBox->addItems(m_controller->availablePorts());
}

void ConnectionDialog::onIsConnectedChanged()
{
    // Connection state changed - waiting for echo response
    // The dialog will be closed/kept open based on echoResponseReceived
}

void ConnectionDialog::onEchoResponseReceived(bool success, const QString &deviceName, const QString &hwVersion, const QString &swVersion)
{
    m_connectButton->setEnabled(true);
    
    if (success) {
        // Show success message
        QString message = tr("Successfully connected to %1 device.\nDevice hardware version: %2\nDevice firmware version: %3")
            .arg(deviceName)
            .arg(hwVersion)
            .arg(swVersion);
        QMessageBox::information(this, tr("Connection Successful"), message);
        accept();
    } else {
        // Show failure message
        QString message = tr("No ST device found on %1.").arg(m_pendingPortName);
        QMessageBox::warning(this, tr("Connection Failed"), message);
        // Keep dialog open - user can try another port
        m_connectButton->setEnabled(true);
    }
}

void ConnectionDialog::onErrorOccurred(const QString &errorString)
{
    // Port failed to open - re-enable the button so user can try again
    m_connectButton->setEnabled(true);
    QMessageBox::warning(this, tr("Connection Error"), tr("Failed to open port: %1").arg(errorString));
}

#include "ConnectionDialog.moc"
