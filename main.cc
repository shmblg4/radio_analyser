#include <MainWindow.hpp>
#include <QApplication>
#include <QStyleFactory>
#include <radio_scanner.hpp>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    a.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow w;
    w.applyTheme(true);
    w.show();

    return a.exec();
}