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

#include "cable/cable.h"
#include "cable/cablemanager.h"
#include "conductorproperties.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDebug>
#include <QHash>
#include <QList>
#include <QMap>
#include <QUuid>
#include <algorithm>

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

		//One core of a multicore cable
	struct Core
	{
		Conductor *conductor = nullptr;
		ConductorProperties properties;
	};

		//What takes one number: a single wire, or a whole multicore cable
		//(then @a conductor and @a properties are those of its first core,
		//or of the first one that already holds the number)
	struct Wire
	{
		QUuid id;
		Conductor *conductor = nullptr;
		ConductorProperties properties;
		QString type;
		bool keeps = false;
		Cable *cable = nullptr;
		QList<Core> cores;
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
	A multicore cable takes one number from the same per-type counter, for
	its name and for all its cores (written to every core, so the number
	survives the loss of the first one).
*/
void refreshProject(QETProject *project)
{
	if (!project) return;
	QList<Wire> wires;
	QMap<QUuid, Wire> cables;
	QMap<QUuid, QList<QPair<QString, Core>>> cores;
	for (Diagram *folio : project->folios()) {
		if (!folio || !folio->isLoaded()) continue;
		for (Conductor *c : folio->conductors()) {
			Wire wire;
			wire.conductor = c;
			wire.properties = c->properties();
			if (!wire.properties.m_tecside_number) continue;
			wire.id = c->uuid();
			wire.type = wire.properties.m_potential_type.isEmpty()
					? project->derivedPotentialType(c->uuid())
					: wire.properties.m_potential_type;
			Cable *cable = CableManager::cableByUuid(
					project, wire.properties.m_cable_uuid);
			if (cable)
				cores[cable->uuid()] << qMakePair(
						wire.type, Core{c, wire.properties});
			else
				wires << wire;
		}
	}
		//A multicore takes one number for all its cores. Its type is the
		//one of its first core.
	for (auto it = cores.begin() ; it != cores.end() ; ++it) {
		auto &list = it.value();
		std::sort(list.begin(), list.end(),
				  [](const QPair<QString, Core> &a,
					 const QPair<QString, Core> &b) {
			const int sa = a.second.properties.m_cable_slot;
			const int sb = b.second.properties.m_cable_slot;
			return sa != sb ? sa < sb
					: a.second.conductor->uuid().toString()
					< b.second.conductor->uuid().toString();
		});
		Wire wire;
		wire.id = it.key();
		wire.cable = CableManager::cableByUuid(project, it.key());
		wire.type = list.first().first;
		for (const auto &core : std::as_const(list)) {
			if (core.first != wire.type)
				qWarning() << "TECside cable number: cable"
						   << wire.cable->designation()
						   << "mixes cable types" << wire.type
						   << "and" << core.first;
			wire.cores << core.second;
		}
		wire.conductor = wire.cores.first().conductor;
		wire.properties = wire.cores.first().properties;
		for (const Core &core : std::as_const(wire.cores)) {
			if (core.properties.m_type_seq >= 1
					&& core.properties.m_type_seq_of == wire.type) {
				wire.properties = core.properties;
				break;
			}
		}
		wires << wire;
	}
	if (wires.isEmpty()) return;

		//The numbers of wires in folios not built stay where they are
	QHash<QString, QSet<int>> used;
	if (projectDataBase *store = project->dataBase())
		for (const auto &number : store->unplacedCableNumbers())
			used[number.first].insert(number.second);

		//Who keeps a number several wires hold: the one that held it at
		//the last refresh (a copy never takes it from its original), the
		//lowest uuid when none or several did
	auto &memory = project->cableNumberMemory();
	QMap<QPair<QString, int>, QString> keeper;
	QMap<QPair<QString, int>, QString> previous;
	QSet<QPair<QString, int>> several;
	const auto held = [](const Wire &w) {
		return !w.type.isEmpty() && w.properties.m_type_seq >= 1
				&& w.properties.m_type_seq_of == w.type;
	};
	for (const Wire &w : std::as_const(wires)) {
		if (!held(w) || used.value(w.type).contains(w.properties.m_type_seq))
			continue;
		const QPair<QString, int> key(w.type, w.properties.m_type_seq);
		const QString uuid = w.id.toString();
		const auto known = keeper.constFind(key);
		if (known == keeper.constEnd() || uuid < *known)
			keeper.insert(key, uuid);
		if (memory.value(w.id) == key) {
			if (previous.contains(key)) several.insert(key);
			previous.insert(key, uuid);
		}
	}
	for (auto it = previous.constBegin() ; it != previous.constEnd() ; ++it)
		if (!several.contains(it.key()))
			keeper.insert(it.key(), it.value());
	for (Wire &w : wires) {
		if (!held(w)) continue;
		w.keeps = keeper.value({w.type, w.properties.m_type_seq})
				== w.id.toString();
		if (w.keeps) used[w.type].insert(w.properties.m_type_seq);
	}

	for (Wire &w : wires) {
		int seq = -1;
		QString text;
		if (!w.type.isEmpty()) {
			seq = w.properties.m_type_seq;
			if (!w.keeps) {
				seq = nextFree(used.value(w.type));
				used[w.type].insert(seq);
			}
			const auto ends = fromTo(
					locationOf(w.conductor->terminal1),
					signalOf(w.conductor->terminal1),
					locationOf(w.conductor->terminal2),
					signalOf(w.conductor->terminal2));
			text = format(w.type, seq, ends.first, ends.second);
		}
		if (seq >= 1)
			memory.insert(w.id, {w.type, seq});
		else
			memory.remove(w.id);

		if (!w.cable) {
			ConductorProperties p = w.properties;
			p.m_type_seq = seq;
			p.m_type_seq_of = seq >= 1 ? w.type : QString();
			p.text = text;
				//A formula would write the text again
			p.m_formula.clear();
			if (p != w.properties)
				w.conductor->setProperties(p);
			continue;
		}

			//Like the numbers of single wires, the name of a cable is
			//derived and is written without an undo step of its own,
			//unless the user typed one in
		QString name = text;
		if (!text.isEmpty() && !w.cable->designationByHand())
			w.cable->setDesignation(text);
		else if (w.cable->designationByHand()
				 && !w.cable->designation().isEmpty())
			name = w.cable->designation();
		for (const Core &core : std::as_const(w.cores)) {
			ConductorProperties p = core.properties;
			p.m_type_seq = seq;
			p.m_type_seq_of = seq >= 1 ? w.type : QString();
			p.text = name.isEmpty() ? QString()
					: QStringLiteral("%1/%2").arg(
						  name, QString::number(p.m_cable_slot + 1));
			p.m_formula.clear();
			if (p != core.properties)
				core.conductor->setProperties(p);
		}
	}
}
}
