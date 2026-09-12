#include "studio_panel_model.h"
#include <QAbstractButton>
#include <QComboBox>
#include <QBoxLayout>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QScopedValueRollback>
#include <cmath>

StudioPanelModel::StudioPanelModel(QObject* parent) : QAbstractListModel(parent)
{
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &StudioPanelModel::refresh);
}

int StudioPanelModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : rows_.size(); }
QHash<int, QByteArray> StudioPanelModel::roleNames() const { return {{Qt::UserRole, "control"}}; }
QVariant StudioPanelModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size() || role != Qt::UserRole) return {};
    return rows_[index.row()].state;
}

QVariantMap StudioPanelModel::describe(QWidget* w) const
{
    QVariantMap s{{"kind", "label"}, {"text", ""}, {"value", ""}, {"choices", QStringList()},
                  {"minimum", 0}, {"maximum", 1}, {"step", 1}, {"decimals", 0},
                  {"enabled", w->isEnabled()}, {"hint", w->toolTip()}, {"readOnly", false}};
    if (auto* v = qobject_cast<QGroupBox*>(w)) { s["kind"] = "heading"; s["text"] = v->title(); }
    else if (auto* v = qobject_cast<QLabel*>(w)) s["text"] = v->text();
    else if (auto* v = qobject_cast<QAbstractButton*>(w)) {
        s["kind"] = v->isCheckable() ? "check" : "button";
        s["text"] = v->text(); s["value"] = v->isChecked();
    } else if (auto* v = qobject_cast<QComboBox*>(w)) {
        s["kind"] = "choice"; s["value"] = v->currentIndex();
        QStringList choices; for (int i = 0; i < v->count(); ++i) choices << v->itemText(i);
        s["choices"] = choices;
    } else if (auto* v = qobject_cast<QDoubleSpinBox*>(w)) {
        s["kind"] = "number"; s["value"] = v->value(); s["minimum"] = v->minimum();
        s["maximum"] = v->maximum(); s["step"] = v->singleStep(); s["decimals"] = v->decimals();
    } else if (auto* v = qobject_cast<QSpinBox*>(w)) {
        s["kind"] = "number"; s["value"] = v->value(); s["minimum"] = v->minimum();
        s["maximum"] = v->maximum(); s["step"] = v->singleStep();
    } else if (auto* v = qobject_cast<QSlider*>(w)) {
        s["kind"] = "slider"; s["value"] = v->value(); s["minimum"] = v->minimum();
        s["maximum"] = v->maximum(); s["step"] = v->singleStep();
    } else if (auto* v = qobject_cast<QLineEdit*>(w)) {
        s["kind"] = "text"; s["value"] = v->text(); s["text"] = v->placeholderText(); s["readOnly"] = v->isReadOnly();
    } else if (auto* v = qobject_cast<QPlainTextEdit*>(w)) {
        s["kind"] = "output"; s["value"] = v->toPlainText();
    }
    const QString qmlLabel = w->property("qmlPanelLabel").toString().trimmed();
    if (!qmlLabel.isEmpty()) s["text"] = qmlLabel;
    if (!w->accessibleName().isEmpty()) s["text"] = w->accessibleName();
    return s;
}

void StudioPanelModel::collectLayout(QLayout* layout, QList<QPointer<QWidget>>& widgets) const
{
    if (!layout) return;

    // Preserve QFormLayout semantics for the QML inspector. The first bridge
    // pass flattened label/field pairs into unrelated rows, which made panels
    // such as Preview Settings look like raw widget dumps. Attach the form label
    // to its field instead and omit the redundant QLabel row.
    if (auto* form = qobject_cast<QFormLayout*>(layout)) {
        for (int row = 0; row < form->rowCount(); ++row) {
            auto* spanning = form->itemAt(row, QFormLayout::SpanningRole);
            if (spanning) {
                if (spanning->widget()) collect(spanning->widget(), widgets);
                else if (spanning->layout()) collectLayout(spanning->layout(), widgets);
                continue;
            }

            QString labelText;
            if (auto* labelItem = form->itemAt(row, QFormLayout::LabelRole))
                if (auto* label = qobject_cast<QLabel*>(labelItem->widget()))
                    labelText = label->text().trimmed();

            if (auto* fieldItem = form->itemAt(row, QFormLayout::FieldRole)) {
                if (auto* field = fieldItem->widget()) {
                    const bool labelable = qobject_cast<QComboBox*>(field) || qobject_cast<QAbstractSpinBox*>(field) ||
                        qobject_cast<QSlider*>(field) || qobject_cast<QLineEdit*>(field);
                    if (labelable && !labelText.isEmpty() && field->accessibleName().isEmpty())
                        field->setProperty("qmlPanelLabel", labelText);
                    collect(field, widgets);
                } else if (fieldItem->layout()) {
                    collectLayout(fieldItem->layout(), widgets);
                }
            }
        }
        return;
    }

    const auto* box = qobject_cast<QBoxLayout*>(layout);
    const bool horizontal = box && (box->direction() == QBoxLayout::LeftToRight ||
                                    box->direction() == QBoxLayout::RightToLeft);
    for (int i = 0; i < layout->count(); ++i) {
        auto* item = layout->itemAt(i);
        // Many legacy inspectors use compact horizontal "Label: control" rows.
        // Preserve that relationship in QML rather than emitting two unrelated
        // rows. Only do this for horizontal layouts so explanatory labels in a
        // vertical stack are never mistaken for field names.
        if (horizontal && item && qobject_cast<QLabel*>(item->widget()) && i + 1 < layout->count()) {
            auto* label = qobject_cast<QLabel*>(item->widget());
            auto* next = layout->itemAt(i + 1);
            auto* field = next ? next->widget() : nullptr;
            const bool labelable = field && (qobject_cast<QComboBox*>(field) || qobject_cast<QAbstractSpinBox*>(field) ||
                qobject_cast<QSlider*>(field) || qobject_cast<QLineEdit*>(field));
            if (labelable) {
                if (field->accessibleName().isEmpty()) field->setProperty("qmlPanelLabel", label->text().trimmed());
                collect(field, widgets);
                ++i;
                continue;
            }
        }
        if (item->widget()) collect(item->widget(), widgets);
        else if (item->layout()) collectLayout(item->layout(), widgets);
    }
}

void StudioPanelModel::collect(QWidget* w, QList<QPointer<QWidget>>& widgets) const
{
    if (!w || w->property("qmlPanelSkip").toBool() ||
        (w != panel_ && w->isHidden() && w->testAttribute(Qt::WA_WState_ExplicitShowHide))) return;
    if (auto* scroll = qobject_cast<QScrollArea*>(w)) { collect(scroll->widget(), widgets); return; }
    if (qobject_cast<QGroupBox*>(w)) widgets << w;
    else if (qobject_cast<QLabel*>(w) || qobject_cast<QAbstractButton*>(w) ||
             qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSpinBox*>(w) ||
             qobject_cast<QSlider*>(w) || qobject_cast<QLineEdit*>(w) || qobject_cast<QPlainTextEdit*>(w)) {
        widgets << w; return; // Never recurse into a control's implementation children.
    }
    collectLayout(w->layout(), widgets);
}

void StudioPanelModel::setPanel(QWidget* panel) { panel_ = panel; refresh(); }
void StudioPanelModel::setActive(bool active) { if (active) { refresh(); timer_.start(); } else timer_.stop(); }
void StudioPanelModel::refresh()
{
    if (editing_) return;
    QList<QPointer<QWidget>> widgets;
    collect(panel_, widgets);
    bool same = widgets.size() == rows_.size();
    for (int i = 0; same && i < widgets.size(); ++i) same = widgets[i] == rows_[i].widget;
    if (!same) {
        beginResetModel(); rows_.clear();
        for (auto w : widgets) rows_.append({w, describe(w)});
        endResetModel(); return;
    }
    for (int i = 0; i < rows_.size(); ++i) {
        const auto state = describe(rows_[i].widget);
        if (rows_[i].state == state) continue;
        rows_[i].state = state;
        emit dataChanged(index(i), index(i), {Qt::UserRole});
    }
}

void StudioPanelModel::edit(int row, const QVariant& value)
{
    if (row < 0 || row >= rows_.size()) return;
    QPointer<QWidget> w = rows_[row].widget;
    if (!w || !w->isEnabled()) return;
    // Backend callbacks may rebuild parameter widgets. Never reuse a raw pointer
    // or reset the QML delegate while its editing signal is on the stack.
    QScopedValueRollback<bool> guard(editing_, true);
    if (auto* v = qobject_cast<QAbstractButton*>(w)) v->setChecked(value.toBool());
    else if (auto* v = qobject_cast<QComboBox*>(w)) { if (value.toInt() >= 0 && value.toInt() < v->count()) v->setCurrentIndex(value.toInt()); }
    else if (auto* v = qobject_cast<QDoubleSpinBox*>(w)) { bool ok; double n = value.toDouble(&ok); if (ok && std::isfinite(n)) v->setValue(n); }
    else if (auto* v = qobject_cast<QSpinBox*>(w)) { bool ok; int n = value.toInt(&ok); if (ok) v->setValue(n); }
    else if (auto* v = qobject_cast<QSlider*>(w)) v->setValue(value.toInt());
    else if (auto* v = qobject_cast<QLineEdit*>(w)) {
        if (!v->isReadOnly()) { v->setText(value.toString()); if (w) QMetaObject::invokeMethod(w, "editingFinished"); }
    }
    QTimer::singleShot(0, this, &StudioPanelModel::refresh);
}

void StudioPanelModel::activate(int row)
{
    if (row < 0 || row >= rows_.size()) return;
    auto* button = qobject_cast<QAbstractButton*>(rows_[row].widget);
    if (!button || !button->isEnabled()) return;
    QScopedValueRollback<bool> guard(editing_, true);
    button->click();
    QTimer::singleShot(0, this, &StudioPanelModel::refresh);
}
