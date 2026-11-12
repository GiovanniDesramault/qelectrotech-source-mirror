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
#ifndef TECSIDECABLENUMBER_H
#define TECSIDECABLENUMBER_H

#include "properties/terminaldata.h"

#include <QSet>
#include <QString>
#include <utility>

class Conductor;
class QETProject;
class Terminal;

/**
	@brief The TECside cable numbers of single cables: TYPE-seq-from-to,
	as in SDI-012-03-07 (cable type, number within that type, location
	of the output side, location of the input side).
*/
namespace TecsideCableNumber
{
	QString format(const QString &code, int seq,
				   const QString &from, const QString &to);
	int nextFree(const QSet<int> &used);
	std::pair<QString, QString> fromTo(
			const QString &loc1, TerminalData::Signal s1,
			const QString &loc2, TerminalData::Signal s2);
	std::pair<Terminal *, Terminal *> orderedEnds(Conductor *conductor);
	void refreshProject(QETProject *project);
}

#endif
