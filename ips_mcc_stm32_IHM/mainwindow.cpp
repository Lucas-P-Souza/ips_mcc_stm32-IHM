#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"
#include <QDebug>
#include <QSerialPortInfo>
#include <QFile>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // ========================================================================
    // BLOCK 1: Serial port initialization and initial UI state
    // ========================================================================
    serial = new QSerialPort(this);

    // Scan system for available COM/ttyACM ports
    ui->boxPorta->clear();
    const auto portas = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portas) {
        ui->boxPorta->addItem(info.systemLocation());
    }

    // Ensure UI initializes in Open Loop (PWM) mode by default
    ui->sliderPWM->setEnabled(true);
    ui->speedBox->setEnabled(false);

    // Update PWM percentage label when slider moves
    connect(ui->sliderPWM, &QSlider::valueChanged, this, [=](int valor){
        ui->labelPWM->setText(QString::number(valor) + " %");
    });

    // Connect UART data reception to telemetry handler
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::dadosRecebidos);

    // ========================================================================
    // BLOCK 2: RPM (Speed) graph configuration
    // ========================================================================
    // Graph 0 (Orange): Absolute RPM from encoder Index (Channel I)
    ui->graficoRPM->addGraph();
    ui->graficoRPM->graph(0)->setPen(QPen(QColor(255, 140, 0), 2));
    ui->graficoRPM->graph(0)->setAntialiased(true);

    // Graph 1 (Blue): RPM from quadrature encoder (Channels A/B)
    ui->graficoRPM->addGraph();
    ui->graficoRPM->graph(1)->setPen(QPen(Qt::blue, 2));
    ui->graficoRPM->graph(1)->setAntialiased(true);

    ui->graficoRPM->yAxis->setLabel("Velocidade (RPM)");
    ui->graficoRPM->xAxis->setLabel("Tempo (s)");
    ui->graficoRPM->yAxis->setRange(0, 1000);
    ui->graficoRPM->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // ========================================================================
    // BLOCK 3: Current graph configuration
    // ========================================================================
    // Graph 0 (Red): Current sensor data (ADC)
    ui->graficoCorrente->addGraph();
    ui->graficoCorrente->graph(0)->setPen(QPen(Qt::red, 2));

    ui->graficoCorrente->yAxis->setLabel("Corrente (A)");
    ui->graficoCorrente->xAxis->setLabel("Tempo (s)");
    ui->graficoCorrente->yAxis->setRange(0, 1.5);
    ui->graficoCorrente->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // ========================================================================
    // BLOCK 4: Step response (Tau) reference line configuration
    // ========================================================================
    // Dashed green horizontal line for visual target reference
    linhaTau = new QCPItemStraightLine(ui->graficoRPM);
    linhaTau->setPen(QPen(Qt::darkGreen, 2, Qt::DashLine));
    linhaTau->point1->setCoords(0, 0);
    linhaTau->point2->setCoords(1, 0);
}

MainWindow::~MainWindow() {
    delete ui;
}

// ========================================================================
// BLOCK 5: Control mode switching (ComboBox)
// ========================================================================
void MainWindow::on_controleBox_currentIndexChanged(int index) {
    // Reset all actuators for safety during mode transition
    ui->sliderPWM->setValue(0);
    ui->speedBox->setValue(0);

    if (index == 0) {
        // --- OPEN LOOP MODE (PWM) ---
        ui->sliderPWM->setEnabled(true);
        ui->speedBox->setEnabled(false);
        qDebug() << "Modo selecionado: Malha Aberta (Atuador direto por PWM)";
    } else {
        // --- CLOSED LOOP MODE (Speed Asservissement) ---
        ui->sliderPWM->setEnabled(false);
        ui->speedBox->setEnabled(true);
        qDebug() << "Modo selecionado: Asservissement (Referência por VITESSE)";
    }
}

// ========================================================================
// BLOCK 6: Serial connection management
// ========================================================================
void MainWindow::on_btnConectar_clicked() {
    if(serial->isOpen()) {
        serial->close();
        ui->btnConectar->setText("Conectar");
    } else {
        QString portaSelecionada = ui->boxPorta->currentText();
        serial->setPortName(portaSelecionada);
        serial->setBaudRate(QSerialPort::Baud115200);

        if(serial->open(QIODevice::ReadWrite)) {
            ui->btnConectar->setText("Déconnecter");
            serial->clear();

            // Clear plots for a new clean capture
            ui->graficoRPM->graph(0)->data()->clear();
            ui->graficoRPM->graph(1)->data()->clear();
            ui->graficoCorrente->graph(0)->data()->clear();

            tempo_conexao = -1.0; // Signal plot time offset reset

            qDebug() << "Conectado com sucesso na porta:" << portaSelecionada;
        } else {
            qDebug() << "Erro ao abrir serial:" << serial->errorString();
        }
    }
}

// ========================================================================
// BLOCK 7: UART telemetry parsing
// ========================================================================
void MainWindow::dadosRecebidos() {
    while (serial->canReadLine()) {
        QByteArray buffer = serial->readLine();

        // 'linha' = string holding the trimmed incoming serial data line
        QString linha = QString::fromLatin1(buffer).trimmed();

        // 'valores' = string list containing parsed CSV tokens from the serial packet
        QStringList valores = linha.split(",");

        // Data integrity validation: STM32 string must have 5 columns
        if (valores.size() >= 5) {
            bool okT, okR, okA, okV, okRI;

            // Convert raw UART data strings into engineering units
            // t = raw MCU time converted from ms to seconds
            double t = valores[0].toDouble(&okT) / 1000.0;
            // amp = raw current converted from mA to Amperes
            double amp = valores[2].toDouble(&okA) / 1000.0;
            // voltas = accumulated mechanical turn counter from MCU
            int voltas = valores[3].toInt(&okV);

            // rpm & rpm_canal_i = raw values scaled by an 11/10 transmission ratio correction factor
            double rpm = qAbs(valores[1].toDouble(&okR)) * 11/10;
            double rpm_canal_i = qAbs(valores[4].toDouble(&okRI)) * 11/10;

            if (okT && okR && okA && okV && okRI) {

                // 'tempo_conexao' = baseline timestamp anchor used to tare the graph axis to 0s on startup
                if (tempo_conexao < 0) {
                    tempo_conexao = t;
                }
                // 't_grafico' = relative elapsed time in seconds for real-time plotting
                double t_grafico = t - tempo_conexao;

                // ========================================================================
                // STEP RESPONSE (TAU) ANALYSIS
                // ========================================================================
                // 'cronometro_rodando' = flag tracking if a step-response transient test is active
                // 'esperando_inicio_degrau' = flag waiting for the exact inflection timestamp of the step command
                if (cronometro_rodando && esperando_inicio_degrau) {
                    tempo_zero = t_grafico; // Capture step test start execution time
                    esperando_inicio_degrau = false;
                    qDebug() << ">>> Cálculo do Tau iniciado em t =" << tempo_zero;
                }

                if (cronometro_rodando) {
                    // Mechanical Tau analytical stop condition (reached 63.2% of target speed 'alvo_rpm_tau')
                    if (rpm >= alvo_rpm_tau) {
                        float resultado_tau = t_grafico - tempo_zero;
                        qDebug() << "-----------------------------------";
                        qDebug() << "TAU MECÂNICO ENCONTRADO!";
                        qDebug() << "TAU:" << resultado_tau << "segundos";
                        qDebug() << "-----------------------------------";
                        cronometro_rodando = false;
                    }
                }

                // ========================================================================
                // INDEPENDENT CSV RECORDING
                // ========================================================================
                // 'gravando_csv' = flag tracking if filesystem data logging is active
                if (gravando_csv && arquivoCSV.isOpen()) {
                    // 'esperando_inicio_csv' = tares time and turn counters on the first logged packet
                    if (esperando_inicio_csv) {
                        tempo_zero_csv = t_grafico; // Save file baseline time tare
                        voltas_zero_csv = voltas;   // Save file baseline turn tare
                        esperando_inicio_csv = false;
                    }

                    // Normalize temporal and mechanical variables to start strictly at zero inside the CSV file
                    double t_csv_indep = t_grafico - tempo_zero_csv;
                    int voltas_csv_indep = voltas - voltas_zero_csv;

                    QTextStream stream(&arquivoCSV);
                    stream << t_csv_indep << ","
                           << rpm << ","
                           << amp << ","
                           << voltas_csv_indep << ","
                           << rpm_canal_i << "\n";
                }

                // --- RPM PLOT UPDATE ---
                ui->graficoRPM->graph(1)->addData(t_grafico, rpm);         // Blue curve (A/B quadrature)
                ui->graficoRPM->graph(0)->addData(t_grafico, rpm_canal_i); // Orange curve (Index Channel I)
                ui->graficoRPM->xAxis->setRange(t_grafico, 5, Qt::AlignRight); // 5-second moving window

                ui->lblVelocidadeAB->setText(QString::number(rpm, 'f', 1) + " RPM -> AB");
                ui->lblVelocidadeAB->setText(QString::number(rpm_canal_i, 'f', 1) + " RPM -> I");

                if (rpm > ui->graficoRPM->yAxis->range().upper) {
                    ui->graficoRPM->yAxis->setRangeUpper(rpm * 1.2);
                }

                // --- CURRENT PLOT UPDATE ---
                ui->graficoCorrente->graph(0)->addData(t_grafico, amp);
                ui->graficoCorrente->xAxis->setRange(t_grafico, 5, Qt::AlignRight);
                ui->lblCorrente->setText(QString::number(amp, 'f', 2) + " A");

                if (amp > ui->graficoCorrente->yAxis->range().upper && amp < 100) {
                    ui->graficoCorrente->yAxis->setRangeUpper(amp * 1.2);
                }
            }
        }
    }

    ui->graficoRPM->replot();
    ui->graficoCorrente->replot();
}

// ========================================================================
// BLOCK 8: Actuation command transmission
// ========================================================================
void MainWindow::on_btnEnviar_clicked() {
    if(serial->isOpen()) {
        serial->clear(QSerialPort::Output);

        int valor_alvo = 0; // Target value accumulator (can represent PWM % or Target RPM)
        int dir = ui->radioHorario->isChecked() ? 1 : 0;
        QString cmd;

        if (ui->controleBox->currentIndex() == 0) {
            valor_alvo = ui->sliderPWM->value();
            cmd = QString("P%1,S%2\n").arg(valor_alvo).arg(dir);
        } else {
            valor_alvo = ui->speedBox->value();
            cmd = QString("V%1,S%2\n").arg(valor_alvo).arg(dir);
        }

        // ========================================================================
        // TRIGGER TAU ANALYTICS (Math only, no file handling)
        // ========================================================================
        if (ui->controleBox->currentIndex() == 1) {
            // If in Closed Loop (Asservissement), place the reference line at exact target coordinate
            linhaTau->point1->setCoords(0, valor_alvo);
            linhaTau->point2->setCoords(1, valor_alvo);
        } else {
            // If in Open Loop (PWM), hide the reference line out of view at zero
            linhaTau->point1->setCoords(0, 0);
            linhaTau->point2->setCoords(1, 0);
        }

        ui->graficoRPM->replot(); // Force immediate plot update to render line change

        if (valor_alvo > 0) {
            esperando_inicio_degrau = true;
            cronometro_rodando = true;
            qDebug() << "Análise de Tau armada. Alvo analítico:" << alvo_rpm_tau << "RPM";
        } else {
            cronometro_rodando = false;
        }

        serial->write(cmd.toLatin1());
        serial->flush();

        qDebug() << "ENVIADO PARA STM32 ->" << cmd.trimmed();
    }
}

// ========================================================================
// BLOCK 9: Emergency break safety interlock
// ========================================================================
void MainWindow::on_breakButton_toggled(bool checked) {
    if(serial->isOpen()) {
        serial->clear(QSerialPort::Output);

        if (checked) {
            ui->sliderPWM->setValue(0);
            ui->speedBox->setValue(0);
            ui->breakButton->setText("TURN OFF BREAK");

            // --- SAFETY: Auto-disable recording button if active ---
            if (ui->btnGravar->isChecked()) {
                ui->btnGravar->setChecked(false); // Automatically triggers Block 10 to close the file safely
            }
        } else {
            ui->breakButton->setText("TURN ON BREAK");
        }

        QString cmd = QString("B%1\n").arg(checked ? 1 : 0);
        serial->write(cmd.toLatin1());
        serial->flush();
    }
}

// ========================================================================
// BLOCK 10: CSV recording toggle control
// ========================================================================
void MainWindow::on_btnGravar_toggled(bool checked) {
    if (checked) {
        // Initialize and create test CSV file
        arquivoCSV.setFileName("dados_motor.csv");
        if(arquivoCSV.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&arquivoCSV);
            stream << "Tempo_s,RPM_AB,Corrente_A,Voltas,RPM_I\n"; // File log header

            gravando_csv = true;
            esperando_inicio_csv = true; // Force time tare on next incoming UART packet

            ui->btnGravar->setText("Parar Gravação");
            qDebug() << ">>> GRAVAÇÃO INICIADA (Arquivo dados_motor.csv criado)";
        } else {
            qDebug() << "❌ ERRO: Não foi possível criar o arquivo dados_motor.csv";
            ui->btnGravar->setChecked(false); // Force button back to off state
        }
    } else {
        // Cleanly finalize and save file to disk
        gravando_csv = false;
        if(arquivoCSV.isOpen()) {
            arquivoCSV.close();
            qDebug() << ">>> GRAVAÇÃO FINALIZADA (Arquivo dados_motor.csv salvo com sucesso!)";
        }
        ui->btnGravar->setText("Gravar CSV");
    }
}
