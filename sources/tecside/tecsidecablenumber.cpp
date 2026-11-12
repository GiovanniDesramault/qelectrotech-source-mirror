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
#include "tecside/tecsidecablenumber.h"

#include "conductorproperties.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QUuid>

namespace TecsideCableNumber
{
namespace
{
	QString padded(const QString &location)
	{
		if (location.isEmpty()) return QStringLiteral("00");
		return location.rightJustified(2, QLatin1Char('0'));
	}

		//Output side first, then no direction, then input side
	int rank(TerminalData::Signal signal)
	{
		switch (signal) {
		case TerminalData::Signal::Out: return 0;
		case TerminalData::Signal::None: return 1;
		default: return 2;
		}
	}

	struct Wire
	{
		Conductor *conductor = nullptr;
		ConductorProperties properties;
		QString type;
		bool keeps = false;
	};

	QString locationOf(Terminal *terminal)
	{
		Element *element = terminal ? terminal->parentElement() : nullptr;
		return element ? element->elementInformations()
						 .value(QStringLiteral("location")).toString()
					   : QString();
	}

	TerminalData::Signal signalOf(Terminal *terminal)
	{
		const QString name = terminal ? terminal->signalName() : QString();
		if (name == QLatin1String("out")) return TerminalData::Signal::Out;
		if (name == QLatin1String("in")) return TerminalData::Signal::In;
		return TerminalData::Signal::None;
	}
}

/**
	@return "CODE-seq-from-to", the number padded to three digits and each
	location to two ("00" when it is empty); empty when @p code is
*/
QString format(const QString &code, int seq,
			   const QString &from, const QString &to)
{
	if (code.isEmpty()) return QString();
	return QStringLiteral("%1-%2-%3-%4").arg(
			code, QString::number(seq).rightJustified(3, QLatin1Char('0')),
			padded(from), padded(to));
}

/**
	@return the smallest integer from 1 on that is not in @p used
*/
int nextFree(const QSet<int> &used)
{
	int seq = 1;
	while (used.contains(seq)) ++seq;
	return seq;
}

/**
	@return the locations of the two ends of a cable, the output side
	first; with no direction to tell them apart the lower location first
*/
std::pair<QString, QString> fromTo(
		const QString &loc1, TerminalData::Signal s1,
		const QString &loc2, TerminalData::Signal s2)
{
	const QString a = padded(loc1);
	const QString b = padded(loc2);
	if (rank(s1) != rank(s2))
		return rank(s1) < rank(s2) ? std::make_pair(a, b)
								   : std::make_pair(b, a);
	return b < a ? std::make_pair(b, a) : std::make_pair(a, b);
}

/**
	Number the wires of @p project that ask for it. The number rides on
	the refresh of the derived types and is written to the wire without an
	undo step of its own, so undoing the wire takes it away as well.
	Two wires that carry the same type and number (a copy) are told apart
	by their uuid: the lower one keeps it, the others take the next free.
*/
void refreshProject(QETProject *project)
{
	if (!project) return;
	QList<Wire> wires;
	QSet<QUuid> built;
	for (Diagram *folio : project->folios()) {
		if (!folio || !folio->isLoaded()) continue;
		for (Conductor *c : folio->conductors()) {
			built.insert(c->uuid());
			Wire wire;
			wire.conductor = c;
			wire.properties = c->properties();
			if (!wire.properties.m_tecside_number) continue;
			wire.type = wire.properties.m_potential_type.isEmpty()
					? project->derivedPotentialType(c->uuid())
					: wire.properties.m_potential_type;
			wires << wire;
		}
	}
	if (wires.isEmpty()) return;

		//The numbers of wires in folios not built stay where they are
	QHash<QString, QSet<int>> used;
	if (projectDataBase *store = project->dataBase())
		for (const auto &number : store->unplacedCableNumbers())
			used[number.first].insert(number.second);

		//Who keeps a number several wires hold: the lowest uuid
	QMap<QPair<QString, int>, QString> keeper;
	const auto held = [](const Wire &w) {
		return !w.type.isEmpty() && w.properties.m_type_seq >= 1
				&& w.properties.m_type_seq_of == w.type;
	};
	for (const Wire &w : std::as_const(wires)) {
		if (!held(w) || used.value(w.type).contains(w.properties.m_type_seq))
			continue;
		const QPair<QString, int> key(w.type, w.properties.m_type_seq);
		const QString uuid = w.conductor->uuid().toString();
		const auto known = keeper.constFind(key);
		if (known == keeper.constEnd() || uuid < *known)
			keeper.insert(key, uuid);
	}
	for (Wire &w : wires) {
		if (!held(w)) continue;
		w.keeps = keeper.value({w.type, w.properties.m_type_seq})
				== w.conductor->uuid().toString();
		if (w.keeps) used[w.type].insert(w.properties.m_type_seq);
	}

	for (Wire &w : wires) {
		ConductorProperties p = w.properties;
		if (w.type.isEmpty()) {
			p.m_type_seq = -1;
			p.m_type_seq_of.clear();
			p.text.clear();
		} else {
			if (!w.keeps) {
				p.m_type_seq = nextFree(used.value(w.type));
				p.m_type_seq_of = w.type;
				used[w.type].insert(p.m_type_seq);
			}
			const auto ends = fromTo(
					locationOf(w.conductor->terminal1),
					signalOf(w.conductor->terminal1),
					locationOf(w.conductor->terminal2),
					signalOf(w.conductor->terminal2));
			p.text = format(w.type, p.m_type_seq, ends.first, ends.second);
		}
			//A formula would write the text again
		p.m_formula.clear();
		if (p == w.properties) continue;
		w.conductor->setProperties(p);
	}
}
}
