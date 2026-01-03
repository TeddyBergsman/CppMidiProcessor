#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QTextStream>
#include "PresetLoader.h"
#include "PresetData.h"
// No longer need <QDir>
#include <QCoreApplication>

#include "ireal/HtmlPlaylistParser.h"
#include "chart/IRealProgressionParser.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    // QSettings identity (needed for Preferences persistence)
    QCoreApplication::setOrganizationName("TeddyBergsman");
    QCoreApplication::setApplicationName("CppMidiProcessor");

    // Debug/dump mode: parse an iReal HTML and print chart/barline info, then exit.
    // Usage:
    //   CppMidiProcessor --dump-ireal /path/to/Favorites.html "What A Wonderful World"
    const QStringList args = QCoreApplication::arguments();
    const int dumpIdx = args.indexOf("--dump-ireal");
    if (dumpIdx >= 0) {
        QTextStream out(stdout);
        if (dumpIdx + 1 >= args.size()) {
            out << "Missing HTML path.\n";
            return 2;
        }
        const QString htmlPath = args[dumpIdx + 1];
        const QString titleNeedle = (dumpIdx + 2 < args.size()) ? args[dumpIdx + 2] : QString();

        const ireal::Playlist pl = ireal::HtmlPlaylistParser::parseFile(htmlPath);
        if (pl.songs.isEmpty()) {
            out << "No songs found in playlist.\n";
            return 3;
        }

        const ireal::Song* chosen = nullptr;
        for (const auto& s : pl.songs) {
            if (titleNeedle.isEmpty() || s.title.contains(titleNeedle, Qt::CaseInsensitive)) {
                chosen = &s;
                break;
            }
        }
        if (!chosen) {
            out << "No song matched: " << titleNeedle << "\n";
            return 4;
        }

        out << "Song: " << chosen->title << "\n";
        out << "Progression tail: " << chosen->progression.right(220) << "\n";

        const chart::ChartModel model = chart::parseIRealProgression(chosen->progression);
        out << "Lines: " << model.lines.size() << "  timeSig=" << model.timeSigNum << "/" << model.timeSigDen << "\n";
        if (!model.footerText.isEmpty()) {
            out << "Footer: " << model.footerText << "\n";
        }

        // Dump last 2 lines' bars and barlines.
        const int startLine = std::max(0, int(model.lines.size()) - 2);
        for (int li = startLine; li < model.lines.size(); ++li) {
            const auto& line = model.lines[li];
            out << "Line[" << li << "] section=" << line.sectionLabel << " bars=" << line.bars.size() << "\n";
            for (int bi = 0; bi < line.bars.size(); ++bi) {
                const auto& bar = line.bars[bi];
                out << "  Bar[" << bi << "] L='" << bar.barlineLeft << "' R='" << bar.barlineRight << "' "
                    << "endStart=" << bar.endingStart << " endEnd=" << bar.endingEnd << " ann='" << bar.annotation << "'\n";
                out << "    cells:";
                for (const auto& cell : bar.cells) out << " [" << cell.chord << "]";
                out << "\n";
            }
        }
        return 0;
    }

    // --- Path to find the resource inside the executable ---
    // The ":/" prefix tells QFile to look inside the embedded resources
    // that were compiled from resources.qrc.
    QString presetPath = ":/preset.xml";

    // Load the preset from the embedded resource.
    PresetLoader loader;
    Preset preset = loader.loadPreset(presetPath);

    // If the preset is not valid, show an error and exit.
    if (!preset.isValid) {
        QMessageBox::critical(nullptr, "Fatal Error", "Could not load or parse the embedded preset.xml resource. The application cannot start.");
        return 1;
    }

    // Initialize the main components with the loaded preset data.
    MainWindow w(preset);
    
    w.show();

    return a.exec();
}