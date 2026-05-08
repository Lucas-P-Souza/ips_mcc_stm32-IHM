#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"
#include <QDebug>
#include <QSerialPortInfo>
#include <QFile>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    serial = new QSerialPort(this);

    ui->boxPorta->clear();
    const auto portas = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : portas) {
        ui->boxPorta->addItem(info.systemLocation());
    }

    connect(ui->sliderPWM, &QSlider::valueChanged, this, [=](int valor){
        ui->labelPWM->setText(QString::number(valor) + "%");
    });

    // --- COLE ISSO AQUI: CRIANDO A LINHA DO CANAL I ---
    ui->graficoRPM->addGraph(); // Cria o graph(1) para o Canal I (Laranja)
    ui->graficoRPM->graph(0)->setPen(QPen(QColor(255, 140, 0), 2)); // Laranja escuro
    ui->graficoRPM->graph(0)->setAntialiased(true);

    // --- CONFIGURAÇÃO GRÁFICO RPM ---
    ui->graficoRPM->addGraph();
    ui->graficoRPM->graph(1)->setPen(QPen(Qt::blue, 2));
    ui->graficoRPM->graph(1)->setAntialiased(true);

    ui->graficoRPM->yAxis->setLabel("Velocidade (RPM)");
    ui->graficoRPM->xAxis->setLabel("Tempo (s)");
    ui->graficoRPM->yAxis->setRange(0, 4000);

    // --- TEMPORÁRIO: LINHA VERDE DE 63% ---
    linhaTau = new QCPItemStraightLine(ui->graficoRPM);
    linhaTau->setPen(QPen(Qt::darkGreen, 2, Qt::DashLine));
    linhaTau->point1->setCoords(0, 0);
    linhaTau->point2->setCoords(1, 0);

    // --- CONFIGURAÇÃO GRÁFICO CORRENTE ---
    ui->graficoCorrente->addGraph();
    ui->graficoCorrente->graph(0)->setPen(QPen(Qt::red, 2));
    ui->graficoCorrente->yAxis->setLabel("Corrente (A)");
    ui->graficoCorrente->yAxis->setRange(-0.2, 3.0);

    ui->graficoRPM->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->graficoCorrente->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    connect(serial, &QSerialPort::readyRead, this, &MainWindow::dadosRecebidos);
}

MainWindow::~MainWindow() { delete ui; }

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

            ui->graficoRPM->graph(0)->data()->clear();
            ui->graficoRPM->graph(1)->data()->clear();

            ui->graficoCorrente->graph(0)->data()->clear();

            tempo_conexao = -1.0;

            qDebug() << "Conectado com sucesso na porta:" << portaSelecionada;
        } else {
            qDebug() << "Erro ao abrir serial:" << serial->errorString();
        }
    }
}

void MainWindow::dadosRecebidos() {
    while (serial->canReadLine()) {
        QByteArray buffer = serial->readLine();
        QString linha = QString::fromLatin1(buffer).trimmed();
        QStringList valores = linha.split(",");

        if (valores.size() >= 5) {
            bool okT, okR, okA, okV, okRI;

            double t = valores[0].toDouble(&okT) / 1000.0;
            double rpm = qAbs(valores[1].toDouble(&okR));          // RPM original (A/B)
            double amp = valores[2].toDouble(&okA) / 1000.0;
            int voltas = valores[3].toInt(&okV);                    // Voltas completas
            double rpm_canal_i = qAbs(valores[4].toDouble(&okRI));  // Novo RPM do Canal I

            // Verifica se TODAS as 5 variáveis vieram corretas
            if (okT && okR && okA && okV && okRI) {

                // --- 1. TARA DO BOTÃO CONECTAR (Gráfico começa em zero) ---
                if (tempo_conexao < 0) {
                    tempo_conexao = t; // Captura o primeiro tempo que a STM32 mandou
                }
                double t_grafico = t - tempo_conexao; // Tempo que vai pro gráfico

                // --- 2. TARA DO BOTÃO ENVIAR (CSV e Voltas começam em zero) ---
                double t_csv = t_grafico; // Por padrão, copia o gráfico
                int voltas_csv = voltas;

                if (cronometro_rodando && esperando_inicio_degrau) {
                    tempo_zero = t_grafico; // Guarda o tempo exato do clique
                    voltas_zero = voltas;   // Guarda as voltas do clique
                    esperando_inicio_degrau = false;
                }

                if (cronometro_rodando) {
                    t = t - tempo_zero;
                    voltas = voltas - voltas_zero;
                }

                if (arquivoCSV.isOpen() && cronometro_rodando) {
                    QTextStream stream(&arquivoCSV);
                    // Escreve as variáveis separadas por vírgula
                    stream << t_csv << ","
                           << rpm << ","
                           << amp << ","
                           << voltas_csv << ","
                           << rpm_canal_i << "\n";
                }

                // --- TEMPORÁRIO: LÓGICA DO CRONÔMETRO TAU ---
                if (cronometro_rodando) {
                    // Sincroniza o tempo zero com o primeiro dado após o clique
                    if (esperando_inicio_degrau) {
                        tempo_zero = t;
                        voltas_zero = voltas;
                        esperando_inicio_degrau = false;
                        qDebug() << ">>> Degrau iniciado em t =" << tempo_zero;
                    }

                    // Verifica se atingiu 68% (1177.41 RPM)
                    if (rpm >= alvo_rpm_tau) {
                        float resultado_tau = t - tempo_zero;
                        qDebug() << "-----------------------------------";
                        qDebug() << "TAU MECÂNICO ENCONTRADO!";
                        qDebug() << "Tempo Final:" << t;
                        qDebug() << "TAU:" << resultado_tau << "segundos";
                        qDebug() << "-----------------------------------";
                        cronometro_rodando = false; // Para a medição
                    }
                }

                // --- TEMPORÁRIO: ATUALIZAÇÃO DA ALTURA DA LINHA VERDE ---
                if (rpm > max_rpm_teste) {
                    max_rpm_teste = rpm;
                    double altura_63 = max_rpm_teste * 0.68;
                    linhaTau->point1->setCoords(0, altura_63);
                    linhaTau->point2->setCoords(1, altura_63);
                }

                // --- ATUALIZAÇÃO DOS GRÁFICOS ---
                ui->graficoRPM->graph(1)->addData(t_grafico, rpm);
                ui->graficoRPM->graph(0)->addData(t_grafico, rpm_canal_i);
                ui->graficoCorrente->graph(0)->addData(t_grafico, amp);

                qDebug() << "[DEBUG] Tempo:" << QString::number(t_grafico, 'f', 2)
                         << "s | RPM A/B:" << rpm
                         << "| RPM REAL (Canal I):" << rpm_canal_i;

                ui->graficoRPM->xAxis->setRange(t_grafico, 5, Qt::AlignRight);
                ui->graficoCorrente->xAxis->setRange(t_grafico, 5, Qt::AlignRight);

                ui->lblVelocidade->setText(QString::number(rpm_canal_i, 'f', 1) + " RPM");
                ui->lblCorrente->setText(QString::number(amp, 'f', 2) + " A");

                if (rpm > ui->graficoRPM->yAxis->range().upper) {
                    ui->graficoRPM->yAxis->setRangeUpper(rpm * 1.2);
                }
                if (amp > ui->graficoCorrente->yAxis->range().upper && amp < 100) {
                    ui->graficoCorrente->yAxis->setRangeUpper(amp * 1.2);
                }
            }
        }
    }
    ui->graficoRPM->replot();
    ui->graficoCorrente->replot();
}

void MainWindow::on_btnEnviar_clicked() {
    if(serial->isOpen()) {
        serial->clear(QSerialPort::Output);

        // --- TEMPORÁRIO: RESET PARA NOVO TESTE DE TAU ---
        max_rpm_teste = 0.0;
        linhaTau->point1->setCoords(0, 0);
        linhaTau->point2->setCoords(1, 0);

        int pwm = ui->sliderPWM->value();

        // Dispara o cronômetro se o PWM for maior que zero (degrau)
        if (pwm > 0) {
            esperando_inicio_degrau = true;
            cronometro_rodando = true;
            qDebug() << "Cronômetro armado. Alvo:" << alvo_rpm_tau << "RPM";

            arquivoCSV.setFileName("dados_motor.csv");
            if(arquivoCSV.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream stream(&arquivoCSV);
                // Escreve o cabeçalho da tabela com todas as colunas!
                stream << "Tempo_s,RPM_AB,Corrente_A,Voltas,RPM_I\n";
            }
        }
        else {
            // Se o PWM for 0 (mandou parar), fecha e salva o arquivo no PC!
            if(arquivoCSV.isOpen()) {
                arquivoCSV.close();
                qDebug() << "Arquivo CSV salvo e fechado!";
            }
        }

        int dir = ui->radioHorario->isChecked() ? 1 : 0;
        QString cmd = QString("P%1,S%2\n").arg(pwm).arg(dir);
        serial->write(cmd.toLatin1());
        serial->flush();

        qDebug() << "ENVIADO PARA STM32 ->" << cmd.trimmed();
    }
}

void MainWindow::on_breakButton_toggled(bool checked) {
    if(serial->isOpen()) {
        serial->clear(QSerialPort::Output);
        QString cmd = QString("B%1\n").arg(checked ? 1 : 0);
        serial->write(cmd.toLatin1());
        serial->flush();

        if(arquivoCSV.isOpen()) {
            arquivoCSV.close();
            qDebug() << "Arquivo CSV salvo por Freio de Emergência!";
        }

        if (checked) {
            ui->breakButton->setText("TURN OFF BREAK");
        } else {
            ui->breakButton->setText("TURN ON BREAK");
        }
    }
}
