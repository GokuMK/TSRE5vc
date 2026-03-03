/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <QApplication>
#include <QDebug>
#include <QtCore>
#include <QFile>
#include <QTextStream>
#include <QPalette>
#include <QColor>
#include <QStringList>
#include <iostream>
#include <tsre/Game.h>
#include <routeEditor/RouteEditorWindow.h>
#include <routeEditor/LoadWindow.h>
#include <conEditor/CELoadWindow.h>
#include <shapeViewer/ShapeViewerWindow.h>
#include <tsre/geo/MapWindow.h>
#include <routeEditor/RouteEditorServer.h>
#include <routeEditor/RouteEditorClient.h>
#include <tsre/Undo.h>
#include <tsre/tests/TestRunner.h>

QFile logFile;
QTextStream logFileOut;

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg){
    Q_UNUSED(context);
    char symbol = '?';
    switch (type) {
        case QtDebugMsg: symbol = 'I'; break;
        case QtInfoMsg: symbol = 'I'; break;
        case QtWarningMsg: symbol = 'E'; break;
        case QtCriticalMsg: symbol = '!'; break;
        case QtFatalMsg: symbol = 'X'; break;
        default: symbol = '?'; break;
    }
    QString output = QString("[%1] %2").arg(symbol).arg(msg);
    if(Game::consoleOutput)
        std::cout << output.toStdString() << "\n";
    logFileOut << output << "\n";
    logFileOut.flush();
    logFile.flush(); 
    
    if( type == QtFatalMsg ) abort(); 
}

void LoadConEditor(){
    CELoadWindow* ceLoadWindow = new CELoadWindow();
    ceLoadWindow->show();
}

void LoadShapeViewer(QString arg){
    ShapeViewerWindow* shapeWindow = new ShapeViewerWindow();
    if(arg.length() > 0)
        shapeWindow->loadFile(arg);
    shapeWindow->show();
}

void LoadRouteEditor(){
    if(Game::serverLogin.length() > 0)
        Game::ServerMode = true;
    
    if(Game::ServerMode){
        Game::useQuadTree = true;
        Undo::UndoEnabled = false;
        // Create Server Client
        Game::serverClient = new RouteEditorClient();
    }
    
    RouteEditorWindow *window = new RouteEditorWindow();
    if(Game::fullscreen){
        window->setWindowFlags(Qt::CustomizeWindowHint);
        window->setWindowState(Qt::WindowMaximized);
    } else {
        window->resize(1280, 800);
    }
        
    if(!Game::ServerMode){
        LoadWindow *loadWindow = new LoadWindow();
        QObject::connect(window, SIGNAL(exitNow()), loadWindow, SLOT(exitNow()));
        QObject::connect(loadWindow, SIGNAL(showMainWindow()), window, SLOT(showRoute()));

        if(Game::checkRoot(Game::root) && (Game::checkRoute(Game::route) || Game::createNewRoutes)){
            window->showRoute();
        } else {
            loadWindow->show();
        }
    } else {
        QObject::connect(Game::serverClient, SIGNAL(loadRoute()), window, SLOT(showRoute()));
        Game::serverClient->connectNow();
    }
}

void RunRouteEditorServer(){
    Game::loadAllWFiles = true;
    Game::gui = false;
    RouteEditorServer *server = new RouteEditorServer();
    //..server->run();
}

enum CommandLineParseResult {
    CommandLineOk,
    CommandLineError,
    CommandLineVersionRequested,
    CommandLineHelpRequested
};

QHash<QString, QString> consoleArgs;

CommandLineParseResult parseCommandLineArgs(QCommandLineParser &parser){
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    const QCommandLineOption helpOption = parser.addHelpOption();
    const QCommandLineOption versionOption = parser.addVersionOption();
    const QCommandLineOption ServerIpOption("ip", "Server IP address.", "ip");
    parser.addOption(ServerIpOption);
    const QCommandLineOption ServerPortOption("port", "Server Port.", "port");
    parser.addOption(ServerPortOption);
    const QCommandLineOption FileOption("file", "Optional file to load with shapeview or play.", "file");
    parser.addOption(FileOption);
    const QCommandLineOption ShapeViewOption("shapeview", "Run ShapeViewer.");
    parser.addOption(ShapeViewOption);
    const QCommandLineOption RouteOption("route", "Route to run.", "file");
    parser.addOption(RouteOption);
    const QCommandLineOption AceConvOption("aceconv", "Run Ace Converter.");
    parser.addOption(AceConvOption);
    const QCommandLineOption ConEditOption("conedit", "Run Consist Editor.");
    parser.addOption(ConEditOption);
    const QCommandLineOption PlayOption("play", "Play Activity.");
    parser.addOption(PlayOption);
    const QCommandLineOption ServerOption("server", "Run Editor Server.");
    parser.addOption(ServerOption);

    // Tests (headless)
    const QCommandLineOption TestOption("test", "Run TSRE test runner and exit.");
    parser.addOption(TestOption);
    const QCommandLineOption TestListOption("test-list", "List available test suites and exit.");
    parser.addOption(TestListOption);
    const QCommandLineOption TestSuiteOption("test-suite", "Test suite to run (e.g. flex).", "name");
    parser.addOption(TestSuiteOption);
    const QCommandLineOption TestCasesOption("test-cases", "Path to test cases JSONL file.", "file");
    parser.addOption(TestCasesOption);
    const QCommandLineOption TestVerboseOption("test-verbose", "Verbose test output.");
    parser.addOption(TestVerboseOption);

    // Flex capture logging (JSONL)
    const QCommandLineOption FlexLogOption("flex-log", "Capture Flex inputs/outputs to a JSONL file.");
    parser.addOption(FlexLogOption);
    const QCommandLineOption FlexLogFileOption("flex-log-file", "Flex JSONL output file path.", "file");
    parser.addOption(FlexLogFileOption);
    const QCommandLineOption FlexLogCandidatesOption("flex-log-candidates", "Also log all valid Flex candidates (can be large).");
    parser.addOption(FlexLogCandidatesOption);
    
    if (!parser.parse(QCoreApplication::arguments())) {
        return CommandLineError;
    }
    
    
    if (parser.isSet(versionOption))
        return CommandLineVersionRequested;

    if (parser.isSet(helpOption))
        return CommandLineHelpRequested;

    if (parser.isSet(ServerIpOption)) {
        const QString ip = parser.value(ServerIpOption);
        consoleArgs["IP"] = ip;
    }
    if (parser.isSet(ServerPortOption)) {
        const QString port = parser.value(ServerPortOption);
        consoleArgs["PORT"] = port;
    }
    if (parser.isSet(RouteOption)) {
        const QString route = parser.value(RouteOption);
        consoleArgs["ROUTE"] = route;
    }
    if (parser.isSet(FileOption)) {
        const QString file = parser.value(FileOption);
        consoleArgs["FILENAME"] = file;
    }
    
    if (parser.isSet(ShapeViewOption)) {
        consoleArgs["SV"] = "TRUE";
    }
    if (parser.isSet(AceConvOption)) {
        consoleArgs["ACE"] = "TRUE";
    }
    if (parser.isSet(ConEditOption)) {
        consoleArgs["CON"] = "TRUE";
    }
    if (parser.isSet(PlayOption)) {
        consoleArgs["PLAY"] = "TRUE";
    }
    if (parser.isSet(ServerOption)) {
        consoleArgs["SERVER"] = "TRUE";
    }

    if (parser.isSet(TestOption)) {
        consoleArgs["TEST"] = "TRUE";
    }
    if (parser.isSet(TestListOption)) {
        consoleArgs["TEST_LIST"] = "TRUE";
    }
    if (parser.isSet(TestSuiteOption)) {
        consoleArgs["TEST_SUITE"] = parser.value(TestSuiteOption);
    }
    if (parser.isSet(TestCasesOption)) {
        consoleArgs["TEST_CASES"] = parser.value(TestCasesOption);
    }
    if (parser.isSet(TestVerboseOption)) {
        consoleArgs["TEST_VERBOSE"] = "TRUE";
    }

    if (parser.isSet(FlexLogOption)) {
        consoleArgs["FLEX_LOG"] = "TRUE";
    }
    if (parser.isSet(FlexLogFileOption)) {
        consoleArgs["FLEX_LOG_FILE"] = parser.value(FlexLogFileOption);
    }
    if (parser.isSet(FlexLogCandidatesOption)) {
        consoleArgs["FLEX_LOG_CANDS"] = "TRUE";
    }
    
    return CommandLineOk;
}

int main(int argc, char *argv[]){

   // #ifdef  Q_OS_WIN32 
   //     ::ShowWindow( ::GetConsoleWindow(), SW_HIDE ); //hide console window
   // #endif

    QLocale lepsze(QLocale::English);
    //loc.setNumberOptions(lepsze.numberOptions());
    QLocale::setDefault(lepsze);
        
    QSurfaceFormat format;
//#ifdef __APPLE__
//    format.setVersion(3, 3);
//    format.setProfile(QSurfaceFormat::CoreProfile);
//#endif
    //format.setDepthBufferSize(32);
    //format.setStencilBufferSize(8);
    format.setSamples(Game::AASamples);
    //format.set
    format.setSwapInterval(0);
    //format.setSwapBehavior(QSurfaceFormat::TripleBuffer);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);
    //QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true); // has no effect?
    QApplication::setApplicationName(Game::AppName);
    QApplication::setApplicationVersion(Game::AppVersion);
    //QApplication::pr
    QApplication app(argc, argv);
    
    QString workingDir = QDir::currentPath();
    
    if(!Game::UseWorkingDir){
        QDir::setCurrent(QCoreApplication::applicationDirPath());
    }
    
    workingDir = QDir::currentPath();
    workingDir.replace("/build", "");
    QDir::setCurrent(workingDir);
    
    Game::load();
        
    logFile.setFileName("log.txt");
    if(logFile.open(QIODevice::WriteOnly)){
        logFileOut.setDevice(&logFile);
    } else {
        qDebug() << "Cannot open log file for writing!";
    }
    
    qInstallMessageHandler( myMessageOutput );
    
    qDebug() << "workingDir" << workingDir;
    
    QCommandLineParser parser;
    switch (parseCommandLineArgs(parser)) {
        case CommandLineOk:
            break;
        case CommandLineError:
            return 1;
        case CommandLineVersionRequested:
            printf("%s %s\n", qPrintable(QCoreApplication::applicationName()),
                   qPrintable(QCoreApplication::applicationVersion()));
            return 0;
        case CommandLineHelpRequested:
            parser.showHelp();
            Q_UNREACHABLE();
    }

    // Apply runtime-only CLI toggles.
    if (consoleArgs["FLEX_LOG"] == "TRUE" || consoleArgs["FLEX_LOG_CANDS"] == "TRUE" || consoleArgs["FLEX_LOG_FILE"].length() > 0) {
        Game::flexLogEnabled = true;
    }
    if (consoleArgs["FLEX_LOG_CANDS"] == "TRUE") {
        Game::flexLogCandidates = true;
    }
    if (consoleArgs["FLEX_LOG_FILE"].length() > 0) {
        Game::flexLogFile = consoleArgs["FLEX_LOG_FILE"];
    }

    // Test runner (headless) - runs and exits without starting the GUI.
    if (consoleArgs["TEST_LIST"] == "TRUE") {
        Game::gui = false;
        Game::consoleOutput = true;
        const QStringList suites = TsreTests::listSuites();
        std::cout << "Available test suites:\n";
        for (const QString &s : suites)
            std::cout << "  " << s.toStdString() << "\n";
        return 0;
    }
    if (consoleArgs["TEST"] == "TRUE") {
        Game::gui = false;
        Game::consoleOutput = true;
        TsreTests::TestRunOptions opts;
        opts.suite = consoleArgs["TEST_SUITE"];
        opts.casesFile = consoleArgs["TEST_CASES"];
        opts.verbose = (consoleArgs["TEST_VERBOSE"] == "TRUE");
        return TsreTests::run(opts);
    }

    //app.set
    Game::PixelRatio = app.devicePixelRatio();
    qDebug() << "devicePixelRatio"<< app.devicePixelRatio();
    app.setStyle(QStyleFactory::create("Fusion"));
    if(!Game::systemTheme){
        //app.setStyle(QStyleFactory::create("Fusion"));
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53,53,53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(25,25,25));
        darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53,53,53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(240, 130, 0));
        darkPalette.setColor(QPalette::Highlight, QColor(240, 130, 0));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text , QColor(153,153,153));
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText , QColor(153,153,153));
        app.setPalette(darkPalette);
        app.setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
        app.setStyleSheet("QPushButton:checked{background-color: #666666;} ");
        Game::StyleMainLabel = "#c4a480";
        Game::StyleGreenButton = "#008800";
        Game::StyleRedButton = "#880000";
        Game::StyleYellowButton = "#888800";
        Game::StyleGreenText = "#55FF55";
        Game::StyleRedText = "#FF5555";
    } else {
        QPalette palette = app.palette();
        palette.setColor(QPalette::Disabled, QPalette::Text , QColor(160,90,64));
        palette.setColor(QPalette::Disabled, QPalette::WindowText , QColor(160,90,64));
        palette.setColor(QPalette::Highlight, QColor(160, 90, 64));
        palette.setColor(QPalette::Inactive, QPalette::HighlightedText, Qt::white);
        app.setPalette(palette);
        app.setStyleSheet("QPushButton:checked{background-color: #e0c0a4;} ");
    }
    
    Game::InitAssets();
    
    //Game::window.resize(1280, 720);
    //window.resize(window.sizeHint());
     
    //int desktopArea = QApplication::desktop()->width() *
    //                  QApplication::desktop()->height();
    //int widgetArea = window.width() * window.height();
    //if (((float)widgetArea / (float)desktopArea) < 1.0f)
    //    window.show();
    //else
    //    window.showMaximized();
    
    //Check if file opened with "open in TSRE"
    QStringList args = app.arguments();
    if(args.count() == 2){
        if(QFileInfo::exists(args[1])){
            consoleArgs["SV"] = "TRUE";
            consoleArgs["FILENAME"] = args[1];
        }
    }
    //////////////////////////////////////////
    //qDebug() << "arg1 " << args[1];    
    if(consoleArgs["ROUTE"].length() > 0){
        Game::route = consoleArgs["ROUTE"];
    }
    if(consoleArgs["IP"].length() > 0){
        RouteEditorServer::IP = consoleArgs["IP"];
    }
    if(consoleArgs["PORT"].length() > 0){
        RouteEditorServer::Port = consoleArgs["PORT"].toInt();
        qDebug() << RouteEditorServer::Port ;
    }
    
    if(consoleArgs["ACE"] == "TRUE"){
        // Run ace converter
        qDebug() << "Run ace converter";
        return app.exec();
    }
    if(consoleArgs["CON"] == "TRUE"){
        // Run ace converter
        qDebug() << "Run con editor";
        LoadConEditor();
        return app.exec();
    }
    if(consoleArgs["SV"] == "TRUE"){
        // Run ace converter
        qDebug() << "Run shape viewer";
        LoadShapeViewer(consoleArgs["FILENAME"]);
        return app.exec();
    }
    if(consoleArgs["PLAY"] == "TRUE"){
        // Play
        if(consoleArgs["FILENAME"].length() > 0)
            Game::ActivityToPlay = consoleArgs["FILENAME"];
        else
            Game::ActivityToPlay = "#";
        qDebug() << "Play" << Game::route << Game::ActivityToPlay;
    }
    if(consoleArgs["SERVER"] == "TRUE"){
        Game::checkRoute(Game::route);
        qDebug() << "Run server";
        RunRouteEditorServer();
        return app.exec();
    }

    //LoadConEditor();
    //LoadShapeViewer(consoleArgs["FILENAME"]);
    //return app.exec();
    // Run route editor
    LoadRouteEditor();

    //MapWindow aaa;
    //aaa.show();
    return app.exec();
 }
