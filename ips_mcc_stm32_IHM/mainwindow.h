#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtSerialPort/QSerialPort>
#include "qcustomplot.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    void on_btnConectar_clicked();
    void on_btnEnviar_clicked();
    void dadosRecebidos();
    void on_breakButton_toggled(bool checked);

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;
    QFile arquivoCSV;

    QCPItemStraightLine *linhaTau;
    double max_rpm_teste = 0.0;
    bool esperando_inicio_degrau = false;
    bool cronometro_rodando = false;
    double tempo_zero = 0.0;
    int voltas_zero = 0;
    double tempo_conexao = -1.0;
    double alvo_rpm_tau = 1137.6;
};
#endif
