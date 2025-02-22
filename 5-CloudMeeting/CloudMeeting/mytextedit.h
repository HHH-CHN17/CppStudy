#ifndef MYTEXTEDIT_H
#define MYTEXTEDIT_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QStringList>
#include <QPair>
#include <QVector>
//文本编辑控件的重载，一样会集成到widget里面去，用于替换里面的编辑框
class Completer: public QCompleter
{
Q_OBJECT
public:
    explicit Completer(QWidget *parent= nullptr);
};

class MyTextEdit : public QWidget
{
    Q_OBJECT
private:
    QPlainTextEdit *edit;
    Completer *completer;
    QVector<QPair<int, int> > ipspan;
public:
    explicit MyTextEdit(QWidget *parent = nullptr);
    QString toPlainText();
    void setPlainText(QString);
    void setPlaceholderText(QString);
    void setCompleter(QStringList );
private:
    QString textUnderCursor();
    bool eventFilter(QObject *, QEvent *);

private slots:
    void changeCompletion(QString);
public slots:

    void complete();
signals:
};

#endif // MYTEXTEDIT_H
