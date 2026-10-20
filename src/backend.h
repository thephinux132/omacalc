#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString expression READ expression NOTIFY calculationChanged)
    Q_PROPERTY(QString display READ display NOTIFY calculationChanged)
    Q_PROPERTY(QString preview READ preview NOTIFY calculationChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(bool degrees READ degrees WRITE setDegrees NOTIFY degreesChanged)
    Q_PROPERTY(bool scientific READ scientific WRITE setScientific NOTIFY scientificChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    QString expression() const;
    QString display() const;
    QString preview() const;
    QVariantList history() const { return m_history; }

    bool degrees() const { return m_degrees; }
    void setDegrees(bool degrees);
    bool scientific() const { return m_scientific; }
    void setScientific(bool scientific);

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }

    // Angles are read in degrees unless told otherwise.
    static double evaluateTokens(const QStringList &tokens, bool *ok, bool degrees = true);
    static QString formatNumber(double value);

    Q_INVOKABLE void pressKey(const QString &key);
    Q_INVOKABLE void copyResult() const;
    Q_INVOKABLE void pasteNumber();
    Q_INVOKABLE void recallHistory(int index);
    Q_INVOKABLE void clearHistory();
    // Launch size for the current text scale. Not a live binding: the window
    // reads it once, and a later scale change waits until the next launch.
    Q_INVOKABLE QVariantMap defaultWindowSize() const;

signals:
    void calculationChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void historyChanged();
    void degreesChanged();
    void scientificChanged();

private:
    void pressDigit(const QString &digit);
    void pressDecimal();
    void pressOperator(const QString &pretty);
    void pressEquals();
    void pressPercent();
    void pressToggleSign();
    void pressBackspace();
    void pressClear();
    void pressOpener(const QString &opener);
    void pressCloser();
    void pressFactorial();
    void pressConstant(const QString &constant);

    QString currentValue() const;
    void startEntry();
    void sealEntry();
    QStringList completedTokens() const;
    void addToHistory(const QString &expression, double value);
    void beginEditingAfterResult();
    void clearEvaluation();
    static QString prettyExpression(const QStringList &tokens);
    void loadOmarchyTheme();
    void watchOmarchyTheme();
    void discardSavedWindowGeometry();

    QStringList m_tokens;
    QString m_entry;
    QString m_result;
    double m_resultValue = 0;
    QString m_evaluatedExpression;
    bool m_justEvaluated = false;
    bool m_errored = false;
    bool m_degrees = true;
    bool m_scientific = false;
    QVariantList m_history;

    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QFileSystemWatcher m_themeWatcher;
};
