#include "ContentCase.h"
#include "ContentCaseExecution.h"
#include <QUuid>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>

namespace ContentCase {
namespace {
bool inside(QString root, QString path) {
    root = QDir::cleanPath(root).replace('\\', '/');
    path = QDir::cleanPath(path).replace('\\', '/');
    return path.compare(root, Qt::CaseInsensitive) == 0 ||
           path.startsWith(root + "/", Qt::CaseInsensitive);
}
bool outputPath(const QString &root, const QString &input, QString &path, QString &error) {
    if (input.isEmpty())
        return true;
    const QFileInfo info(input);
    if (info.exists() || info.isSymLink() || info.isJunction()) {
        error = "Output already exists (never overwritten): " + input;
        return false;
    }
    const QFileInfo directory(info.absolutePath());
    const QString canonical = directory.canonicalFilePath();
    if (canonical.isEmpty() || !directory.isDir()) {
        error = "Output parent directory must already exist: " + input;
        return false;
    }
    path = QDir(canonical).filePath(info.fileName());
    if (inside(root, path)) {
        error = "Output must be outside the game root: " + input;
        return false;
    }
    return true;
}
bool writeNew(const QString &path, const QByteArray &bytes, QString &error) {
    if (path.isEmpty())
        return true;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        error = "Cannot create new output: " + path + ": " + file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        error = "Output write failed: " + path;
        return false;
    }
    return true;
}
} // namespace
int run(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    auto fail = [&](const QString &message) {
        err << "Content case: " << message << "\n";
        return 2;
    };
    QString root, planInput, reportInput, applyInput, rollbackInput, journalInput;
    bool plan = false, help = false, mode = false, verify = false;
    const auto args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--contentcase") {
            mode = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            help = true;
            continue;
        }
        if (arg == "--verify") {
            if (verify)
                return fail("Duplicate --verify");
            verify = true;
            continue;
        }
        if (arg == "--plan") {
            if (plan)
                return fail("Duplicate --plan");
            plan = true;
            if (!root.isEmpty() && i + 1 < args.size() && !args[i + 1].startsWith('-'))
                planInput = args[++i];
            continue;
        }
        if (arg == "--report" || arg == "--apply" || arg == "--rollback" || arg == "--journal") {
            QString &target = arg == "--report"     ? reportInput
                              : arg == "--apply"    ? applyInput
                              : arg == "--rollback" ? rollbackInput
                                                    : journalInput;
            if (!target.isEmpty() || i + 1 >= args.size() || args[i + 1].startsWith('-'))
                return fail(arg + " requires one path");
            target = args[++i];
            continue;
        }
        if (arg == "--") {
            if (!root.isEmpty() || i + 2 != args.size())
                return fail("Supply one game root");
            root = args[++i];
            continue;
        }
        if (arg.startsWith('-'))
            return fail("Unknown option: " + arg);
        if (!root.isEmpty())
            return fail("Supply one game root; optional plan output belongs after --plan");
        root = arg;
    }
    if (help) {
        out << "Usage: TSRE5vc --contentcase <gameroot> [--journal new-directory] [--report "
               "new-report.md]\n"
               "       TSRE5vc --contentcase <gameroot> --plan [new-plan.json] [--report "
               "new-report.md]\n"
               "       TSRE5vc --contentcase <gameroot> --apply plan.json [--journal "
               "new-directory]\n"
               "       TSRE5vc --contentcase <gameroot> --verify [--report new-report.md]\n"
               "       TSRE5vc --contentcase <gameroot> --rollback journal.json\n"
               "Default invocation repairs supported components, with backups and a recovery "
               "journal.\n"
               "--plan and --verify are read-only. Existing output files are never overwritten.\n"
               "Journal directories must be new, outside gameroot, and on the same filesystem.\n"
               "Unresolved components and directory collisions are retained and reported.\n"
               "Exit 0: requested operation completed; 1: unresolved/partial result; 2: usage or "
               "execution error.\n";
        return 0;
    }
    if (!mode)
        return fail("Use --contentcase <gameroot>");
    if (int(plan) + int(verify) + int(!applyInput.isEmpty()) + int(!rollbackInput.isEmpty()) > 1)
        return fail("Choose one of --plan, --apply, --verify, --rollback");
    if ((plan || verify || !rollbackInput.isEmpty()) && !journalInput.isEmpty())
        return fail("--journal is only for execution");
    if (!rollbackInput.isEmpty() && !reportInput.isEmpty())
        return fail("--report is not supported with rollback");
    const QFileInfo rootInfo(root);
    if (root.isEmpty() || !rootInfo.isDir())
        return fail("Supply an existing game-root directory");
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    QString planPath, reportPath, error;
    if (!outputPath(canonicalRoot, planInput, planPath, error) ||
        !outputPath(canonicalRoot, reportInput, reportPath, error))
        return fail(error);
    if (!planPath.isEmpty() && planPath.compare(reportPath, Qt::CaseInsensitive) == 0)
        return fail("Plan and report must have different paths");
    auto progress = [&](const QString &message) {
        err << message << "\n";
        err.flush();
    };
    if (!rollbackInput.isEmpty()) {
        if (!rollback(canonicalRoot, rollbackInput, error, progress))
            return fail(error);
        out << "Rollback completed.\n";
        return 0;
    }
    if (!plan && !verify) {
        QJsonObject saved;
        if (!applyInput.isEmpty()) {
            QFile f(applyInput);
            if (!f.open(QIODevice::ReadOnly))
                return fail("Cannot read plan: " + applyInput);
            QJsonParseError parse;
            const auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
                doc.object()["schema"] != "tsre-content-case-plan-v1")
                return fail("Invalid plan JSON");
            saved = doc.object();
        }
        if (journalInput.isEmpty())
            journalInput = QFileInfo(canonicalRoot).absolutePath() + "/TSRE-case-" +
                           QUuid::createUuid().toString(QUuid::Id128);
        const auto result = execute(canonicalRoot, saved, journalInput, error, progress);
        if (result.isEmpty())
            return fail(error);
        if (!reportPath.isEmpty()) {
            QString checked;
            if (!outputPath(canonicalRoot, reportPath, checked, error))
                return fail(error);
            auto cell = [](QString s) {
                return s.replace('|', "\\|").replace('\n', ' ').replace('\r', ' ');
            };
            QString report = "# Content case conversion\n\nStatus: **" +
                             result["status"].toString() + "**\n\nJournal: `" +
                             result["journal"].toString() + "`\n\n";
            report += "## Errors\n\n| Source | Reason |\n| --- | --- |\n";
            for (const auto &v : result["errors"].toArray()) {
                const auto f = v.toObject();
                QStringList examples;
                for (const auto &x : f["examples"].toArray())
                    examples << x.toString();
                report += "| " + cell(f["path"].toString()) + " | " +
                          cell(f["code"].toString() + ": " + f["reason"].toString() + "; " +
                               examples.join("; ")) +
                          " |\n";
            }
            for (const auto &v : result["writerErrors"].toArray()) {
                const auto f = v.toObject();
                report += "| " + cell(f["path"].toString()) +
                          " | Reference edit withheld: " + cell(f["reason"].toString()) + " |\n";
            }
            report += "\n## Withheld operations\n\n| Operation | Source | Reason |\n| --- | --- | "
                      "--- |\n";
            for (const auto &v : result["skippedOperations"].toArray()) {
                const auto op = v.toObject();
                report += "| " + cell(op["operation"].toString()) + " | " +
                          cell(op["from"].toString(op["location"].toString())) + " | " +
                          cell(op["executionReason"].toString()) + " |\n";
            }
            report += "\n## Missing content warnings\n\n";
            for (const auto &v : result["missingTargets"].toArray()) {
                const auto f = v.toObject();
                report += "- " + cell(f["path"].toString()) + ": ";
                for (const auto &x : f["examples"].toArray())
                    report += cell(x.toString()) + "; ";
                report += '\n';
            }
            report +=
                "\nApplied steps: " + QString::number(result["actionsApplied"].toInt()) +
                ". Remaining proposals: " + QString::number(result["remainingOperations"].toInt()) +
                ".\n\n"
                "Unsupported reference coverage remains uncertified. Exact operations, backups, "
                "and recovery events are in the journal directory.\n";
            if (!writeNew(reportPath, report.toUtf8(), error))
                return fail(error);
        }
        auto summary = result;
        summary.remove("errors");
        summary.remove("missingTargets");
        summary.remove("skippedOperations");
        summary.remove("writerErrors");
        summary["skippedOperationCount"] = result["skippedOperations"].toArray().size();
        summary["writerErrorCount"] = result["writerErrors"].toArray().size();
        out << QJsonDocument(summary).toJson(QJsonDocument::Indented);
        return result["status"] == "complete" || (result["status"] == "no-changes" &&
                                                  result["preExistingErrors"].toInt() == 0 &&
                                                  result["skippedOperations"].toArray().isEmpty() &&
                                                  result["writerErrors"].toArray().isEmpty() &&
                                                  result["remainingOperations"].toInt() == 0)
                   ? 0
                   : 1;
    }
    const auto result = scan(canonicalRoot, error, progress);
    if (result.isEmpty())
        return fail(error);
    QString checked;
    if (!outputPath(canonicalRoot, planPath, checked, error) ||
        !outputPath(canonicalRoot, reportPath, checked, error))
        return fail(error);
    if ((!planPath.isEmpty() &&
         !writeNew(planPath, QJsonDocument(result).toJson(QJsonDocument::Compact), error)) ||
        (!reportPath.isEmpty() && !writeNew(reportPath, markdownReport(result).toUtf8(), error)))
        return fail(error);
    const int mismatches =
        result["summary"].toObject()["referenceStatuses"].toObject()["case-mismatch"].toInt();
    QJsonObject summary{{"gameRoot", canonicalRoot},
                        {"summary", result["summary"]},
                        {"inventorySha256", result["inventorySha256"]},
                        {"inspectedContentSha256", result["inspectedContentSha256"]},
                        {"coverageCertified", false},
                        {"applyReady", false}};
    if (verify) {
        summary["verification"] = "Exact component spelling checked by inventory enumeration; "
                                  "unsupported coverage remains uncertified";
        summary["remainingOperations"] = result["operations"].toArray().size();
    }
    out << QJsonDocument(summary).toJson(QJsonDocument::Indented);
    return result["summary"].toObject()["failedCases"].toInt() > 0 ||
                   (verify && (mismatches > 0 || !result["operations"].toArray().isEmpty()))
               ? 1
               : 0;
}
} // namespace ContentCase
