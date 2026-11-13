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
#ifndef TECSIDELOCATIONSDIALOG_H
#define TECSIDELOCATIONSDIALOG_H

#include "tecside/tecsidelocations.h"

#include <QAbstractListModel>
#include <QDialog>
#include <QHash>
#include <utility>

class QComboBox;
class QTableWidget;

/**
	@brief The places as an editable combo box offers them: "03 – FOH" in
	the list, only "03" written in the field once one is picked (an
	editable QComboBox writes the EditRole of the item it takes).
*/
class TecsideLocationModel : public QAbstractListModel
{
	Q_OBJECT

	public:
		using Choice = std::pair<QString, QString>; ///< value, label

		explicit TecsideLocationModel(QObject *parent = nullptr);

		void setChoices(const QList<Choice> &choices);
		const QList<Choice> &choices() const {return m_choices;}
		static QList<Choice> choicesOf(const TecsideLocations &locations);
		static void setUpCombo(QComboBox *combo, const QList<Choice> &choices);

		int rowCount(const QModelIndex &parent = QModelIndex()) const override;
		QVariant data(const QModelIndex &index, int role) const override;

	private:
		QList<Choice> m_choices;
};

/**
	@brief Project > Manage locations: the places of the project, their
	number and name, in order. Renumbering a place moves its elements
	along (renumbering()).
*/
class TecsideLocationsDialog : public QDialog
{
	Q_OBJECT

	public:
		explicit TecsideLocationsDialog(const TecsideLocations &locations,
										QWidget *parent = nullptr);

		TecsideLocations locations() const;
		QHash<QString, QString> renumbering() const;
		void accept() override;

			/// Ask for the places of @p project and push them, with the
			/// renumbered elements, as one undo step
		static void edit(QETProject *project, QWidget *parent);

	private:
		void addRow(const QString &number, const QString &name,
					const QString &original);
		void addLocation();
		void removeLocation();
		void moveLocation(int step);
		QString numberAt(int row) const;

		QTableWidget *m_table = nullptr;
};

/**
	@brief Set location: the place to give all the selected elements
*/
class TecsideSetLocationDialog : public QDialog
{
	Q_OBJECT

	public:
		TecsideSetLocationDialog(const TecsideLocations &locations,
								 const QString &current,
								 QWidget *parent = nullptr);
		QString location() const;

			/// Ask for a place and give it to @p elements, as one undo step
		static void edit(QETProject *project, const QList<Element *> &elements,
						 QWidget *parent);

	private:
		QComboBox *m_combo = nullptr;
};

#endif
