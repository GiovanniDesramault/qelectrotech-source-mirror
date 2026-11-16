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
#ifndef TECSIDEREPLACE_H
#define TECSIDEREPLACE_H

#include <QStringList>

class Element;
class ElementsLocation;
class QETProject;
class QWidget;

/**
	@brief Edit > Replace with: put another symbol in the place of one
	device, keeping its wiring. Each wire goes to the terminal of the new
	symbol with the same name (HDMI OUT to HDMI OUT), with its properties
	and so its cable number; label, location and the other information
	the new symbol does not fill itself come along. One undo step.
*/
namespace TecsideReplace
{
		/// Replace @p old by the symbol at @p location. @return the new
		/// element, or nullptr when that symbol cannot be loaded; @p lost
		/// gets the terminals of @p old whose wire had no terminal to go to
		/// (that wire is removed with the old symbol).
	Element *replace(Element *old, const ElementsLocation &location,
					 QStringList *lost = nullptr);

		/// Ask for a symbol of the collections and replace @p old by it
	void replaceWithDialog(Element *old, QWidget *parent);

		/// Bring the symbols of @p project that changed in their
		/// collection up to date, and put a fresh copy in the place of each
		/// one placed: new texts (label in the header, location, annotation)
		/// come with it, the wiring and information stay. @return how many
		/// were placed again; @p lost as for replace(). The placing again
		/// is one undo step; updating the project's copies is not undone.
	int refreshSymbols(QETProject *project, QStringList *lost = nullptr);

		/// Ask, refresh the symbols of @p project and say how it went
	void refreshWithDialog(QETProject *project, QWidget *parent);
}

#endif
