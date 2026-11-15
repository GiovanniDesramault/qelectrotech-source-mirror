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
#ifndef TECSIDEMATERIALS_H
#define TECSIDEMATERIALS_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

class Diagram;
class QETProject;
class QWidget;

/**
	@brief Project > Materials list: what to load for a job. The devices
	counted per model and per place; the cables counted per type and per
	run, a run being one place or two places joined. Every physical cable
	counts once: each link of a speaker chain, a multicore once, a wire
	drawn through corner symbols once. The same lines go to a CSV file and
	to a folio.
*/
namespace TecsideMaterials
{
	struct Line
	{
		QString place;	///< "01 – FOH", "01 – FOH ↔ 02 – Stage", or none
		bool cable = false;
		QString item;	///< "Blackmagic Micro Converter", "SDI"
		int count = 0;
	};

		/// The lines of @p project: by place in the order of the list
		/// (devices, then cables), the runs between places after, the
		/// things without a place last; within a place by item
	QList<Line> lines(QETProject *project);

	QStringList headers();
	QByteArray toCsv(QETProject *project);

		/// The lines on a new folio at the end of @p project, one undo
		/// step. @return the folio, or nullptr when there is nothing to list
	Diagram *draw(QETProject *project);

		/// Ask for a file and write the CSV of @p project to it
	void exportWithDialog(QETProject *project, QWidget *parent);
}

#endif
