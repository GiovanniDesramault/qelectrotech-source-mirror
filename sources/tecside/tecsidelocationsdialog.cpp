/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "tecside/tecsidelocationsdialog.h"

#include "qetgraphicsitem/element.h"
#include "qetinformation.h"
#include "qetproject.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QUndoStack>
#include <QVBoxLayout>

TecsideLocationModel::TecsideLocationModel(QObject *parent) :
	QAbstractListModel(parent)
{}

void TecsideLocationModel::setChoices(const QList<Choice> &choices)
{
	beginResetModel();
	m_choices = choices;
	endResetModel();
}

QList<TecsideLocationModel::Choice> TecsideLocationModel::choicesOf(
		const TecsideLocations &locations)
{
	QList<Choice> choices;
	if (!locations.isEnabled())
		return choices;
	for (const TecsideLocation &location : locations.list())
		choices.append({location.number, locations.label(location)});
	return choices;
}

/**
	Give the editable @p combo the places @p choices: nothing put in the
	list from what is typed, and no completion turning "0" into "01"
*/
void TecsideLocationModel::setUpCombo(QComboBox *combo, const QList<Choice> &choices)
{
	auto *model = qobject_cast<TecsideLocationModel *>(combo->model());
	if (!model) {
		model = new TecsideLocationModel(combo);
		combo->setModel(model);
	}
	combo->setEditable(true);
	combo->setInsertPolicy(QComboBox::NoInsert);
	model->setChoices(choices);
	combo->setCompleter(nullptr);
}

int TecsideLocationModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : m_choices.size();
}

QVariant TecsideLocationModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_choices.size())
		return QVariant();
	const Choice &choice = m_choices.at(index.row());
	if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
		return choice.second;
	if (role == Qt::EditRole)
		return choice.first;
	return QVariant();
}

TecsideLocationsDialog::TecsideLocationsDialog(const TecsideLocations &locations,
											   QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Manage locations"));

	m_table = new QTableWidget(0, 2, this);
	m_table->setHorizontalHeaderLabels({tr("Number"), tr("Name")});
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	for (const TecsideLocation &location : locations.list())
		addRow(location.number, location.name, location.number);
	m_table->resizeColumnToContents(0);

	auto *add = new QPushButton(tr("Add"), this);
	auto *remove = new QPushButton(tr("Remove"), this);
	auto *up = new QPushButton(tr("Move up"), this);
	auto *down = new QPushButton(tr("Move down"), this);
	connect(add, &QPushButton::clicked, this, &TecsideLocationsDialog::addLocation);
	connect(remove, &QPushButton::clicked, this, &TecsideLocationsDialog::removeLocation);
	connect(up, &QPushButton::clicked, this, [this]() { moveLocation(-1); });
	connect(down, &QPushButton::clicked, this, [this]() { moveLocation(1); });

	auto *side = new QVBoxLayout;
	side->addWidget(add);
	side->addWidget(remove);
	side->addSpacing(8);
	side->addWidget(up);
	side->addWidget(down);
	side->addStretch();

	auto *row = new QHBoxLayout;
	row->addWidget(m_table, 1);
	row->addLayout(side);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &TecsideLocationsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &TecsideLocationsDialog::reject);

	auto *layout = new QVBoxLayout(this);
	auto *hint = new QLabel(tr("The number of a location is what its devices carry "
							   "in their \"location\" information and in the cable "
							   "numbers. Renumbering a location moves its devices along."),
							this);
	hint->setWordWrap(true);
	layout->addWidget(hint);
	layout->addLayout(row);
	layout->addWidget(buttons);
	resize(460, 380);
}

/**
	A row of the table; @p original is the number the place had when the
	dialog opened, empty for a new one
*/
void TecsideLocationsDialog::addRow(const QString &number, const QString &name,
									const QString &original)
{
	const int row = m_table->rowCount();
	m_table->insertRow(row);
	auto *number_item = new QTableWidgetItem(number);
	number_item->setData(Qt::UserRole, original);
	m_table->setItem(row, 0, number_item);
	m_table->setItem(row, 1, new QTableWidgetItem(name));
}

QString TecsideLocationsDialog::numberAt(int row) const
{
	const QTableWidgetItem *item = m_table->item(row, 0);
	return item ? TecsideLocations::normalizedNumber(item->text()) : QString();
}

void TecsideLocationsDialog::addLocation()
{
	addRow(locations().nextFreeNumber(), QString(), QString());
	const int row = m_table->rowCount() - 1;
	m_table->setCurrentCell(row, 1);
	m_table->editItem(m_table->item(row, 1));
}

void TecsideLocationsDialog::removeLocation()
{
	const int row = m_table->currentRow();
	if (row >= 0)
		m_table->removeRow(row);
}

void TecsideLocationsDialog::moveLocation(int step)
{
	const int row = m_table->currentRow();
	const int to = row + step;
	if (row < 0 || to < 0 || to >= m_table->rowCount())
		return;
	for (int column = 0 ; column < 2 ; ++column) {
		QTableWidgetItem *a = m_table->takeItem(row, column);
		QTableWidgetItem *b = m_table->takeItem(to, column);
		m_table->setItem(row, column, b);
		m_table->setItem(to, column, a);
	}
	m_table->setCurrentCell(to, m_table->currentColumn());
}

/**
	@return the places as the table holds them, on: pressing OK in this
	dialog turns the feature on for a project that had it off
*/
TecsideLocations TecsideLocationsDialog::locations() const
{
	TecsideLocations locations;
	locations.setEnabled(true);
	QList<TecsideLocation> list;
	for (int row = 0 ; row < m_table->rowCount() ; ++row) {
		const QTableWidgetItem *name = m_table->item(row, 1);
		list.append({numberAt(row), name ? name->text().trimmed() : QString()});
	}
	locations.setList(list);
	return locations;
}

/**
	@return for each place whose number changed, its old number and its
	new one
*/
QHash<QString, QString> TecsideLocationsDialog::renumbering() const
{
	QHash<QString, QString> old_to_new;
	for (int row = 0 ; row < m_table->rowCount() ; ++row) {
		const QString original = m_table->item(row, 0)->data(Qt::UserRole).toString();
		const QString number = numberAt(row);
		if (!original.isEmpty() && original != number)
			old_to_new.insert(original, number);
	}
	return old_to_new;
}

void TecsideLocationsDialog::accept()
{
	QSet<QString> numbers;
	for (int row = 0 ; row < m_table->rowCount() ; ++row) {
		const QString number = numberAt(row);
		if (number.isEmpty()) {
			QMessageBox::warning(this, windowTitle(),
								 tr("Every location needs a number."));
			m_table->setCurrentCell(row, 0);
			return;
		}
		if (numbers.contains(number)) {
			QMessageBox::warning(this, windowTitle(),
								 tr("The number %1 is used twice.").arg(number));
			m_table->setCurrentCell(row, 0);
			return;
		}
		numbers.insert(number);
	}
	QDialog::accept();
}

void TecsideLocationsDialog::edit(QETProject *project, QWidget *parent)
{
	if (!project || project->isReadOnly())
		return;
	TecsideLocationsDialog dialog(project->tecsideLocations(), parent);
	if (dialog.exec() != QDialog::Accepted)
		return;
	const TecsideLocations old_locations = project->tecsideLocations();
	const TecsideLocations new_locations = dialog.locations();
	auto *macro = new QUndoCommand(tr("Manage locations"));
	if (new_locations != old_locations)
		new ChangeTecsideLocationsCommand(project, old_locations, new_locations, macro);
	TecsideLocationEdit::renumber(project, dialog.renumbering(), macro);
	if (!macro->childCount()) {
		delete macro;
		return;
	}
	project->undoStack()->push(macro);
}

TecsideSetLocationDialog::TecsideSetLocationDialog(const TecsideLocations &locations,
												   const QString &current,
												   QWidget *parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Set location"));
	m_combo = new QComboBox(this);
	TecsideLocationModel::setUpCombo(m_combo, TecsideLocationModel::choicesOf(locations));
	m_combo->setCurrentIndex(-1);
	m_combo->setEditText(current);
	m_combo->setMinimumContentsLength(20);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *form = new QFormLayout;
	form->addRow(tr("Location:"), m_combo);
	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
	m_combo->setFocus();
	m_combo->lineEdit()->selectAll();
}

QString TecsideSetLocationDialog::location() const
{
	return TecsideLocations::normalizedNumber(m_combo->currentText());
}

void TecsideSetLocationDialog::edit(QETProject *project, const QList<Element *> &elements,
									QWidget *parent)
{
	if (!project || project->isReadOnly() || elements.isEmpty())
		return;
		//The place they all share, if they do
	QSet<QString> current;
	for (Element *element : elements)
		current.insert(element->elementInformations()
					   .value(QETInformation::ELMT_LOCATION).toString());
	TecsideSetLocationDialog dialog(project->tecsideLocations(),
									current.size() == 1 ? *current.cbegin() : QString(),
									parent);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (QUndoCommand *command = TecsideLocationEdit::setLocation(elements, dialog.location()))
		project->undoStack()->push(command);
}
