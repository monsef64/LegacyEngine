#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "editor/script_editor.hpp"
#include "scripting/lua_console.hpp"

#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QPushButton>
#include <QSyntaxHighlighter>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ui {
namespace qt {

namespace {

// A small Lua highlighter: keywords, strings, numbers and comments. The old
// editor coloured the same tokens while drawing text itself.
class LuaHighlighter : public QSyntaxHighlighter {
  public:
    explicit LuaHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
        keyword_.setForeground(QColor(197, 134, 192));
        keyword_.setFontWeight(QFont::DemiBold);
        string_.setForeground(QColor(152, 195, 121));
        number_.setForeground(QColor(209, 154, 102));
        comment_.setForeground(QColor(106, 153, 85));
        comment_.setFontItalic(true);
        global_.setForeground(QColor(97, 175, 239));
        static const QStringList keywords{
            QStringLiteral("and"),   QStringLiteral("break"),  QStringLiteral("do"),
            QStringLiteral("else"),  QStringLiteral("elseif"), QStringLiteral("end"),
            QStringLiteral("false"), QStringLiteral("for"),    QStringLiteral("function"),
            QStringLiteral("goto"),  QStringLiteral("if"),     QStringLiteral("in"),
            QStringLiteral("local"), QStringLiteral("nil"),    QStringLiteral("not"),
            QStringLiteral("or"),    QStringLiteral("repeat"), QStringLiteral("return"),
            QStringLiteral("then"),  QStringLiteral("true"),   QStringLiteral("until"),
            QStringLiteral("while")};
        for (const auto& keyword : keywords) {
            rules_.push_back({QRegularExpression(QStringLiteral("\\b%1\\b").arg(keyword)),
                              keyword_});
        }
        rules_.push_back({QRegularExpression(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"")), string_});
        rules_.push_back({QRegularExpression(QStringLiteral("'(?:[^'\\\\]|\\\\.)*'")), string_});
        rules_.push_back(
            {QRegularExpression(QStringLiteral("\\b[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\\b")),
             number_});
        rules_.push_back({QRegularExpression(QStringLiteral("--\\[\\[.*?\\]\\]")), comment_});
        rules_.push_back({QRegularExpression(QStringLiteral("--[^\n]*")), comment_});
        rules_.push_back(
            {QRegularExpression(QStringLiteral(
                 "\\b(?:scene|input|levels|log|events|math|os|string|table|pairs|ipairs|print)\\b")),
             global_});
    }

  protected:
    void highlightBlock(const QString& text) override {
        for (const auto& [pattern, format] : rules_) {
            auto match = pattern.globalMatch(text);
            while (match.hasNext()) {
                const auto found = match.next();
                setFormat(found.capturedStart(), found.capturedLength(), format);
            }
        }
        // Multi-line comments and long strings continue to the next block.
        static const QRegularExpression commentStart(QStringLiteral("--\\[\\["));
        static const QRegularExpression commentEnd(QStringLiteral("\\]\\]"));
        if (previousBlockState() != 1 && text.contains(commentStart)) {
            const int start = text.indexOf(commentStart) + 4;
            const int end = text.indexOf(commentEnd, start);
            if (end < 0) {
                setFormat(start, text.size() - start, comment_);
                setCurrentBlockState(1);
            } else {
                setFormat(start, end - start, comment_);
            }
        } else if (previousBlockState() == 1) {
            const int end = text.indexOf(commentEnd);
            if (end < 0) {
                setFormat(0, text.size(), comment_);
                setCurrentBlockState(1);
            } else {
                setFormat(0, end + 2, comment_);
            }
        }
    }

  private:
    QTextCharFormat keyword_, string_, number_, comment_, global_;
    std::vector<std::pair<QRegularExpression, QTextCharFormat>> rules_;
};

} // namespace

ScriptPanel::ScriptPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(metrics::spacing);
    auto* save = new QToolButton(this);
    save->setText(tr("Save"));
    save->setToolTip(tr("Save the script (Ctrl+S)"));
    bar->addWidget(save);
    auto* run = new QToolButton(this);
    run->setText(tr("Run"));
    run->setToolTip(tr("Execute the script against the selected object"));
    bar->addWidget(run);
    pathLabel_ = new QLabel(tr("No script open"), this);
    pathLabel_->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    bar->addWidget(pathLabel_, 1);
    layout->addLayout(bar);

    code_ = new QPlainTextEdit(this);
    code_->setObjectName(QStringLiteral("ScriptCode"));
    code_->setPlaceholderText(
        tr("-- Lua script\nfunction start()\n    log('Hello from ' .. object.name)\nend"));
    code_->setTabStopDistance(32);
    QFont monospace(QStringLiteral("monospace"));
    monospace.setStyleHint(QFont::Monospace);
    monospace.setPointSizeF(10.5);
    code_->setFont(monospace);
    new LuaHighlighter(code_->document());
    layout->addWidget(code_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    connect(code_, &QPlainTextEdit::textChanged, this, [this]() {
        if (!scripts_ || path_.isEmpty()) {
            return;
        }
        const bool dirty = code_->toPlainText() !=
                           QString::fromStdString(std::string(scripts_->source()));
        if (dirty != dirty_) {
            dirty_ = dirty;
            if (dirty) {
                scripts_->markDirty();
            }
            pathLabel_->setText(QStringLiteral("%1%2").arg(path_, dirty_ ? "*" : ""));
        }
    });
    connect(save, &QToolButton::clicked, this, [this]() { saveCurrent(); });
    connect(run, &QToolButton::clicked, this, [this]() { runCurrent(); });
}

void ScriptPanel::bind(World& world0,
                       AssetBrowser& browser0,
                       ScriptEditor& scripts0,
                       EditorHistory& history0) {
    world_ = &world0;
    browser_ = &browser0;
    scripts_ = &scripts0;
    history_ = &history0;
}

QString ScriptPanel::currentPath() const {
    return path_;
}

void ScriptPanel::openPath(const QString& path) {
    if (path == path_ && !path.isEmpty()) {
        return;
    }
    if (dirty_) {
        saveCurrent();
    }
    path_ = path;
    loadCurrent();
}

void ScriptPanel::refresh() {
    if (!browser_) {
        return;
    }
    if (!browser_->openedScript.empty() &&
        QString::fromStdString(browser_->openedScript) != path_) {
        openPath(QString::fromStdString(browser_->openedScript));
    }
}

void ScriptPanel::loadCurrent() {
    if (!scripts_ || !browser_ || path_.isEmpty()) {
        code_->setPlainText(QString());
        pathLabel_->setText(tr("No script open"));
        dirty_ = false;
        return;
    }
    if (!scripts_->loadFile(browser_->assets, path_.toStdString())) {
        statusLabel_->setText(tr("Could not open %1").arg(path_));
        return;
    }
    code_->setPlainText(QString::fromStdString(std::string(scripts_->source())));
    pathLabel_->setText(path_);
    dirty_ = false;
    statusLabel_->clear();
}

void ScriptPanel::saveCurrent() {
    if (!scripts_ || !browser_ || path_.isEmpty()) {
        return;
    }
    if (!scripts_->saveFile(browser_->assets)) {
        statusLabel_->setText(tr("Could not save %1").arg(path_));
        return;
    }
    dirty_ = false;
    pathLabel_->setText(path_);
    statusLabel_->setText(tr("Saved %1").arg(path_));
    Q_EMIT scriptEdited();
}

void ScriptPanel::runCurrent() {
    if (!scripts_ || !browser_ || path_.isEmpty() || !world_) {
        return;
    }
    if (!scripts_->executeSource(*world_, browser_->assets, path_.toStdString())) {
        statusLabel_->setText(
            tr("Could not run %1: %2")
                .arg(path_, QString::fromStdString(scripts_->error())));
        return;
    }
    statusLabel_->setText(tr("Script ran."));
}

// ---------------------------------------------------------------------------
// ConsolePanel
// ---------------------------------------------------------------------------

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setObjectName(QStringLiteral("ConsoleOutput"));
    QFont monospace(QStringLiteral("monospace"));
    monospace.setStyleHint(QFont::Monospace);
    output_->setFont(monospace);
    layout->addWidget(output_, 1);
    auto* row = new QHBoxLayout();
    input_ = new QLineEdit(this);
    input_->setPlaceholderText(tr("Lua expression, then Enter"));
    row->addWidget(input_, 1);
    auto* run = new QPushButton(tr("Run"), this);
    run->setAutoDefault(false);
    row->addWidget(run);
    layout->addLayout(row);
    connect(run, &QPushButton::clicked, this, [this]() { execute(); });
    connect(input_, &QLineEdit::returnPressed, this, [this]() { execute(); });
}

void ConsolePanel::bind(ScriptEditor& scripts0) {
    scripts_ = &scripts0;
    printedLines_ = 0;
}

void ConsolePanel::appendLine(const QString& text) {
    output_->appendPlainText(text);
}

void ConsolePanel::refresh() {
    if (logger_) {
        logger_();
    }
    if (!scripts_) {
        return;
    }
    const auto& lines = scripts_->console().lines();
    if (lines.size() < printedLines_) {
        // The console was reset (play mode start/stop clears it).
        output_->clear();
        printedLines_ = 0;
    }
    for (; printedLines_ < lines.size(); ++printedLines_) {
        const auto& line = lines[printedLines_];
        QString prefix;
        switch (line.kind) {
        case LuaConsole::Kind::Error:
            prefix = QStringLiteral("[error] ");
            break;
        case LuaConsole::Kind::Warning:
            prefix = QStringLiteral("[warn] ");
            break;
        case LuaConsole::Kind::Output:
            prefix = QString();
            break;
        default:
            prefix = QStringLiteral("> ");
            break;
        }
        output_->appendPlainText(prefix + QString::fromStdString(line.text));
    }
    if (printedLines_ > 0) {
        output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
    }
}

void ConsolePanel::execute() {
    if (!scripts_) {
        return;
    }
    const QString text = input_->text();
    if (text.trimmed().isEmpty()) {
        return;
    }
    input_->clear();
    scripts_->console().execute(text.toStdString(), "console");
    refresh();
}

} // namespace qt
} // namespace ui
