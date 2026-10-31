#pragma once

// GH #422 (E23-29): the Source tab's language detection and syntax colours.
//
// Qt-only (jnext_debugger); the SDL-only build never compiles this.
//
// A SourceHighlighter colours the foreground of a QPlainTextEdit's document and
// nothing else (no weight, italic, background, family or size), so a highlighted
// line has the geometry of the same line in Plain and cannot hide the current
// line's selection background or the gutter's marks (the gutter is another widget).

#include <QColor>
#include <QPalette>
#include <QSet>
#include <QString>
#include <QStringView>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <string_view>

enum class SourceLanguage { Plain, C, Asm, Basic };

/// The language of a source file. The extension first (case-insensitive; a trailing
/// ".m4" is dropped and the inner extension used: z88dk's "zxn_crt.asm.m4"), then the
/// content of the first 200 lines when the extension is not one of the table's. Plain
/// when neither decides.
SourceLanguage source_language_for(std::string_view file_name, const QString& text);

class SourceHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    /// The owner's ten classes (GH #422, 2026-10-31). Text is the editor palette's text colour
    /// and never a format: an unclassified character carries none.
    enum class Role { Text, Keyword, Instruction, Preprocessor, Label, String, Char, Integer, Float, Comment };
    static constexpr int kRoles = 10;

    explicit SourceHighlighter(QObject* parent);

    /// Set before the text is replaced; it does not rehighlight.
    void set_language(SourceLanguage language) { language_ = language; }
    SourceLanguage language() const { return language_; }

    /// Rebuild the colours for `palette`; rehighlight() only when one changed.
    bool apply_palette(const QPalette& palette);
    /// The colour in force for a role.
    QColor colour(Role role) const { return colours_[static_cast<int>(role)]; }
    /// The constant a light palette uses (a dark one passes it through palette_tint::ink()).
    static QColor light_colour(Role role);
    /// highlightBlock() calls so far.
    int blocks_highlighted() const { return blocks_; }

    /// Upper case; enumerated once from the disassembler (disasm_one() over the 7 opcode
    /// pages), so a new Z80N opcode is coloured the day the disassembler names it.
    static const QSet<QString>& asm_mnemonics();
    /// Upper case; the operand words the disassembler prints (registers, conditions).
    static const QSet<QString>& asm_registers();
    /// A z80asm or sjasmplus directive, case-insensitive, optional leading '.'.
    static bool is_asm_directive(QStringView word);

protected:
    void highlightBlock(const QString& text) override;

private:
    /// What ends an embedded-assembly span, and what the span may contain.
    enum class AsmCtx { File, CHash, CSdcc, Basic };

    int scan_asm(const QString& t, int i, AsmCtx ctx, bool& ended, bool& in_comment);
    int scan_c(const QString& t, int i, int& mode);
    void highlight_asm(const QString& t);
    void highlight_c(const QString& t);
    void highlight_basic(const QString& t);
    void paint(int from, int to, Role role);

    SourceLanguage language_ = SourceLanguage::Plain;
    QColor colours_[kRoles];
    QTextCharFormat formats_[kRoles];
    int blocks_ = 0;
};
