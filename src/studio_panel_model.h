#pragma once

#include <QAbstractListModel>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QWidget>

class QLayout;

// Present the existing backend controls as data, never as embedded native UI.
// QPointers survive reflected-parameter rebuilds; edits use the existing signals.
class StudioPanelModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit StudioPanelModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setPanel(QWidget* panel);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void edit(int row, const QVariant& value);
    Q_INVOKABLE void activate(int row);
    void refresh();

private:
    struct Row { QPointer<QWidget> widget; QVariantMap state; };
    QVariantMap describe(QWidget* widget) const;
    void collect(QWidget* widget, QList<QPointer<QWidget>>& widgets) const;
    void collectLayout(QLayout* layout, QList<QPointer<QWidget>>& widgets) const;
    QPointer<QWidget> panel_;
    QList<Row> rows_;
    QTimer timer_;
    bool editing_ = false;
};
