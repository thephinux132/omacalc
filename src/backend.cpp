#include "backend.h"

#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QSettings>
#include <QTextStream>
#include <QVariantMap>
#include <cmath>
#include <limits>

namespace {
const QString plusSign = QStringLiteral("+");
const QString minusSign = QStringLiteral("−");
const QString multiplySign = QStringLiteral("×");
const QString divideSign = QStringLiteral("÷");
const QString powerSign = QStringLiteral("^");

// A leading # is a bare hex color rather than a comment.
QString withoutTomlComment(const QString &text) {
    const QString value = text.trimmed();
    QChar quote;
    bool escaped = false;
    for (int i = 0; i < value.size(); ++i) {
        const QChar character = value.at(i);
        if (escaped) {
            escaped = false;
        } else if (quote == QLatin1Char('"') && character == QLatin1Char('\\')) {
            escaped = true;
        } else if (!quote.isNull() && character == quote) {
            quote = QChar();
        } else if (quote.isNull() && (character == QLatin1Char('"')
                                      || character == QLatin1Char('\''))) {
            quote = character;
        } else if (quote.isNull() && character == QLatin1Char('#') && i > 0) {
            return value.left(i).trimmed();
        }
    }
    return value.trimmed();
}

bool isOperator(const QString &token) {
    return token == plusSign || token == minusSign
        || token == multiplySign || token == divideSign
        || token == powerSign;
}

// Each function token carries its own opening parenthesis, so "sin(" reads
// back exactly as it was entered and closes like any other group.
const QStringList functionTokens = {
    QStringLiteral("sin("), QStringLiteral("cos("), QStringLiteral("tan("),
    QStringLiteral("ln("), QStringLiteral("log("), QStringLiteral("√("),
};
const QString openParen = QStringLiteral("(");
const QString closeParen = QStringLiteral(")");
const QString factorialSign = QStringLiteral("!");
const QString piSign = QStringLiteral("π");
const QString eulerSign = QStringLiteral("e");

bool isOpener(const QString &token) {
    return token == openParen || functionTokens.contains(token);
}

bool isConstant(const QString &token) {
    return token == piSign || token == eulerSign;
}

// A token that can stand at the end of a complete operand: a number, a
// constant, a closed group, or a factorial.
bool endsOperand(const QString &token) {
    return !isOperator(token) && !isOpener(token);
}

int unclosedGroups(const QStringList &tokens) {
    int depth = 0;
    for (const QString &token : tokens) {
        if (isOpener(token))
            ++depth;
        else if (token == closeParen && depth > 0)
            --depth;
    }
    return depth;
}

// The index where the operand ending at `end` begins, so a closed group is
// taken whole along with the function that opened it.
int operandStart(const QStringList &tokens, int end) {
    const QString &token = tokens.at(end);
    if (token == factorialSign && end > 0)
        return operandStart(tokens, end - 1);
    if (token != closeParen)
        return end;
    int depth = 0;
    for (int i = end; i >= 0; --i) {
        if (tokens.at(i) == closeParen)
            ++depth;
        else if (isOpener(tokens.at(i)) && --depth == 0)
            return i;
    }
    return 0;
}

// Digits are entered raw, so "5." and "-" can linger while typing. Seal them
// into plain numbers before they join the expression.
QString sealNumber(const QString &entry) {
    QString sealed = entry;
    if (sealed.endsWith(QLatin1Char('.')))
        sealed.chop(1);
    if (sealed.isEmpty() || sealed == QStringLiteral("-"))
        return QStringLiteral("0");
    return sealed;
}

// Binary floating point leaves sin(π) at 1.2e-16 rather than 0. A result that
// small next to an argument of at least 1 is rounding noise, not an answer.
double snapTrigNoise(double argument, double result) {
    if (std::fabs(argument) >= 1.0
            && std::fabs(result) < 4 * std::numeric_limits<double>::epsilon() * std::fabs(argument))
        return 0;
    return result;
}

// Recursive descent over the token list, lowest precedence first:
//   expression := term (("+" | "−") term)*
//   term       := unary (("×" | "÷") unary)*
//   unary      := ("−" | "+") unary | power
//   power      := postfix ("^" unary)?          right-associative
//   postfix    := primary "!"*
//   primary    := number | constant | opener expression ")"
// Signed number tokens like "-2" are literals, so "-2 ^ 2" squares the
// negative number while "−(2) ^ 2" negates the square.
class Parser {
public:
    Parser(const QStringList &tokens, bool degrees) : m_tokens(tokens), m_degrees(degrees) {}

    double parse(bool *ok) {
        const double value = expression();
        *ok = m_ok && m_position == m_tokens.size() && std::isfinite(value);
        return *ok ? value : 0;
    }

private:
    const QString &peek() const {
        static const QString end;
        return m_position < m_tokens.size() ? m_tokens.at(m_position) : end;
    }

    double fail() {
        m_ok = false;
        return 0;
    }

    double checked(double value) {
        return std::isfinite(value) ? value : fail();
    }

    double expression() {
        double value = term();
        while (m_ok && (peek() == plusSign || peek() == minusSign)) {
            const bool adding = m_tokens.at(m_position++) == plusSign;
            const double right = term();
            value = checked(adding ? value + right : value - right);
        }
        return value;
    }

    double term() {
        double value = unary();
        while (m_ok && (peek() == multiplySign || peek() == divideSign)) {
            const bool multiplying = m_tokens.at(m_position++) == multiplySign;
            const double right = unary();
            value = checked(multiplying ? value * right : value / right);
        }
        return value;
    }

    double unary() {
        if (peek() == minusSign) {
            ++m_position;
            return -unary();
        }
        if (peek() == plusSign) {
            ++m_position;
            return unary();
        }
        return power();
    }

    double power() {
        const double base = postfix();
        if (!m_ok || peek() != powerSign)
            return base;
        ++m_position;
        const double exponent = unary();
        if (!m_ok)
            return 0;
        // A negative base with a fractional exponent has no real result.
        if (base < 0.0 && std::floor(exponent) != exponent)
            return fail();
        return checked(std::pow(base, exponent));
    }

    double postfix() {
        double value = primary();
        while (m_ok && peek() == factorialSign) {
            ++m_position;
            if (value < 0 || std::floor(value) != value || value > 170)
                return fail();
            double product = 1;
            for (int n = 2; n <= int(value); ++n)
                product *= n;
            value = product;
        }
        return value;
    }

    double primary() {
        if (m_position >= m_tokens.size())
            return fail();
        const QString token = m_tokens.at(m_position++);

        if (token == piSign)
            return M_PI;
        if (token == eulerSign)
            return M_E;
        if (isOpener(token)) {
            const double inner = expression();
            if (!m_ok || peek() != closeParen)
                return fail();
            ++m_position;
            return applyFunction(token, inner);
        }
        if (isOperator(token) || token == closeParen || token == factorialSign)
            return fail();

        bool numberOk = false;
        const double value = QLocale::c().toDouble(token, &numberOk);
        return numberOk ? value : fail();
    }

    double applyFunction(const QString &function, double x) {
        if (function == openParen)
            return x;
        if (function == QStringLiteral("√("))
            return x < 0 ? fail() : std::sqrt(x);
        if (function == QStringLiteral("ln("))
            return x <= 0 ? fail() : std::log(x);
        if (function == QStringLiteral("log("))
            return x <= 0 ? fail() : std::log10(x);
        return trig(function, x);
    }

    double trig(const QString &function, double x) {
        const bool sine = function == QStringLiteral("sin(");
        const bool cosine = function == QStringLiteral("cos(");
        if (m_degrees) {
            // Whole quarter turns are exact, so sin(180) is 0 and tan(90)
            // has no value rather than a huge rounding artifact.
            const double reduced = std::fmod(x, 360.0);
            if (std::fmod(reduced, 90.0) == 0) {
                const int quarter = ((int(reduced / 90.0) % 4) + 4) % 4;
                static const int sines[] = {0, 1, 0, -1};
                static const int cosines[] = {1, 0, -1, 0};
                if (sine)
                    return sines[quarter];
                if (cosine)
                    return cosines[quarter];
                return cosines[quarter] == 0 ? fail() : double(sines[quarter]) / cosines[quarter];
            }
            const double radians = reduced * M_PI / 180.0;
            return checked(sine ? std::sin(radians)
                                : cosine ? std::cos(radians) : std::tan(radians));
        }
        const double result = sine ? std::sin(x) : cosine ? std::cos(x) : std::tan(x);
        return checked(snapTrigNoise(x, result));
    }

    const QStringList &m_tokens;
    const bool m_degrees;
    int m_position = 0;
    bool m_ok = true;
};
}

Backend::Backend(QObject *parent) : QObject(parent) {
    // Older builds stored the compositor's size. On Hyprland 0.56 that size
    // was the tiled or maximized rectangle, and the maximized flag was set for
    // a normal window, so the next launch filled the screen. Drop both keys.
    discardSavedWindowGeometry();

    QSettings settings;
    m_degrees = settings.value(QStringLiteral("mode/degrees"), true).toBool();
    m_scientific = settings.value(QStringLiteral("mode/scientific"), false).toBool();
    m_history = settings.value(QStringLiteral("history/entries")).toList();

    loadOmarchyTheme();
    watchOmarchyTheme();
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        loadOmarchyTheme();
        watchOmarchyTheme();
    });
}

QString Backend::expression() const {
    if (m_errored || m_justEvaluated)
        return m_evaluatedExpression;
    return prettyExpression(m_tokens);
}

QString Backend::display() const {
    if (m_errored)
        return QStringLiteral("Error");
    if (!m_entry.isEmpty())
        return m_entry;
    if (m_justEvaluated)
        return m_result;

    // Show the last complete operand: the number before a pending operator,
    // or the value of a group that was just closed.
    int end = m_tokens.size() - 1;
    while (end >= 0 && !endsOperand(m_tokens.at(end)))
        --end;
    if (end < 0)
        return QStringLiteral("0");
    bool ok = false;
    const double value = evaluateTokens(
        m_tokens.mid(operandStart(m_tokens, end), end - operandStart(m_tokens, end) + 1),
        &ok, m_degrees);
    return ok ? formatNumber(value) : QStringLiteral("0");
}

// The answer so far, shown before equals is pressed. Empty when it would only
// repeat the display or there is nothing yet to work out.
QString Backend::preview() const {
    if (m_errored || m_justEvaluated)
        return QString();
    const QStringList tokens = completedTokens();
    if (tokens.size() < 2)
        return QString();
    bool ok = false;
    const double value = evaluateTokens(tokens, &ok, m_degrees);
    if (!ok)
        return QString();
    const QString formatted = formatNumber(value);
    return formatted == display() ? QString() : formatted;
}

// What equals would evaluate: the entry sealed on, dangling operators and
// empty groups dropped, and every open group closed.
QStringList Backend::completedTokens() const {
    QStringList tokens = m_tokens;
    if (!m_entry.isEmpty())
        tokens << sealNumber(m_entry);
    while (!tokens.isEmpty() && (isOperator(tokens.last()) || isOpener(tokens.last())))
        tokens.removeLast();
    for (int open = unclosedGroups(tokens); open > 0; --open)
        tokens << closeParen;
    return tokens;
}

// Operand tokens carry full round-trip precision when they come from a chained
// result; present every number at display precision instead. Groups hug their
// contents, so "sin( 30 )" reads "sin(30)".
QString Backend::prettyExpression(const QStringList &tokens) {
    QString pretty;
    for (int i = 0; i < tokens.size(); ++i) {
        const QString &token = tokens.at(i);
        const bool afterOpener = i > 0 && isOpener(tokens.at(i - 1));
        const bool unaryMinus = token == minusSign && afterOpener;
        const bool afterUnary = i > 1 && tokens.at(i - 1) == minusSign
            && isOpener(tokens.at(i - 2));
        if (i > 0 && !afterOpener && !afterUnary
                && token != closeParen && token != factorialSign)
            pretty += QLatin1Char(' ');
        if (isOperator(token) || isOpener(token) || isConstant(token)
                || token == closeParen || token == factorialSign || unaryMinus)
            pretty += token;
        else
            pretty += formatNumber(QLocale::c().toDouble(token));
    }
    return pretty;
}

// The number the calculator is "at" right now: the entry being typed, or the
// operand the last operator was applied to, or the fresh-start zero.
QString Backend::currentValue() const {
    if (!m_entry.isEmpty())
        return sealNumber(m_entry);
    for (int i = m_tokens.size() - 1; i >= 0; --i) {
        if (!isOperator(m_tokens.at(i)))
            return m_tokens.at(i);
    }
    return QStringLiteral("0");
}

void Backend::pressKey(const QString &key) {
    if (key.size() == 1 && key.at(0).isDigit()) {
        pressDigit(key);
    } else if (key == QStringLiteral(".")) {
        pressDecimal();
    } else if (key == QStringLiteral("+")) {
        pressOperator(plusSign);
    } else if (key == QStringLiteral("-") || key == minusSign) {
        pressOperator(minusSign);
    } else if (key == QStringLiteral("*") || key == multiplySign) {
        pressOperator(multiplySign);
    } else if (key == QStringLiteral("/") || key == divideSign) {
        pressOperator(divideSign);
    } else if (key == QStringLiteral("^")) {
        pressOperator(powerSign);
    } else if (key == QStringLiteral("=")) {
        pressEquals();
    } else if (key == QStringLiteral("%")) {
        pressPercent();
    } else if (key == QStringLiteral("sign")) {
        pressToggleSign();
    } else if (key == QStringLiteral("backspace")) {
        pressBackspace();
    } else if (key == QStringLiteral("clear")) {
        pressClear();
    } else if (key == openParen) {
        pressOpener(openParen);
    } else if (key == closeParen) {
        pressCloser();
    } else if (key == factorialSign) {
        pressFactorial();
    } else if (key == piSign || key == QStringLiteral("pi")) {
        pressConstant(piSign);
    } else if (key == eulerSign) {
        pressConstant(eulerSign);
    } else if (key == QStringLiteral("sqrt") || key == QStringLiteral("√")) {
        pressOpener(QStringLiteral("√("));
    } else if (functionTokens.contains(key + openParen)) {
        pressOpener(key + openParen);
    } else {
        return;
    }

    emit calculationChanged();
}

// A new entry right after a complete operand multiplies it, so 2(3) is 6 and
// (1 + 1) 5 is 10.
void Backend::startEntry() {
    if (m_entry.isEmpty() && !m_tokens.isEmpty() && endsOperand(m_tokens.last()))
        m_tokens << multiplySign;
}

void Backend::sealEntry() {
    if (m_entry.isEmpty())
        return;
    m_tokens << sealNumber(m_entry);
    m_entry.clear();
}

void Backend::pressDigit(const QString &digit) {
    if (m_errored)
        pressClear();
    if (m_justEvaluated) {
        // A digit after equals starts a new calculation rather than
        // appending to the result.
        pressClear();
    }
    startEntry();

    if (m_entry == QStringLiteral("0")) {
        m_entry = digit;
        return;
    }
    if (m_entry == QStringLiteral("-0")) {
        m_entry = QStringLiteral("-") + digit;
        return;
    }

    // Fifteen significant digits is what the display format preserves; letting
    // the entry grow beyond that would silently round what was typed. The
    // zero in a leading "0." is not significant, so it does not count.
    int digits = 0;
    for (const QChar character : m_entry) {
        if (character.isDigit())
            ++digits;
    }
    if (m_entry.startsWith(QStringLiteral("0."))
            || m_entry.startsWith(QStringLiteral("-0.")))
        --digits;
    if (digits < 15)
        m_entry += digit;
}

void Backend::pressDecimal() {
    if (m_errored)
        pressClear();
    if (m_justEvaluated)
        pressClear();
    startEntry();

    if (m_entry.isEmpty())
        m_entry = QStringLiteral("0.");
    else if (!m_entry.contains(QLatin1Char('.')))
        m_entry += QLatin1Char('.');
}

void Backend::pressOperator(const QString &pretty) {
    if (m_errored)
        return;

    if (m_justEvaluated) {
        // Chain from the exact result value, not its rounded display text, so
        // 1 ÷ 3 = × 3 comes back as 1. Seventeen significant digits round-trip
        // any double; the expression line re-rounds them for presentation.
        m_tokens = QStringList(QString::number(m_resultValue, 'g', 17));
        clearEvaluation();
    }

    if (!m_entry.isEmpty()) {
        m_tokens << sealNumber(m_entry) << pretty;
        m_entry.clear();
    } else if (m_tokens.isEmpty()) {
        m_tokens << QStringLiteral("0") << pretty;
    } else if (isOpener(m_tokens.last())) {
        // Straight after an opening parenthesis only a negation makes sense.
        if (pretty == minusSign)
            m_tokens << pretty;
    } else if (isOperator(m_tokens.last())) {
        const bool unary = m_tokens.size() > 1 && isOpener(m_tokens.at(m_tokens.size() - 2));
        if (unary && pretty != minusSign)
            return;
        m_tokens.last() = pretty;
    } else {
        m_tokens << pretty;
    }
}

void Backend::pressOpener(const QString &opener) {
    if (m_errored || m_justEvaluated)
        pressClear();
    sealEntry();
    startEntry();
    m_tokens << opener;
}

void Backend::pressCloser() {
    if (m_errored || m_justEvaluated || unclosedGroups(m_tokens) == 0)
        return;
    sealEntry();
    if (m_tokens.isEmpty() || !endsOperand(m_tokens.last()))
        return;
    m_tokens << closeParen;
}

void Backend::pressFactorial() {
    if (m_errored)
        return;
    if (m_justEvaluated) {
        m_tokens = QStringList(QString::number(m_resultValue, 'g', 17));
        clearEvaluation();
    }
    sealEntry();
    if (m_tokens.isEmpty() || !endsOperand(m_tokens.last()))
        return;
    m_tokens << factorialSign;
}

void Backend::pressConstant(const QString &constant) {
    if (m_errored || m_justEvaluated)
        pressClear();
    sealEntry();
    startEntry();
    m_tokens << constant;
}

void Backend::pressEquals() {
    if (m_errored || m_justEvaluated)
        return;

    const QStringList finalTokens = completedTokens();
    if (finalTokens.isEmpty())
        return;

    m_evaluatedExpression = prettyExpression(finalTokens);
    bool ok = false;
    const double value = evaluateTokens(finalTokens, &ok, m_degrees);
    if (!ok) {
        m_errored = true;
    } else {
        m_resultValue = value;
        m_result = formatNumber(value);
        m_justEvaluated = true;
        if (finalTokens.size() > 1)
            addToHistory(m_evaluatedExpression, value);
    }
    m_tokens.clear();
    m_entry.clear();
}

// iOS-style percent: with a pending + or −, x% means x percent of the running
// total, so 200 + 10 % = gives 220. With ×, ÷, or ^ (or on its own) x% is
// simply x ÷ 100, so 200 × 10 % = gives 20. Inside parentheses the running
// total is the group's, so (200 + 10 %) is 220 too.
void Backend::pressPercent() {
    if (m_errored)
        return;

    if (m_justEvaluated) {
        const double percent = m_resultValue / 100.0;
        clearEvaluation();
        m_entry = formatNumber(percent);
        return;
    }

    if (m_entry.isEmpty() && !m_tokens.isEmpty() && !isOperator(m_tokens.last()))
        return;

    bool ok = false;
    const double value = QLocale::c().toDouble(currentValue(), &ok);
    if (!ok)
        return;

    double percent = value / 100.0;
    if (!m_tokens.isEmpty() && (m_tokens.last() == plusSign || m_tokens.last() == minusSign)) {
        int groupStart = 0;
        int depth = 0;
        for (int i = m_tokens.size() - 1; i >= 0; --i) {
            if (m_tokens.at(i) == closeParen) {
                ++depth;
            } else if (isOpener(m_tokens.at(i)) && depth-- == 0) {
                groupStart = i + 1;
                break;
            }
        }
        const QStringList leftSide = m_tokens.mid(groupStart, m_tokens.size() - 1 - groupStart);
        bool baseOk = false;
        const double base = evaluateTokens(leftSide, &baseOk, m_degrees);
        if (baseOk)
            percent = base * value / 100.0;
    }
    m_entry = formatNumber(percent);
}

void Backend::pressToggleSign() {
    if (m_errored)
        return;
    if (m_justEvaluated)
        beginEditingAfterResult();

    // With nothing typed yet, start a fresh negative operand instead of
    // dredging up the previous one: 4 + ± 2 enters -2, not -42.
    if (m_entry.isEmpty()) {
        startEntry();
        m_entry = QStringLiteral("-0");
        return;
    }

    if (m_entry.startsWith(QLatin1Char('-')))
        m_entry.remove(0, 1);
    else
        m_entry.prepend(QLatin1Char('-'));
}

void Backend::pressBackspace() {
    if (m_errored) {
        pressClear();
        return;
    }
    if (m_justEvaluated)
        beginEditingAfterResult();

    if (!m_entry.isEmpty()) {
        m_entry.chop(1);
        if (m_entry == QStringLiteral("-"))
            m_entry.clear();
        return;
    }

    // With no entry, step back through the expression one token at a time,
    // and reopen a number for editing when backspace reaches it.
    if (m_tokens.isEmpty())
        return;
    m_tokens.removeLast();
    if (!m_tokens.isEmpty() && endsOperand(m_tokens.last())
            && m_tokens.last() != closeParen && m_tokens.last() != factorialSign
            && !isConstant(m_tokens.last())) {
        m_entry = formatNumber(QLocale::c().toDouble(m_tokens.takeLast()));
    }
}

void Backend::pressClear() {
    m_tokens.clear();
    m_entry.clear();
    clearEvaluation();
    m_errored = false;
}

// Editing after equals picks up from the result's displayed digits, with the
// old expression cleared away so the new one grows from "42" rather than
// "42 × 3 + 7".
void Backend::beginEditingAfterResult() {
    m_entry = m_result;
    m_tokens.clear();
    clearEvaluation();
}

void Backend::clearEvaluation() {
    m_result.clear();
    m_resultValue = 0;
    m_evaluatedExpression.clear();
    m_justEvaluated = false;
}

double Backend::evaluateTokens(const QStringList &tokens, bool *ok, bool degrees) {
    bool localOk = false;
    if (!ok)
        ok = &localOk;
    return Parser(tokens, degrees).parse(ok);
}

QString Backend::formatNumber(double value) {
    if (value == 0)
        value = 0;  // Collapse negative zero.

    // Fifteen significant digits keeps binary-float noise like
    // 0.1 + 0.2 = 0.30000000000000004 out of the display while showing
    // every integer the 15-digit entry limit can produce exactly; larger
    // magnitudes fall back to scientific notation.
    return QString::number(value, 'g', 15);
}

void Backend::copyResult() const {
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(display());
}

// Paste replaces the current entry when the clipboard holds a number,
// tolerating surrounding whitespace, a decimal comma, and the typographic
// minus this calculator itself puts in expressions.
void Backend::pasteNumber() {
    const QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return;

    QString text = clipboard->text().trimmed();
    text.replace(minusSign, QStringLiteral("-"));
    text.remove(QLatin1Char(' '));

    bool ok = false;
    double value = QLocale::c().toDouble(text, &ok);
    if (!ok) {
        text.replace(QLatin1Char(','), QLatin1Char('.'));
        value = QLocale::c().toDouble(text, &ok);
    }
    if (!ok || !std::isfinite(value))
        return;

    if (m_errored || m_justEvaluated)
        pressClear();
    startEntry();
    m_entry = formatNumber(value);
    emit calculationChanged();
}

// Recalling a past result drops its exact value into the expression, where it
// works like any operand: after another operand it multiplies.
void Backend::recallHistory(int index) {
    if (index < 0 || index >= m_history.size())
        return;
    bool ok = false;
    const double value = QLocale::c().toDouble(
        m_history.at(index).toMap().value(QStringLiteral("value")).toString(), &ok);
    if (!ok || !std::isfinite(value))
        return;

    if (m_errored || m_justEvaluated)
        pressClear();
    sealEntry();
    startEntry();
    m_tokens << QString::number(value, 'g', 17);
    emit calculationChanged();
}

void Backend::clearHistory() {
    if (m_history.isEmpty())
        return;
    m_history.clear();
    QSettings().remove(QStringLiteral("history/entries"));
    emit historyChanged();
}

void Backend::addToHistory(const QString &expression, double value) {
    QVariantMap entry;
    entry.insert(QStringLiteral("expression"), expression);
    entry.insert(QStringLiteral("result"), formatNumber(value));
    entry.insert(QStringLiteral("value"), QString::number(value, 'g', 17));
    if (!m_history.isEmpty() && m_history.first().toMap() == entry)
        return;

    m_history.prepend(entry);
    while (m_history.size() > 100)
        m_history.removeLast();
    QSettings().setValue(QStringLiteral("history/entries"), m_history);
    emit historyChanged();
}

void Backend::setDegrees(bool degrees) {
    if (m_degrees == degrees)
        return;
    m_degrees = degrees;
    QSettings().setValue(QStringLiteral("mode/degrees"), degrees);
    emit degreesChanged();
    emit calculationChanged();
}

void Backend::setScientific(bool scientific) {
    if (m_scientific == scientific)
        return;
    m_scientific = scientific;
    QSettings().setValue(QStringLiteral("mode/scientific"), scientific);
    emit scientificChanged();
}

QVariantMap Backend::defaultWindowSize() const {
    qreal scale = m_textScale;
    if (!std::isfinite(scale) || scale <= 0)
        scale = 1.0;

    // 400×568 is the v0.2.2 design size; the scientific panel adds three key
    // rows. The minimum matches Main.qml, so a text scale below 1 still opens
    // at a usable calculator.
    QVariantMap size;
    size.insert(QStringLiteral("width"), qMax(300, qRound(400 * scale)));
    size.insert(QStringLiteral("height"),
                qMax(430, qRound((m_scientific ? 772 : 568) * scale)));
    return size;
}

void Backend::discardSavedWindowGeometry() {
    QSettings settings;
    const bool hadGeometry = settings.contains(QStringLiteral("window/geometry"));
    const bool hadMaximized = settings.contains(QStringLiteral("window/maximized"));
    if (!hadGeometry && !hadMaximized)
        return;

    settings.remove(QStringLiteral("window/geometry"));
    settings.remove(QStringLiteral("window/maximized"));
    settings.sync();
}

void Backend::setDarkMode(bool darkMode) {
    if (m_darkMode == darkMode)
        return;

    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale) {
    if (qFuzzyCompare(m_textScale, textScale))
        return;

    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::loadOmarchyTheme() {
    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QString themeBackground;
    QString themeForeground;
    QString themeAccent;
    QString themeSelection;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;

            const QString key = line.left(equals).trimmed();
            QString value = withoutTomlComment(line.mid(equals + 1));
            if (value.size() >= 2
                    && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                        || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);

            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background") && QColor(value).isValid())
                themeBackground = value;
            else if (key == QStringLiteral("foreground") && QColor(value).isValid())
                themeForeground = value;
            else if (key == QStringLiteral("accent") && QColor(value).isValid())
                themeAccent = value;
            else if (key == QStringLiteral("selection") && QColor(value).isValid())
                themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    m_themeBackground = themeBackground.isEmpty()
        ? (m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff")) : themeBackground;
    m_themeForeground = themeForeground.isEmpty()
        ? (m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324")) : themeForeground;
    m_themeAccent = themeAccent.isEmpty()
        ? (m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2")) : themeAccent;
    m_themeSelection = themeSelection.isEmpty()
        ? (m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2")) : themeSelection;

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    const QString colorsPath = themeDir + QStringLiteral("/colors.toml");

    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(colorsPath))
        m_themeWatcher.addPath(colorsPath);
}
