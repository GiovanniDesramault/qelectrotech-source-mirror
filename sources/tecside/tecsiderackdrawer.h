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
#ifndef TECSIDERACKDRAWER_H
#define TECSIDERACKDRAWER_H

#include <QHash>
#include <QMap>
#include <QString>

class Diagram;
class Element;
class QETProject;
class QWidget;

/**
	@brief Project > Draw rack for location: a new folio with the front
	view of the rack of one place, from the rack symbols of the company
	collection (av/rack, written by build_rack.py). Drawn once: what is
	on it are ordinary elements, moved, removed and added by hand after.
*/
namespace TecsideRackDrawer
{
		/// The rack symbols of a collection, by collection path
		/// ("company://av/rack/...")
	struct Symbols
	{
		QHash<QString, QString> fronts;	///< device id -> its front view
		QMap<int, QString> frames;		///< units -> frame
		QMap<int, QString> blinds;		///< units -> blind panel
		QMap<int, QString> shelves;		///< units -> shelf

		bool isComplete() const
		{return !frames.isEmpty() && blinds.contains(1) && !shelves.isEmpty();}
	};

	Symbols findSymbols(const QString &collection_dir,
						const QString &protocol = QStringLiteral("company://"));
	QString deviceId(const Element *element);
	bool isRackSymbol(const Element *element);

		/// Draw the rack of the place numbered @p number on a new folio at
		/// the end of @p project, one undo step. @return the folio, or
		/// nullptr with @p error saying why.
	Diagram *draw(QETProject *project, const QString &number,
				  const Symbols &symbols, QString *error = nullptr);

		/// Ask for a place of @p project and draw its rack, from the
		/// company collection
	void drawWithDialog(QETProject *project, QWidget *parent);
}

#endif
