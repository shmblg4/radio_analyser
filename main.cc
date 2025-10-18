#include <MainWindow.hpp>
#include <QApplication>
#include <radio_scanner.hpp>

#define CENTER_FREQ_MHZ 434e6
#define SAMPLE_RATE_MHZ 2e6
#define BANDWIDTH_MHZ 2e6

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}