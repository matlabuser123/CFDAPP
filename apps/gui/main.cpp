#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include "SimulationController.hpp"

// P5-B -- GUI Solver Workflow: the GUI's own entry point, structurally
// parallel to apps/cli/main.cpp (parse a little bit of setup, then hand
// off) -- all solver orchestration lives in SimulationController /
// cfd::app::CaseSession / cfd::app::ProjectRunner, none of it here
// (section 0's "ONE SOLVER BACKEND").
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName("CFDApp");

  SimulationController controller;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("simulationController", &controller);

  // engine.load() is synchronous for a local qrc:// URL (Qt only defers
  // to an event-loop callback for a genuinely asynchronous source, e.g.
  // a network URL) -- so rootObjects() is already authoritative
  // immediately after this call, no QQmlApplicationEngine::
  // objectCreationFailed (Qt 6.4+, newer than this project's own Qt
  // 6.2 baseline) needed.
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }

  return QGuiApplication::exec();
}
